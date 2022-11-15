// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaCommon.h"
#include "RdmaConnectionData.h"

namespace EasyRDMA {
    const std::vector<uint8_t> CreateDefaultConnectionData(Direction direction) {
        easyrdma_ConnectionData cd = kDefaultConnectionData;
        cd.direction = static_cast<uint8_t>(direction);
        const uint8_t* startBuffer = reinterpret_cast<const uint8_t*>(&cd.protocolId);
        std::vector<uint8_t> connectionData;
        std::copy(startBuffer, startBuffer + sizeof(kDefaultConnectionData), std::back_inserter(connectionData));
        return std::move(connectionData);
    }

    void ValidateConnectionData(const std::vector<uint8_t>& buffer, Direction myDirection) {
        if(buffer.size() < sizeof(easyrdma_ConnectionData)) {
            RDMA_THROW(easyrdma_Error_IncompatibleProtocol);
        }

        const easyrdma_ConnectionData& otherData = reinterpret_cast<const easyrdma_ConnectionData&>(*buffer.data());

        if(otherData.protocolId != kDefaultConnectionData.protocolId) {
            RDMA_THROW(easyrdma_Error_IncompatibleProtocol);
        }
        if(otherData.oldestCompatibleVersion > kDefaultConnectionData.oldestCompatibleVersion) {
            RDMA_THROW(easyrdma_Error_IncompatibleVersion);
        }
        ASSERT_ALWAYS(myDirection != Direction::Unknown);
        Direction otherDirection = (myDirection == Direction::Receive) ? Direction::Send : Direction::Receive;
        if(otherData.direction != static_cast<uint8_t>(otherDirection)) {
            RDMA_THROW(easyrdma_Error_InvalidDirection);
        }
    }
}; //EasyRDMA