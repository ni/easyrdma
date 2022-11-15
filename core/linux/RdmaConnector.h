// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaSession.h"
#include "RdmaConnectedSession.h"
#include <thread>
#include <mutex>
#include <rdma/rdma_cma.h>
#include <atomic>

class RdmaConnector : public RdmaConnectedSession {
    public:
        RdmaConnector(const RdmaAddress& _localAddress);
        virtual ~RdmaConnector();
        void Connect(Direction direction, const RdmaAddress& remoteAddress, int32_t timeoutMs = -1) override;
        void Cancel() override;
    private:
        bool everConnected;
        bool connectInProgress;
};
