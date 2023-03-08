// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaAddress.h"
#include <vector>

class RdmaEnumeration
{
public:
    struct RdmaInterface
    {
        std::string address;
    };
    static std::vector<RdmaInterface> EnumerateInterfaces(int32_t filterAddressFamily);
};