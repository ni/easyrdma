// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaConnectedSessionBase.h"
#include "RdmaConnectionData.h"
#include "RdmaBufferQueue.h"
#include <assert.h>
#include <algorithm>
#include "api/tAccessSuspender.h"

#include "common/ThreadUtility.h"

static const size_t kMaxCreditsPerBuffer = 100;

using namespace EasyRDMA;


class BufferWaitAccessSuspender : public tAccessSuspender {
    public:
        BufferWaitAccessSuspender(iAccessManaged* ref, bool& _inProgressFlag) : tAccessSuspender(ref, false), inProgressFlag(_inProgressFlag) {
            if(inProgressFlag) {
                RDMA_THROW(easyrdma_Error_BufferWaitInProgress);
            }
            inProgressFlag = true;
            Suspend();
        }
        ~BufferWaitAccessSuspender() {
            inProgressFlag = false;
        }
    protected:
        bool& inProgressFlag;
};


RdmaConnectedSessionBase::RdmaConnectedSessionBase()
    :   direction(Direction::Unknown),
        autoQueueRx(false),
        bufferOwnership(BufferOwnership::Unknown),
        bufferType(BufferType::Unknown),
        connectionData()
{
}


RdmaConnectedSessionBase::RdmaConnectedSessionBase(const std::vector<uint8_t>& _connectionData) : RdmaConnectedSessionBase() {
    connectionData = _connectionData;
}


RdmaConnectedSessionBase::~RdmaConnectedSessionBase() {
    _closing = true;
    Cancel();
    creditBuffers.reset();
    transferBuffers.reset();
}


void RdmaConnectedSessionBase::Cancel() {
    if(creditBuffers) {
        creditBuffers->Abort(easyrdma_Error_OperationCancelled);
    }
    if(transferBuffers) {
        transferBuffers->Abort(easyrdma_Error_OperationCancelled);
    }
    if (ackHandler.joinable()) {
        ackHandler.join();
    }
}


void RdmaConnectedSessionBase::PreConnect(Direction _direction) {
    direction = _direction;
    if (!connectionData.size()) {
        connectionData = CreateDefaultConnectionData(direction);
    }
    SetupQueuePair();
    creditBuffers.reset(new RdmaBufferQueueMultipleBuffer(*this, direction == Direction::Receive ? Direction::Send : Direction::Receive, 100, kMaxCreditsPerBuffer * sizeof(uint64_t), false));
    if(direction == Direction::Send) {
        for(size_t i = 0; i < creditBuffers->size(); ++i) {
            RdmaBuffer* buffer = creditBuffers->WaitForIdleBuffer(0);
            creditBuffers->QueueBuffer(buffer, RdmaBufferQueue::IgnoreCredits::Yes);
        }
        ackHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSessionBase::AckHandlerThread, this), kThreadPriority::Normal, "AckHandler");
    }
}


void RdmaConnectedSessionBase::PostConnect() {
    connected = true;
}


void RdmaConnectedSessionBase::HandleDisconnect() {
    connected = false;
    std::unique_lock<std::mutex> guard(configureLock);
    if(transferBuffers) {
        transferBuffers->Abort(easyrdma_Error_Disconnected);
    }
    if(creditBuffers) {
        creditBuffers->Abort(easyrdma_Error_Disconnected);
    }
}


void RdmaConnectedSessionBase::AddCredit(uint64_t bufferSize) {
    // We need to hold this lock since this gets called asynchronously
    // from Configure (and might come before that happens).
    // If we are called before Configure, we need to store the credits
    // and pass them to the transferBuffers queue once configure happens
    std::unique_lock<std::mutex> guard(configureLock);
    if(transferBuffers) {
        transferBuffers->AddCredit(bufferSize);
    }
    else {
        preConfigureCredits.push(bufferSize);
    }
}


void RdmaConnectedSessionBase::AckHandlerThread() {
    try {
        while(!_closing) {
            RdmaBuffer* creditBuffer = creditBuffers->WaitForCompletedBuffer(-1);
            size_t size = creditBuffer->GetUsed();
            size_t numCredits = size / sizeof(boost::endian::big_uint64_buf_t);
            for(size_t i = 0; i < numCredits; ++i) {
                uint64_t bufferSizeQueued = reinterpret_cast<boost::endian::big_uint64_buf_t*>(creditBuffer->GetBuffer())[i].value();
                AddCredit(bufferSizeQueued);
            }
            creditBuffers->QueueBuffer(creditBuffer, RdmaBufferQueue::IgnoreCredits::Yes);
        }
    }
    catch(std::exception&) {
        // No-op, silently exit thread.
    }
}


