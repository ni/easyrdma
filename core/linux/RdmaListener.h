// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaSession.h"
#include "RdmaConnectedSession.h"
#include "RdmaListenerBase.h"
#include <map>
#include <mutex>
#include <condition_variable>
#include "RdmaCommon.h"

class RdmaListener : public RdmaListenerBase
{
public:
    RdmaListener(const RdmaAddress& localAddress);
    virtual ~RdmaListener();

    std::shared_ptr<RdmaSession> Accept(Direction direction, int32_t timeoutMs) override;
    RdmaAddress GetLocalAddress() override;
    RdmaAddress GetRemoteAddress() override;
    void Cancel() override;

private:
    rdma_cm_id* cm_id;
    RdmaAddress localAddress;
    bool acceptInProgress;
};
