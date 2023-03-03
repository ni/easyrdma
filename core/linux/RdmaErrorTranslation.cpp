// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaErrorTranslation.h"
#include <errno.h>
#include <algorithm>
#include <map>

static const std::map<int, int32_t> errorTranslations =
    {
        {EINVAL, easyrdma_Error_InvalidArgument},
        {ETIMEDOUT, easyrdma_Error_Timeout},
        {EINVAL, easyrdma_Error_InvalidArgument},
        {ENOMEM, easyrdma_Error_OutOfMemory},
        {ECANCELED, easyrdma_Error_OperationCancelled},
        {ECONNREFUSED, easyrdma_Error_UnableToConnect},
        {ECONNABORTED, easyrdma_Error_Disconnected},
        {ENETUNREACH, easyrdma_Error_UnableToConnect},
        {EADDRNOTAVAIL, easyrdma_Error_InvalidAddress},
        {EADDRINUSE, easyrdma_Error_AddressInUse},
};

static const std::map<ibv_wc_status, int32_t> ibvErrorTranslations =
    {
        {IBV_WC_LOC_LEN_ERR, easyrdma_Error_InvalidSize},
        {IBV_WC_LOC_QP_OP_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_LOC_EEC_OP_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_LOC_PROT_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_WR_FLUSH_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_MW_BIND_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_BAD_RESP_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_LOC_ACCESS_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_REM_INV_REQ_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_REM_ACCESS_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_REM_OP_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_RETRY_EXC_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_RNR_RETRY_EXC_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_LOC_RDD_VIOL_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_REM_INV_RD_REQ_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_REM_ABORT_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_INV_EECN_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_INV_EEC_STATE_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_FATAL_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_RESP_TIMEOUT_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_GENERAL_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_TM_ERR, easyrdma_Error_OperatingSystemError},
        {IBV_WC_TM_RNDV_INCOMPLETE, easyrdma_Error_OperatingSystemError},
};

int RdmaErrorTranslation::OSErrorToRdmaError(int osError)
{
    auto it = errorTranslations.find(osError);
    return (it != errorTranslations.end()) ? it->second : easyrdma_Error_OperatingSystemError;
}

int RdmaErrorTranslation::IBVErrorToRdmaError(ibv_wc_status ibvError)
{
    auto it = ibvErrorTranslations.find(ibvError);
    return (it != ibvErrorTranslations.end()) ? it->second : easyrdma_Error_OperatingSystemError;
}
