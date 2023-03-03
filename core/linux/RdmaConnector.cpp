// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaConnector.h"
#include "RdmaCommon.h"
#include "RdmaConnectionData.h"
#include "EventManager.h"
#include <iostream>
#include "api/tAccessSuspender.h"

using namespace EasyRDMA;

RdmaConnector::RdmaConnector(const RdmaAddress& _localAddress) :
    everConnected(false), connectInProgress(false)
{
    HandleError(rdma_create_id(GetEventChannel(), &cm_id, &GetEventManager(), RDMA_PS_TCP));
    GetEventManager().CreateConnectionQueue(cm_id);
    HandleError(rdma_bind_addr(cm_id, RdmaAddress(_localAddress)));
    localAddress = RdmaAddress(rdma_get_local_addr(cm_id));
}

RdmaConnector::~RdmaConnector()
{
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

        tAccessSuspender accessSuspender(this);
        RdmaAddress destAddress(remoteAddress);
        HandleError(rdma_resolve_addr(cm_id, (localAddress.GetProtocol() != AF_UNSPEC) ? static_cast<sockaddr*>(localAddress) : nullptr, destAddress, timeoutMs));
        auto event = GetEventManager().WaitForEvent(cm_id, -1); // Rely on timeout passed to rdma_resolve_addr
        if (event.eventType != RDMA_CM_EVENT_ADDR_RESOLVED) {
            RDMA_THROW_WITH_SUBCODE(easyrdma_Error_UnableToConnect, event.eventType);
        }

        // Wait for route resolved
        HandleError(rdma_resolve_route(cm_id, timeoutMs));
        event = GetEventManager().WaitForEvent(cm_id, -1); // Rely on timeout passed to rdma_resolve_route
        if (event.eventType != RDMA_CM_EVENT_ROUTE_RESOLVED) {
            RDMA_THROW_WITH_SUBCODE(easyrdma_Error_UnableToConnect, event.eventType);
        }

        // Connect
        rdma_conn_param connectParams = {};
        connectParams.private_data = connectionData.data();
        connectParams.private_data_len = connectionData.size();
        connectParams.retry_count = 10;
        connectParams.rnr_retry_count = 10;
        HandleError(rdma_connect(cm_id, &connectParams));
        event = GetEventManager().WaitForEvent(cm_id, timeoutMs);
        if (event.eventType != RDMA_CM_EVENT_ESTABLISHED) {
            RDMA_THROW_WITH_SUBCODE(easyrdma_Error_UnableToConnect, event.eventType);
        }
        ValidateConnectionData(event.connectionData, _direction);
        PostConnect();
        everConnected = true;
        connectInProgress = false;
    } catch (std::exception&) {
        Cancel();
        DestroyQP();
        connectInProgress = false;
        throw;
    }
}

void RdmaConnector::Cancel()
{
    if (cm_id) {
        GetEventManager().AbortWaits(cm_id);
    }
    RdmaConnectedSession::Cancel();
}
