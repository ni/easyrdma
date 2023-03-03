// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "errorElaboration.h"
#include "RdmaError.h"
#include <sstream>
#include "api/easyrdma.h"
#include <map>

static const std::map<int32_t, std::string> errorStringTranslations =
    {
        {easyrdma_Error_Success, ""},
        {easyrdma_Error_Timeout, "Operation timed out."},
        {easyrdma_Error_InvalidSession, "The specified session could not be found."},
        {easyrdma_Error_InvalidArgument, "Invalid argument."},
        {easyrdma_Error_InvalidOperation, "Invalid operation."},
        {easyrdma_Error_NoBuffersQueued, "No buffers queued."},
        {easyrdma_Error_OperatingSystemError, "Operating system error."},
        {easyrdma_Error_InvalidSize, "The provided size was invalid."},
        {easyrdma_Error_OutOfMemory, "Out of memory."},
        {easyrdma_Error_InternalError, "An internal error occurred. Contact National Instruments for support."},
        {easyrdma_Error_InvalidAddress, "Invalid address."},
        {easyrdma_Error_OperationCancelled, "Operation cancelled."},
        {easyrdma_Error_InvalidProperty, "Invalid property."},
        {easyrdma_Error_SessionNotConfigured, "Session not configured."},
        {easyrdma_Error_NotConnected, "Not connected."},
        {easyrdma_Error_UnableToConnect, "Unable to connect."},
        {easyrdma_Error_AlreadyConfigured, "Already configured."},
        {easyrdma_Error_Disconnected, "Disconnected."},
        {easyrdma_Error_BufferWaitInProgress, "Blocking buffer operation already in progress."},
        {easyrdma_Error_AlreadyConnected, "Current session is already connected."},
        {easyrdma_Error_InvalidDirection, "Specified direction is invalid."},
        {easyrdma_Error_IncompatibleProtocol, "Incompatible protocol."},
        {easyrdma_Error_IncompatibleVersion, "Incompatible version."},
        {easyrdma_Error_ConnectionRefused, "Connection refused."},
        {easyrdma_Error_ReadOnlyProperty, "Writing a read-only property is not permitted."},
        {easyrdma_Error_WriteOnlyProperty, "Reading a write-only property is not permitted."},
        {easyrdma_Error_OperationNotSupported, "The current operation is not supported."},
        {easyrdma_Error_AddressInUse, "The requested address is already in use."},
        {easyrdma_Error_SendTooLargeForRecvBuffer, "The Send buffer is too large."},
};

namespace EasyRDMA
{

//////////////////////////////////////////////////////////////////////////////
//
//  GetErrorDescription
//
//  Description:
//      Returns elaborated error description string.
//
//////////////////////////////////////////////////////////////////////////////
std::string GetErrorDescription(const RdmaError& status)
{
    if (status.IsError()) {
        std::string errorDesc = ConvertToErrorString(status.GetCode());
        if (status.GetCode() == easyrdma_Error_Success) {
            return errorDesc;
        }
        if (status.errorSubCode != 0) {
            errorDesc += "\nSubcode: " + std::to_string(status.errorSubCode);
        }
        errorDesc += "\nLocation: " + std::string(status.filename ? status.filename : "Unknown") + ":" + std::to_string(status.fileLineNumber) + "\n";
        return errorDesc;
    }
    return "";
}

std::string ConvertToErrorString(int32_t statusCode)
{
    auto it = errorStringTranslations.find(statusCode);
    return (it != errorStringTranslations.end()) ? it->second : "Unknown error ";
}

}; // namespace EasyRDMA
