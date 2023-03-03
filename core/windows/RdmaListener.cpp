// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaListener.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <assert.h>
#include "api/tAccessSuspender.h"

RdmaListener::RdmaListener(const RdmaAddress& localAddress) :
    acceptInProgress(false)
{
    OverlappedWrapper overlapped;

    HandleHR(NdOpenAdapter(IID_IND2Adapter,
        reinterpret_cast<const struct sockaddr*>(&localAddress.address),
        localAddress.GetSize(),
        adapter));
    // Get the file handle for overlapped operations on this adapter.
    HandleHR(adapter->CreateOverlappedFile(&adapterFile));

    HandleHR(adapter->CreateListener(
        IID_IND2Listener,
        adapterFile,
        listen));
    HandleHR(listen->Bind(
        reinterpret_cast<const sockaddr*>(&localAddress),
        sizeof(localAddress)));

    HandleHR(listen->Listen(0));
}

RdmaListener::~RdmaListener()
{
    // Do not close file handle
}

std::shared_ptr<RdmaSession> RdmaListener::Accept(Direction direction, int32_t timeoutMs)
{
    if (acceptInProgress) {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    }
    acceptInProgress = true;
    OverlappedWrapper overlapped;
    AutoRef<IND2Connector> connector;
    HandleHR(adapter->CreateConnector(
        IID_IND2Connector,
        adapterFile,
        connector));
    tAccessSuspender accessSuspender(this);
    std::shared_ptr<RdmaSession> acceptedSession;
    try {
        HandleHROverlappedWithTimeout(listen->GetConnectionRequest(connector, overlapped), connector, overlapped, timeoutMs);
        acceptedSession = std::make_shared<RdmaConnectedSession>(direction, adapter, adapterFile, connector, connectionData, timeoutMs);
        acceptInProgress = false;
        return acceptedSession;
    } catch (std::exception&) {
        // If our own timeout occurs in GetConnectionRequest(), we need to cancel it
        listen->CancelOverlappedRequests();
        acceptInProgress = false;
        throw;
    }
}

RdmaAddress RdmaListener::GetLocalAddress()
{
    RdmaAddress address;
    ULONG addrSize = static_cast<ULONG>(sizeof(address.address));
    HandleHR(listen->GetLocalAddress(reinterpret_cast<sockaddr*>(&address.address), &addrSize));
    return address;
}

RdmaAddress RdmaListener::GetRemoteAddress()
{
    RdmaAddress address;
    return address;
}

void RdmaListener::Cancel()
{
    listen->CancelOverlappedRequests();
}