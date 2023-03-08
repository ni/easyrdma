// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <string>

class RdmaError;

namespace EasyRDMA
{

std::string GetErrorDescription(const RdmaError& status);
std::string ConvertToErrorString(int32_t errorCode);

}; // namespace EasyRDMA
