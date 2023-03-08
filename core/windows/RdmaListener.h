// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaSession.h"
#include "RdmaConnectedSession.h"
#include "RdmaListenerBase.h"

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
    AutoRef<IND2Adapter> adapter;
    AutoRef<IND2Listener> listen;
    HANDLE adapterFile = nullptr;
    bool acceptInProgress;
};
