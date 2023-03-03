// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "easyrdma.h"
#include "tests/utility/Utility.h"

class Enumeration
{
public:
    static std::vector<std::string> EnumerateInterfaces(int32_t filterAddressFamily = 0)
    {
        size_t numAddresses = 0;
        RDMA_THROW_IF_FATAL(easyrdma_Enumerate(nullptr, &numAddresses, filterAddressFamily));
        std::vector<std::string> interfaces;
        if (numAddresses != 0) {
            std::vector<easyrdma_AddressString> addresses(numAddresses);
            RDMA_THROW_IF_FATAL(easyrdma_Enumerate(&addresses[0], &numAddresses, filterAddressFamily));
            for (auto addr : addresses) {
                interfaces.push_back(&addr.addressString[0]);
            }
        }
        return std::move(interfaces);
    }
};
