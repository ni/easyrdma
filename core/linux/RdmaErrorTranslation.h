// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaCommon.h"

namespace RdmaErrorTranslation
{
int OSErrorToRdmaError(int osError);
int IBVErrorToRdmaError(ibv_wc_status ibvError);
} // namespace RdmaErrorTranslation