bool RdmaConnectedSessionBase::IsConnected() const {
    return connected;
}


void RdmaConnectedSessionBase::ProcessPreConfigureCredits() {
    while(preConfigureCredits.size()) {
        transferBuffers->AddCredit(preConfigureCredits.front());
        preConfigureCredits.pop();
    }
}


void RdmaConnectedSessionBase::ConfigureExternalBuffer(void* externalBuffer, size_t bufferSize, size_t maxConcurrentTransactions) {
    {
        std::unique_lock<std::mutex> guard(configureLock);
        if(transferBuffers) {
            RDMA_THROW(easyrdma_Error_AlreadyConfigured);
        }
        if(usePolling) {
            RDMA_THROW(easyrdma_Error_OperationNotSupported);
        }
        bufferOwnership = BufferOwnership::External;
        bufferType = BufferType::Single;
        transferBuffers.reset(new RdmaBufferQueueSingleBuffer(*this, direction, externalBuffer, bufferSize, maxConcurrentTransactions, usePolling));
        ProcessPreConfigureCredits();
    }
    PostConfigure();
}


void RdmaConnectedSessionBase::ConfigureBuffers(size_t maxTransactionSize, size_t maxConcurrentTransactions) {
    {
        std::unique_lock<std::mutex> guard(configureLock);
        if(transferBuffers) {
            RDMA_THROW(easyrdma_Error_AlreadyConfigured);
        }
        if(!connected) {
            RDMA_THROW(easyrdma_Error_NotConnected);
        }
        bufferOwnership = BufferOwnership::Internal;
        bufferType = BufferType::Multiple;
        autoQueueRx = true;
        transferBuffers.reset(new RdmaBufferQueueMultipleBuffer(*this, direction, maxConcurrentTransactions, maxTransactionSize, usePolling));
        ProcessPreConfigureCredits();
    }
    PostConfigure();
}


void RdmaConnectedSessionBase::PostConfigure() {
    if(direction == Direction::Receive && autoQueueRx) {
        std::vector<uint64_t> bufferLengths(transferBuffers->size());
        for(size_t i = 0; i < transferBuffers->size(); ++i) {
            RdmaBuffer* buffer = transferBuffers->WaitForIdleBuffer(0);
            bufferLengths[i] = buffer->GetBufferLen();
            QueueRecvBuffer(buffer, false /* sendCreditUpdate */);
        }
        size_t creditsLeft = transferBuffers->size();
        uint64_t* bufferLengthsPtr = bufferLengths.data();
        while (creditsLeft) {
            size_t creditsToSend = std::min(creditsLeft, kMaxCreditsPerBuffer);
            SendCreditUpdate(bufferLengthsPtr, creditsToSend);
            creditsLeft -= creditsToSend;
            bufferLengthsPtr += creditsToSend;
        }
    }
}


void RdmaConnectedSessionBase::QueueBuffer(RdmaBuffer* buffer) {
    if(!connected) {
        RDMA_THROW(easyrdma_Error_Disconnected);
    }
    if(direction == Direction::Receive) {
        QueueRecvBuffer(buffer, true /* sendCreditUpdate */);
    }
    else {
        QueueSendBuffer(buffer);
    }
}


void RdmaConnectedSessionBase::QueueRecvBuffer(RdmaBuffer* buffer, bool sendCreditUpdate) {
    assert(direction == Direction::Receive);

    transferBuffers->QueueBuffer(buffer, RdmaBufferQueue::IgnoreCredits::No);

    if (sendCreditUpdate) {
        uint64_t bufferLen = buffer->GetBufferLen();
        SendCreditUpdate(&bufferLen, 1 /* numBuffers */);
    }
}


void RdmaConnectedSessionBase::SendCreditUpdate(uint64_t* bufferLengths, size_t numBuffers) {
    assert(numBuffers <= kMaxCreditsPerBuffer);
    RdmaBuffer* creditBuffer = creditBuffers->WaitForIdleBuffer(-1);
    creditBuffer->SetUsed(numBuffers * sizeof(boost::endian::big_uint64_buf_t));
    boost::endian::big_uint64_buf_t* dest = reinterpret_cast<boost::endian::big_uint64_buf_t*>(creditBuffer->GetBuffer());
    for(size_t i = 0; i < numBuffers; ++i) {
        dest[i] = bufferLengths[i];
    }
    creditBuffers->QueueBuffer(creditBuffer, RdmaBufferQueue::IgnoreCredits::Yes);
}


