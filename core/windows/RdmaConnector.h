// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

#include "RdmaSession.h"
#include "RdmaConnectedSession.h"
#include <mutex>

class RdmaConnector : public RdmaConnectedSession
{
public:
    RdmaConnector(const RdmaAddress& _localAddress);
    virtual ~RdmaConnector();
    void Connect(Direction direction, const RdmaAddress& remoteAddress, int32_t timeoutMs = -1) override;
    void Cancel() override;

private:
    bool everConnected;
    bool connectInProgress;
};
