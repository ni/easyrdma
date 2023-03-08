// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaConnector.h"
#include "RdmaConnectionData.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <assert.h>
#include "api/tAccessSuspender.h"

using namespace EasyRDMA;

RdmaConnector::RdmaConnector(const RdmaAddress& _localAddress) :
    RdmaConnectedSession(), everConnected(false), connectInProgress(false)
{
    OverlappedWrapper overlapped;
    HandleHR(NdOpenAdapter(IID_IND2Adapter,
        _localAddress,
        _localAddress.GetSize(),
        adapter));
    // Get the file handle for overlapped operations on this adapter.
    HandleHR(adapter->CreateOverlappedFile(&adapterFile));

    HandleHR(adapter->CreateConnector(
        IID_IND2Connector,
        adapterFile,
        connector));

    HandleHR(connector->Bind(
        reinterpret_cast<const sockaddr*>(&_localAddress),
        sizeof(_localAddress)));
}

RdmaConnector::~RdmaConnector()
{
    // Do not close file handle
}

void RdmaConnector::Connect(Direction _direction, const RdmaAddress& remoteAddress, int32_t timeoutMs)
{
    if (everConnected) {
        RDMA_THROW(easyrdma_Error_AlreadyConnected);
    }
    if (connectInProgress) {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    }
    connectInProgress = true;
    try {
        PreConnect(_direction);
        {
            tAccessSuspender accessSuspender(this);
            OverlappedWrapper overlapped;
            HandleHROverlappedWithTimeout(connector->Connect(
                                              qp,
                                              remoteAddress,
                                              static_cast<ULONG>(remoteAddress.GetSize()),
                                              0,
                                              0,
                                              connectionData.data(),
                                              static_cast<ULONG>(connectionData.size()),
                                              overlapped),
                connector,
                overlapped,
                timeoutMs);
            AcquireAndValidateConnectionData(connector, _direction);
            HandleHROverlapped(connector->CompleteConnect(overlapped), connector, overlapped);
        }
        PostConnect();
        everConnected = true;
        connectInProgress = false;
    } catch (std::exception&) {
        connectInProgress = false;
        Cancel();
        DestroyQP();
        throw;
    }
}

void RdmaConnector::Cancel()
{
    if (!everConnected) {
        connector->CancelOverlappedRequests();
        try {
            OverlappedWrapper overlapped;
            HandleHROverlapped(connector->Disconnect(overlapped), connector, overlapped);
        } catch (const RdmaException&) {
        }
    }
    RdmaConnectedSession::Cancel();
}
