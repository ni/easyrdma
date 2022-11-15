// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaSession.h"
#include <boost/endian/arithmetic.hpp>

namespace EasyRDMA {

    #pragma pack(push, 1)
    struct easyrdma_ConnectionData {
        boost::endian::big_uint32_t protocolId;
        uint8_t protocolVersion;
        uint8_t oldestCompatibleVersion;
        uint8_t direction;
    };
    #pragma pack(pop)

    static const uint32_t kConnectionDataProtocol = 0x52444D41; // 'RDMA'

    static const easyrdma_ConnectionData kDefaultConnectionData = {
        kConnectionDataProtocol,
        1, /* protocolVersion */
        1, /* oldestCompatibleVersion */
        static_cast<uint8_t>(Direction::Unknown)
    };

    const std::vector<uint8_t> CreateDefaultConnectionData(Direction direction);
    void ValidateConnectionData(const std::vector<uint8_t>& buffer, Direction myDirection);

}; //EasyRDMA
