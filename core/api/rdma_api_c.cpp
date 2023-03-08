// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaError.h"
#include "RdmaEnumeration.h"
#include "api/errorElaboration.h"
#include "RdmaSession.h"
#include "RdmaConnector.h"
#include "RdmaListener.h"
#include "api/rdma_api_common.h"
#include "easyrdma.h"

#include "api/errorhandling.h"

using namespace EasyRDMA;

//============================================================================
//  API_CATCH_EXCEPTION - This macro should be used within the exported API functions
//      for our library (the API functions that a user application would call
//      directly).  This macro handles catching the correct types of exceptions
//      and converting them to a status code that can be returned from the
//      API function.
//============================================================================
#define API_CATCH_EXCEPTION(status)                                         \
    catch (const RdmaException& e)                                          \
    {                                                                       \
        status.Assign(e.rdmaError);                                         \
    }                                                                       \
    catch (std::bad_alloc&)                                                 \
    {                                                                       \
        status.Assign(easyrdma_Error_OutOfMemory, 0, __FILE__, __LINE__);   \
    }                                                                       \
    catch (std::exception&)                                                 \
    {                                                                       \
        status.Assign(easyrdma_Error_InternalError, 0, __FILE__, __LINE__); \
    }

static void UpdateLastError(const RdmaError& status)
{
    PopulateLastRdmaError(status);
}

