// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaListener.h"
#include "RdmaCommon.h"
#include "EventManager.h"
#include "api/tAccessSuspender.h"

RdmaListener::RdmaListener(const RdmaAddress& _localAddress) : cm_id(nullptr), acceptInProgress(false) {
    HandleError(rdma_create_id(GetEventChannel(), &cm_id, &GetEventManager(), RDMA_PS_TCP));
    GetEventManager().CreateConnectionQueue(cm_id);
    HandleError(rdma_bind_addr(cm_id, RdmaAddress(_localAddress)));
    HandleError(rdma_listen(cm_id, -1));
    localAddress = RdmaAddress(rdma_get_local_addr(cm_id));
}

RdmaListener::~RdmaListener() {
    if(cm_id) {
        GetEventManager().DestroyConnectionQueue(cm_id);
        rdma_destroy_id(cm_id);
    }
}

std::shared_ptr<RdmaSession> RdmaListener::Accept(Direction direction, int32_t timeoutMs) {
    if(acceptInProgress) {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    }
    acceptInProgress = true;
    try {
        tAccessSuspender accessSuspender(this);
        auto connectRequestEvent = GetEventManager().WaitForEvent(cm_id, timeoutMs);
        if(connectRequestEvent.eventType != RDMA_CM_EVENT_CONNECT_REQUEST) {
            RDMA_THROW(easyrdma_Error_UnableToConnect);
        }
        std::shared_ptr<RdmaSession> connectedSession = std::make_shared<RdmaConnectedSession>(direction, connectRequestEvent.incomingConnectionId, connectRequestEvent.connectionData, connectionData);
        acceptInProgress = false;
        return connectedSession;
    }
    catch(std::exception&)  {
        acceptInProgress = false;
        throw;
    }
}

RdmaAddress RdmaListener::GetLocalAddress() {
    return localAddress;
}

RdmaAddress RdmaListener::GetRemoteAddress() {
    return RdmaAddress();
}

void RdmaListener::Cancel() {
    GetEventManager().AbortWaits(cm_id);
}
