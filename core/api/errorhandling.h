// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

//============================================================================
//  Includes
//============================================================================
#include "common/RdmaError.h"

namespace EasyRDMA
{

void ClearLastRdmaError();
void PopulateLastRdmaError(const RdmaError& status);
void GetLastRdmaError(RdmaError& status);
int64_t DebugGetNumberOfAllocatedLastRdmaErrors();

}; // namespace EasyRDMA