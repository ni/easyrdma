// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaConnectedSession.h"
#include "RdmaConnectionData.h"
#include "RdmaBufferQueue.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <assert.h>
#include <boost/endian/buffers.hpp>

using namespace EasyRDMA;

#include "ThreadUtility.h"

RdmaConnectedSession::RdmaConnectedSession() : RdmaConnectedSessionBase(), _closing(false) {
}


RdmaConnectedSession::RdmaConnectedSession(Direction _direction, IND2Adapter* _adapter, HANDLE _adapterFile, IND2Connector* _incomingConnection, const std::vector<uint8_t>& _connectionData, int32_t timeoutMs)
    :   RdmaConnectedSessionBase(_connectionData),
        adapterFile(_adapterFile),
        adapter(_adapter),
        connector(_incomingConnection),
        _closing(false)
{
    try {
        PreConnect(_direction);
        AcquireAndValidateConnectionData(_incomingConnection, _direction);
        OverlappedWrapper overlapped;
        HandleHROverlappedWithTimeout(_incomingConnection->Accept(
                                qp,
                                0,
                                0,
                                connectionData.data(),
                                static_cast<ULONG>(connectionData.size()),
                                overlapped), connector, overlapped, timeoutMs);
        PostConnect();
    }
    catch(std::exception&) {
        Destroy();
        throw;
    }
}


void RdmaConnectedSession::Destroy() {
    _closing = true;
    try {
        if (connector.get()) {
            OverlappedWrapper overlapped;
            HandleHROverlapped(connector->Disconnect(overlapped), connector, overlapped);
        }
    }
    catch(std::exception&) {
    }
    if (connectionHandler.joinable()) {
        connectionHandler.join();
    }
    connector.reset();
    if(qp.get()) {
        qp->Flush();
        qp.reset();
    }
    if(cq.get()) {
        cq->CancelOverlappedRequests();
        cq.reset();
    }
    if (eventHandler.joinable()) {
        eventHandler.join();
    }
    if (adapterFile) {
        // We are not supposed to close this since it is owned by the adapter
        adapterFile = nullptr;
    }
}


RdmaConnectedSession::~RdmaConnectedSession() {
    Destroy();
}


void RdmaConnectedSession::SetupQueuePair() {
    assert(!cq.get() && !qp.get());
    OverlappedWrapper overlapped;
    ND2_ADAPTER_INFO adapterInfo = { };
    adapterInfo.InfoVersion = ND_VERSION_2;
    ULONG adapterInfoSize = sizeof(adapterInfo);
    HandleHR(adapter->Query(&adapterInfo, &adapterInfoSize));

    // We could consider limiting this to the max number of outstanding requests provided by the user,
    // but realistically most NICs seem to have the maximum be virtually unbounded, so it doesn't seem
    // to matter.
    DWORD queueDepth = std::min(adapterInfo.MaxCompletionQueueDepth, adapterInfo.MaxInitiatorQueueDepth);
    DWORD inlineThreshold = adapterInfo.InlineRequestThreshold;

    HandleHR(adapter->CreateCompletionQueue(
                    IID_IND2CompletionQueue,
                    adapterFile,
                    queueDepth,
                    0,
                    0,
                    cq));

    DWORD nSge = 2; // Allow wrapping around circular buffer
    HandleHR(adapter->CreateQueuePair(
        IID_IND2QueuePair,
        cq,
        cq,
        nullptr,
        queueDepth,
        queueDepth,
        nSge,
        nSge,
        inlineThreshold,
        qp));
}


void RdmaConnectedSession::DestroyQP() {
    qp.reset();
    cq.reset();
}


void RdmaConnectedSession::PostConnect() {
    ULONG addrSize = static_cast<ULONG>(sizeof(remoteAddress.address));
    HandleHR(connector->GetPeerAddress(reinterpret_cast<sockaddr*>(&remoteAddress.address), &addrSize));

    assert(!eventHandler.joinable());
    eventHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::EventHandlerThread, this), kThreadPriority::Normal, "EventHandler");

    RdmaConnectedSessionBase::PostConnect();
    connectionHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::ConnectionHandlerThread, this), kThreadPriority::Normal, "ConnHandler");
}


void RdmaConnectedSession::ConnectionHandlerThread() {
    OverlappedWrapper overlappedLocal;
    try {
        HandleHROverlapped(connector->NotifyDisconnect(overlappedLocal), connector, overlappedLocal);
        // Calling Disconnect will cause the QPs to start returning cancelled errors, so we make sure we handle the
        // disconnect how we like first.
        HandleDisconnect();
        HandleHROverlapped(connector->Disconnect(overlappedLocal), connector, overlappedLocal);
    }
    catch(std::exception& ) {
        // No-op, silently exit
    }
}


