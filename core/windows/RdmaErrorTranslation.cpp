// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaErrorTranslation.h"
#include <algorithm>
#include <map>
#include "RdmaCommon.h"

static const std::map<int, int32_t> errorTranslations =
{
    { ND_SUCCESS,                   easyrdma_Error_Success            },
    { ND_TIMEOUT,                   easyrdma_Error_Timeout            },
    { ND_INVALID_PARAMETER,         easyrdma_Error_InvalidArgument    },
    { ND_NO_MEMORY,                 easyrdma_Error_OutOfMemory        },
    { ND_INVALID_PARAMETER_MIX,     easyrdma_Error_InvalidArgument    },
    { ND_IO_TIMEOUT,                easyrdma_Error_Timeout            },
    { ND_INTERNAL_ERROR,            easyrdma_Error_InternalError      },
    { ND_INVALID_PARAMETER_1,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_2,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_3,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_4,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_5,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_6,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_7,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_8,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_9,       easyrdma_Error_InvalidArgument    },
    { ND_INVALID_PARAMETER_10,      easyrdma_Error_InvalidArgument    },
    { ND_CANCELED,                  easyrdma_Error_OperationCancelled },
    { ND_INVALID_ADDRESS,           easyrdma_Error_InvalidAddress     },
    { ND_TOO_MANY_ADDRESSES,        easyrdma_Error_InvalidAddress     },
    { ND_ADDRESS_ALREADY_EXISTS,    easyrdma_Error_InvalidAddress     },
    { ND_CONNECTION_REFUSED,        easyrdma_Error_ConnectionRefused  },
    { ND_CONNECTION_INVALID,        easyrdma_Error_NotConnected       },
    { ND_CONNECTION_ABORTED,        easyrdma_Error_Disconnected       },
    { ND_NETWORK_UNREACHABLE,       easyrdma_Error_UnableToConnect    },
    { ND_CONNECTION_ACTIVE,         easyrdma_Error_AlreadyConnected   },
    { ND_SHARING_VIOLATION,         easyrdma_Error_AddressInUse       },
    { (int)(0xC00000CC),            easyrdma_Error_InvalidAddress     }, // STATUS_BAD_NETWORK_NAME from DDK
};

int RdmaErrorTranslation::OSErrorToRdmaError(int osError) {
    auto it = errorTranslations.find(osError);
    return (it != errorTranslations.end()) ? it->second : easyrdma_Error_OperatingSystemError;
}
