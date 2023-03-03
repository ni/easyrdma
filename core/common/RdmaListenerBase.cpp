// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaListenerBase.h"

RdmaListenerBase::RdmaListenerBase()
{
}

RdmaListenerBase::~RdmaListenerBase()
{
}

void RdmaListenerBase::SetProperty(uint32_t propertyId, const void* value, size_t valueSize)
{
    switch (propertyId) {
        case easyrdma_Property_ConnectionData:
            connectionData = std::vector<uint8_t>(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + valueSize);
            break;
        default:
            RDMA_THROW(easyrdma_Error_ReadOnlyProperty);
    }
}