void RdmaConnectedSession::EventHandlerThread() {
    OverlappedWrapper overlappedLocal;
    try {
        while(!_closing && cq.get()) {
            ND2_RESULT ndRes = {};
            while(!_closing && cq.get() && cq->GetResults(&ndRes, 1) > 0) {
                RdmaBuffer* buffer = static_cast<RdmaBuffer*>(ndRes.RequestContext);
                RdmaError completionStatus;
                if(ndRes.Status != ND_SUCCESS) {
                    try {
                        RDMA_THROW_WITH_SUBCODE(RdmaErrorTranslation::OSErrorToRdmaError(ndRes.Status), ndRes.Status);
                    }
                    catch(const RdmaException& e) {
                        completionStatus.Assign(e.rdmaError);
                    }
                }
                size_t bytesTransferred;
                switch(ndRes.RequestType) {
                    case Nd2RequestTypeReceive:
                        bytesTransferred = ndRes.BytesTransferred;
                        break;
                    case Nd2RequestTypeSend:
                        bytesTransferred = completionStatus.IsSuccess() ? buffer->GetUsed() : 0;
                        break;
                    default:
                        RDMA_THROW(easyrdma_Error_InternalError);
                }
                buffer->HandleCompletion(completionStatus, bytesTransferred);
            }
            HandleHROverlapped(cq->Notify(ND_CQ_NOTIFY_ANY, overlappedLocal), cq, overlappedLocal);
        }
    }
    catch(std::exception&) {
        // No-op, silently exit thread. Normal errors are handled within the completion methods.
    }
}


RdmaAddress RdmaConnectedSession::GetLocalAddress() {
    RdmaAddress address;
    ULONG addrSize = static_cast<ULONG>(sizeof(address.address));
    HandleHR(connector->GetLocalAddress(reinterpret_cast<sockaddr*>(&address.address), &addrSize));
    return address;
}


RdmaAddress RdmaConnectedSession::GetRemoteAddress() {
    return remoteAddress;
}


std::unique_ptr<RdmaMemoryRegion> RdmaConnectedSession::CreateMemoryRegion(void* buffer, size_t bufferSize) {
    AutoRef<IND2MemoryRegion> memoryRegion;
    HandleHR(adapter->CreateMemoryRegion(
            IID_IND2MemoryRegion,
            adapterFile,
            memoryRegion));
    OverlappedWrapper overlapped;
    HandleHROverlapped(memoryRegion->Register(
        buffer,
        bufferSize,
        ND_MR_FLAG_ALLOW_LOCAL_WRITE,
        overlapped), memoryRegion, overlapped);

    std::unique_ptr<RdmaMemoryRegion> memoryRegionWrapper(new RdmaMemoryRegion(std::move(memoryRegion), buffer, bufferSize));
    return std::move(memoryRegionWrapper);
}


void RdmaConnectedSession::QueueToQp(Direction _direction, RdmaBuffer* buffer) {
    ND2_SGE sge = {};
    sge.Buffer = buffer->GetBuffer();
    sge.BufferLength = _direction == Direction::Receive ? static_cast<ULONG>(buffer->GetBufferLen())
                                                        : static_cast<ULONG>(buffer->GetUsed());
    sge.MemoryRegionToken = buffer->GetMemoryRegion()->GetMRLocalToken();

    HandleHR(_direction == Direction::Receive ? GetQP()->Receive(buffer, &sge, 1)
                                              : GetQP()->Send(buffer, &sge, 1, 0));
}


void RdmaConnectedSession::AcquireAndValidateConnectionData(IND2Connector* connector, Direction direction) {
    OverlappedWrapper overlapped;
    // Arbitrary size larger than the largest possible connection data buffer
    static const uint32_t kConnectionDataBufferSize = 1024;
    std::vector<uint8_t> connectionDataBuffer(kConnectionDataBufferSize, 0);
    ULONG cdSize = static_cast<ULONG>(connectionDataBuffer.size());
    HandleHROverlapped(connector->GetPrivateData(&connectionDataBuffer[0], &cdSize), connector, overlapped);
    connectionDataBuffer.resize(cdSize);
    ValidateConnectionData(connectionDataBuffer, direction);
}

void RdmaConnectedSession::PollForReceive(int32_t timeoutMs) {
    // Shouldn't get here since it isn't allowed
    RDMA_THROW(easyrdma_Error_InternalError);
}
