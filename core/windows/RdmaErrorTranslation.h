// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

namespace RdmaErrorTranslation {
    int OSErrorToRdmaError(int osError);
}