void RdmaConnectedSessionBase::QueueSendBuffer(RdmaBuffer* buffer) {
    assert(direction == Direction::Send);
    transferBuffers->QueueBuffer(buffer, RdmaBufferQueue::IgnoreCredits::No);
}


RdmaBufferRegion* RdmaConnectedSessionBase::AcquireSendRegion(int32_t timeoutMs) {
    if(direction == Direction::Receive && autoQueueRx) {
        RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable
    }
    if(bufferOwnership == BufferOwnership::External) {
        RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable
    }

    BufferWaitAccessSuspender accessSuspender(this, bufferWaitInProgress);
    RdmaBuffer* buffer = transferBuffers->WaitForIdleBuffer(timeoutMs);
    return buffer;
}


void RdmaConnectedSessionBase::QueueBufferRegion(RdmaBufferRegion* region, const BufferCompletionCallbackData& callbackData) {
    if(bufferOwnership == BufferOwnership::External) {
        RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable
    }
    RdmaBuffer* buffer = static_cast<RdmaBuffer*>(region);
    buffer->SetCompletionCallback(callbackData);
    buffer->Requeue();
}


RdmaBufferRegion* RdmaConnectedSessionBase::AcquireReceivedRegion(int32_t timeoutMs) {
    BufferWaitAccessSuspender accessSuspender(this, bufferWaitInProgress);
    RdmaBuffer* buffer = transferBuffers->WaitForCompletedBuffer(timeoutMs);
    return buffer;
}


void RdmaConnectedSessionBase::QueueExternalBufferRegion(void* pointerWithinBuffer, size_t size, const BufferCompletionCallbackData& callbackData, int32_t timeoutMs) {
    BufferWaitAccessSuspender accessSuspender(this, bufferWaitInProgress);
    RdmaBuffer* buffer = transferBuffers->WaitForIdleBuffer(timeoutMs);
    if(bufferType != BufferType::Single || bufferOwnership != BufferOwnership::External) {
        RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable
    }
    RdmaBufferExternal* externalBuffer = static_cast<RdmaBufferExternal*>(buffer);
    externalBuffer->SetBufferRegion(pointerWithinBuffer, size);
    externalBuffer->SetCompletionCallback(callbackData);
    externalBuffer->Requeue();
}


PropertyData RdmaConnectedSessionBase::GetProperty(uint32_t propertyId) {
    switch(propertyId) {
        case easyrdma_Property_QueuedBuffers:
        case easyrdma_Property_UserBuffers:
            if(transferBuffers) {
                return transferBuffers->GetProperty(propertyId);
            }
            else {
                RDMA_THROW(easyrdma_Error_SessionNotConfigured);
            }
            break;
        case easyrdma_Property_Connected:
            return PropertyData(connected);
        case easyrdma_Property_UseRxPolling:
            return PropertyData(usePolling);
        default:
            RDMA_THROW(easyrdma_Error_InvalidProperty);
    };
}


void RdmaConnectedSessionBase::SetProperty(uint32_t propertyId, const void* value, size_t valueSize) {
    switch(propertyId) {
        case easyrdma_Property_ConnectionData:
            connectionData = std::vector<uint8_t>(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + valueSize);
            break;
        case easyrdma_Property_UseRxPolling: {
            if(valueSize != sizeof(bool)) {
                RDMA_THROW(easyrdma_Error_InvalidArgument);
            }
            bool _usePolling = *reinterpret_cast<const bool*>(value);
            // Setting it to true only supported if:
            // Linux, Receive direction, connected, not yet configured
            if(!connected || transferBuffers) {
                RDMA_THROW(easyrdma_Error_AlreadyConfigured);
            }
            #ifdef _WIN32
                if(_usePolling) {
                    RDMA_THROW(easyrdma_Error_OperationNotSupported);
                }
            #endif
            if(_usePolling && (direction != Direction::Receive)) {
                RDMA_THROW(easyrdma_Error_OperationNotSupported);
            }
            usePolling = _usePolling;
            break;
        }
        default:
            RDMA_THROW(easyrdma_Error_ReadOnlyProperty);
    }
}


bool RdmaConnectedSessionBase::CheckDeferredDestructionConditionsMet() {
    if(transferBuffers) {
        return !transferBuffers->HasUserBuffersOutstanding();
    }
    return true;
}


void RdmaConnectedSessionBase::CheckQueueStatus() {
    if(transferBuffers) {
        RdmaError queueStatus = transferBuffers->GetQueueStatus();
        if(queueStatus.IsError()) {
            throw RdmaException(queueStatus);
        }
    }
}
