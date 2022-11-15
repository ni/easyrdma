// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaSession.h"

class RdmaListenerBase : public RdmaSession {
    public:
        RdmaListenerBase();
        virtual ~RdmaListenerBase();

        virtual void SetProperty(uint32_t propertyId, const void* value, size_t valueSize) override;
    protected:
        std::vector<uint8_t> connectionData;
};