int32_t _RDMA_FUNC easyrdma_Enumerate(easyrdma_AddressString addresses[], size_t* numAddresses, int32_t filterAddressFamily)
{
    RdmaError status;
    try {
        if (!numAddresses) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        GlobalInitializeIfNeeded();
        auto interfaces = RdmaEnumeration::EnumerateInterfaces(filterAddressFamily);
        if (!addresses) {
            *numAddresses = interfaces.size();
        } else {
            *numAddresses = std::min(interfaces.size(), *numAddresses);
            for (size_t i = 0; i < *numAddresses; i++) {
                strncpy(addresses[i].addressString, interfaces[i].address.c_str(), sizeof(addresses[i].addressString) - 1);
            }
        }
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_CreateConnectorSession(const char* localAddress, uint16_t localPort, easyrdma_Session* session)
{
    RdmaError status;
    try {
        if (!session) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        *session = 0;
        GlobalInitializeIfNeeded();
        RdmaSessionRef connectorSession(std::make_shared<RdmaConnector>(RdmaAddress(localAddress ? localAddress : "", localPort)));
        *session = sessionManager.RegisterSession(connectorSession);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_CreateListenerSession(const char* localAddress, uint16_t localPort, easyrdma_Session* session)
{
    RdmaError status;
    try {
        if (!session) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        *session = 0;
        GlobalInitializeIfNeeded();
        RdmaSessionRef listenerSession(std::make_shared<RdmaListener>(RdmaAddress(localAddress ? localAddress : "", localPort)));
        *session = sessionManager.RegisterSession(listenerSession);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_CloseSession(easyrdma_Session session, uint32_t flags)
{
    RdmaError status;
    if (session != 0) {
        try {
            sessionManager.DestroySession(session, flags);
        }
        API_CATCH_EXCEPTION(status);
    }
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_AbortSession(easyrdma_Session session)
{
    RdmaError status;
    try {
        RdmaSessionRef sessionRef = sessionManager.GetSession(session);
        sessionRef->Cancel();
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_Connect(easyrdma_Session connectorSession, uint32_t direction, const char* remoteAddress, uint16_t remotePort, int32_t timeoutMs)
{
    RdmaError status;
    try {
        if (!remoteAddress) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        RdmaSessionRef connectorSessionRef = sessionManager.GetSession(connectorSession);
        connectorSessionRef->Connect(static_cast<Direction>(direction), RdmaAddress(remoteAddress, remotePort), timeoutMs);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_Accept(easyrdma_Session listenSession, uint32_t direction, int32_t timeoutMs, easyrdma_Session* connectedSession)
{
    RdmaError status;
    try {
        if (!connectedSession) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto listenSessionRef = sessionManager.GetSession(listenSession);
        auto connectedSessionRef = RdmaSessionRef(listenSessionRef->Accept(static_cast<Direction>(direction), timeoutMs));
        *connectedSession = sessionManager.RegisterSession(connectedSessionRef);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_GetLocalAddress(easyrdma_Session session, easyrdma_AddressString* localAddress, uint16_t* localPort)
{
    RdmaError status;
    try {
        auto sessionRef = sessionManager.GetSession(session);
        RdmaAddress address = sessionRef->GetLocalAddress();
        if (localAddress) {
            std::string addrString = address.GetAddrString();
            strncpy(localAddress->addressString, addrString.c_str(), sizeof(localAddress->addressString) - 1);
        }
        if (localPort) {
            *localPort = address.GetPort();
        }
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_GetRemoteAddress(easyrdma_Session session, easyrdma_AddressString* remoteAddress, uint16_t* remotePort)
{
    RdmaError status;
    try {
        auto sessionRef = sessionManager.GetSession(session);
        RdmaAddress address = sessionRef->GetRemoteAddress();
        if (remoteAddress) {
            std::string addrString = address.GetAddrString();
            strncpy(remoteAddress->addressString, addrString.c_str(), sizeof(remoteAddress->addressString) - 1);
        }
        if (remotePort) {
            *remotePort = address.GetPort();
        }
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_ConfigureBuffers(easyrdma_Session session, size_t maxTransactionSize, size_t maxConcurrentTransactions)
{
    RdmaError status;
    try {
        auto sessionRef = sessionManager.GetSession(session);
        sessionRef->ConfigureBuffers(maxTransactionSize, maxConcurrentTransactions);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_ConfigureExternalBuffer(easyrdma_Session session, void* externalBuffer, size_t bufferSize, size_t maxConcurrentTransactions)
{
    RdmaError status;
    try {
        auto sessionRef = sessionManager.GetSession(session);
        sessionRef->ConfigureExternalBuffer(externalBuffer, bufferSize, maxConcurrentTransactions);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_AcquireSendRegion(easyrdma_Session session, int32_t timeoutMs, easyrdma_InternalBufferRegion* bufferRegion)
{
    RdmaError status;
    try {
        if (!bufferRegion) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto sessionRef = sessionManager.GetSession(session);
        RdmaBufferRegion* internalRegion = sessionRef->AcquireSendRegion(timeoutMs);
        bufferRegion->buffer = internalRegion->GetPointer();
        bufferRegion->bufferSize = internalRegion->GetSize();
        bufferRegion->usedSize = internalRegion->GetSize();
        bufferRegion->Internal.internalReference1 = reinterpret_cast<void*>(session);
        bufferRegion->Internal.internalReference2 = internalRegion;
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_AcquireReceivedRegion(easyrdma_Session session, int32_t timeoutMs, easyrdma_InternalBufferRegion* bufferRegion)
{
    static_assert(sizeof(easyrdma_InternalBufferRegion) == 64, "Expected fixed size of easyrdma_InternalBufferRegion");
    RdmaError status;
    try {
        if (!bufferRegion) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto sessionRef = sessionManager.GetSession(session);
        RdmaBufferRegion* internalRegion = sessionRef->AcquireReceivedRegion(timeoutMs);
        bufferRegion->buffer = internalRegion->GetPointer();
        bufferRegion->bufferSize = internalRegion->GetSize();
        bufferRegion->usedSize = internalRegion->GetUsed();
        bufferRegion->Internal.internalReference1 = reinterpret_cast<void*>(session);
        bufferRegion->Internal.internalReference2 = internalRegion;
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_QueueBufferRegion(easyrdma_Session session, easyrdma_InternalBufferRegion* bufferRegion, easyrdma_BufferCompletionCallbackData* callback)
{
    RdmaError status;
    try {
        if (!bufferRegion || !bufferRegion->Internal.internalReference1 || !bufferRegion->Internal.internalReference2) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto sessionRef = sessionManager.GetSession(session);
        BufferCompletionCallbackData callbackData = {};
        if (callback && callback->callbackFunction) {
            callbackData.callbackFunction = callback->callbackFunction;
            callbackData.context1 = callback->context1;
            callbackData.context2 = callback->context2;
        }
        auto rdmaBufferRegion = reinterpret_cast<RdmaBufferRegion*>(bufferRegion->Internal.internalReference2);
        rdmaBufferRegion->SetUsed(bufferRegion->usedSize);
        sessionRef->QueueBufferRegion(rdmaBufferRegion, callbackData);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_ReleaseReceivedBufferRegion(easyrdma_Session session, easyrdma_InternalBufferRegion* bufferRegion)
{
    RdmaError status;
    try {
        if (!bufferRegion || !bufferRegion->Internal.internalReference1 || !bufferRegion->Internal.internalReference2) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        if (reinterpret_cast<easyrdma_Session>(bufferRegion->Internal.internalReference1) != session) {
            // Session ids should match
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto sessionRef = sessionManager.GetSession(session);
        try {
            reinterpret_cast<RdmaBufferRegion*>(bufferRegion->Internal.internalReference2)->Requeue();
        } catch (const RdmaException& e) {
            // If the error is a disconnection error, we can ignore it but instead release the buffer back to idle. This makes
            // client code that does a waitforbuffer-process-release sequence work correctly even if the connection fails. They
            // will instead get an error on the first waitforbuffer that blocks.
            if (e.rdmaError.GetCode() == easyrdma_Error_Disconnected) {
                reinterpret_cast<RdmaBufferRegion*>(bufferRegion->Internal.internalReference2)->Release();
            } else {
                throw;
            }
        }
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_ReleaseUserBufferRegionToIdle(easyrdma_Session session, easyrdma_InternalBufferRegion* bufferRegion)
{
    RdmaError status;
    try {
        if (!bufferRegion || !bufferRegion->Internal.internalReference1 || !bufferRegion->Internal.internalReference2) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        if (reinterpret_cast<easyrdma_Session>(bufferRegion->Internal.internalReference1) != session) {
            // Session ids should match
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto sessionRef = sessionManager.GetSession(session, kAccess_Exclusive, tCheckDeferredCloseTable::Yes);
        reinterpret_cast<RdmaBufferRegion*>(bufferRegion->Internal.internalReference2)->Release();
        if (sessionRef.IsDestructionPending()) {
            sessionManager.CheckDeferredSessionDestructionReady(sessionRef, session);
        }
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_QueueExternalBufferRegion(easyrdma_Session session, void* pointerWithinBuffer, size_t size, easyrdma_BufferCompletionCallbackData* callback, int32_t timeoutMs)
{
    RdmaError status;
    try {
        if (!pointerWithinBuffer) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        auto sessionRef = sessionManager.GetSession(session);
        BufferCompletionCallbackData callbackData = {};
        if (callback && callback->callbackFunction) {
            callbackData.callbackFunction = callback->callbackFunction;
            callbackData.context1 = callback->context1;
            callbackData.context2 = callback->context2;
        }
        sessionRef->QueueExternalBufferRegion(pointerWithinBuffer, size, callbackData, timeoutMs);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_GetLastError(easyrdma_ErrorInfo* rdmaErrorStatus)
{
    RdmaError status;
    GetLastRdmaError(status);
    if (rdmaErrorStatus) {
        rdmaErrorStatus->errorCode = status.errorCode;
        rdmaErrorStatus->errorSubCode = status.errorSubCode;
        rdmaErrorStatus->filename = status.filename;
        rdmaErrorStatus->fileLineNumber = status.fileLineNumber;
        return easyrdma_Error_Success;
    }
    return easyrdma_Error_InvalidArgument;
}

int32_t _RDMA_FUNC easyrdma_GetLastErrorString(char* buffer, size_t bufferSize)
{
    if (!buffer) {
        return easyrdma_Error_InvalidArgument;
    }
    RdmaError status;
    GetLastRdmaError(status);
    std::string err_str = GetErrorDescription(status);

    if (bufferSize > err_str.size() + 1) {
        strncpy(buffer, err_str.c_str(), bufferSize);
        return easyrdma_Error_Success;
    } else {
        return easyrdma_Error_InvalidSize;
    }
}

int32_t _RDMA_FUNC easyrdma_GetProperty(easyrdma_Session session, uint32_t propertyId, void* value, size_t* valueSize)
{
    RdmaError status;
    try {
        if (!valueSize) {
            RDMA_THROW(easyrdma_Error_InvalidArgument);
        }

        PropertyData output;
        switch (propertyId) {
            // Error on any write-only attributes
            case easyrdma_Property_ConnectionData:
                RDMA_THROW(easyrdma_Error_WriteOnlyProperty);
            case easyrdma_Property_NumOpenedSessions:
                output = PropertyData(sessionManager.GetOpenedSessions());
                break;
            case easyrdma_Property_NumPendingDestructionSessions:
                output = PropertyData(sessionManager.GetDeferredCloseSessions());
                break;
            default: {
                auto sessionRef = sessionManager.GetSession(session);
                output = sessionRef->GetProperty(propertyId);
                break;
            }
        }
        output.CopyToOutput(value, valueSize);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

int32_t _RDMA_FUNC easyrdma_SetProperty(easyrdma_Session session, uint32_t propertyId, const void* value, size_t valueSize)
{
    RdmaError status;
    try {
        sessionManager.GetSession(session)->SetProperty(propertyId, value, valueSize);
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
    return status.GetCode();
}

void _RDMA_FUNC easyrdma_testsetLastOsError(int osErrorCode)
{
    RdmaError status;
    try {
#ifdef _WIN32
        THROW_HRESULT_ERROR(osErrorCode);
#else
        THROW_OS_ERROR(osErrorCode);
#endif
    }
    API_CATCH_EXCEPTION(status);
    UpdateLastError(status);
}