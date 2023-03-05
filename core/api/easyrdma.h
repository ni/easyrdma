/*
 * EasyRDMA
 *
 * Cross-platform RDMA streaming library
 *
 * Copyright (C) 2022 National Instruments (NI)
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef _easyrdma_h_
#define _easyrdma_h_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(_WIN32)
#define C_CONV __cdecl
#if defined(_BUILDING_EASYRDMA)
#define _IMPORT_EXPORT __declspec(dllexport)
#else
#define _IMPORT_EXPORT __declspec(dllimport)
#endif
#else
#define C_CONV
#if defined(_BUILDING_EASYRDMA)
#define _IMPORT_EXPORT __attribute__((section(".export")))
#else
#define _IMPORT_EXPORT
#endif
#endif
#define _RDMA_FUNC _IMPORT_EXPORT C_CONV

#pragma pack(push, 8)

// EasyRDMA Error codes
#define easyrdma_Error_Success                      0
#define easyrdma_Error_Timeout                      -734001 // 0xFFF4CCCF: Operation timed out.
#define easyrdma_Error_InvalidSession               -734002 // 0xFFF4CCCE: The specified session could not be found.
#define easyrdma_Error_InvalidArgument              -734003 // 0xFFF4CCCD: Invalid argument.
#define easyrdma_Error_InvalidOperation             -734004 // 0xFFF4CCCC: Invalid operation.
#define easyrdma_Error_NoBuffersQueued              -734005 // 0xFFF4CCCB: No buffers queued.
#define easyrdma_Error_OperatingSystemError         -734006 // 0xFFF4CCCA: Operating system error.
#define easyrdma_Error_InvalidSize                  -734007 // 0xFFF4CCC9: The provided size was invalid.
#define easyrdma_Error_OutOfMemory                  -734008 // 0xFFF4CCC8: Out of memory.
#define easyrdma_Error_InternalError                -734009 // 0xFFF4CCC7: An internal error occurred. Contact National Instruments for support.
#define easyrdma_Error_InvalidAddress               -734010 // 0xFFF4CCC6: Invalid address.
#define easyrdma_Error_OperationCancelled           -734011 // 0xFFF4CCC5: Operation cancelled.
#define easyrdma_Error_InvalidProperty              -734012 // 0xFFF4CCC4: Invalid property.
#define easyrdma_Error_SessionNotConfigured         -734013 // 0xFFF4CCC3: Session not configured.
#define easyrdma_Error_NotConnected                 -734014 // 0xFFF4CCC2: Not connected.
#define easyrdma_Error_UnableToConnect              -734015 // 0xFFF4CCC1: Unable to connect.
#define easyrdma_Error_AlreadyConfigured            -734016 // 0xFFF4CCC0: Already configured.
#define easyrdma_Error_Disconnected                 -734017 // 0xFFF4CCBF: Disconnected.
#define easyrdma_Error_BufferWaitInProgress         -734018 // 0xFFF4CCBE: Blocking buffer operation already in progress.
#define easyrdma_Error_AlreadyConnected             -734019 // 0xFFF4CCBD: Current session is already connected.
#define easyrdma_Error_InvalidDirection             -734020 // 0xFFF4CCBC: Specified direction is invalid.
#define easyrdma_Error_IncompatibleProtocol         -734021 // 0xFFF4CCBB: Incompatible protocol.
#define easyrdma_Error_IncompatibleVersion          -734022 // 0xFFF4CCBA: Incompatible version.
#define easyrdma_Error_ConnectionRefused            -734023 // 0xFFF4CCB9: Connection refused.
#define easyrdma_Error_ReadOnlyProperty             -734024 // 0xFFF4CCB8: Writing a read-only property is not permitted.
#define easyrdma_Error_WriteOnlyProperty            -734025 // 0xFFF4CCB7: Reading a write-only property is not permitted.
#define easyrdma_Error_OperationNotSupported        -734026 // 0xFFF4CCB6: The current operation is not supported.
#define easyrdma_Error_AddressInUse                 -734027 // 0xFFF4CCB5: The requested address is already in use.
#define easyrdma_Error_SendTooLargeForRecvBuffer    -734028 // 0xFFF4CCB4: The Send buffer is too large.

// Direction used in Connect/Accept
#define easyrdma_Direction_Send      0x00
#define easyrdma_Direction_Receive   0x01

// Enumeration address type filter
#define easyrdma_AddressFamily_AF_UNSPEC  0x00 // Enumerate any address family
#define easyrdma_AddressFamily_AF_INET    0x04 // Enumerate only IPv4 interfaces
#define easyrdma_AddressFamily_AF_INET6   0x06 // Enumerate only IPv6 interfaces

// Properties
#define easyrdma_Property_QueuedBuffers   0x100     // uint64_t
#define easyrdma_Property_Connected       0x101     // uint8_t/bool
#define easyrdma_Property_UserBuffers     0x102     // uint64_t
#define easyrdma_Property_UseRxPolling    0x103     // uint8_t/bool

// Internal-use-only properties (for testing -- do not use)
#define easyrdma_Property_NumOpenedSessions                0x200     // uint64_t
#define easyrdma_Property_NumPendingDestructionSessions    0x201     // uint64_t
#define easyrdma_Property_ConnectionData                   0x202     // binary blob

// Flags
#define easyrdma_CloseFlags_DeferWhileUserBuffersOutstanding   0x01

// Structures
struct easyrdma_AddressString
{
    char addressString[64];
};

struct easyrdma_InternalBufferRegion
{
    union
    {
        struct
        {
            struct
            {
                void* buffer; // Pointer to internally-allocated buffer
                size_t bufferSize; // Size of internally-allocated buffer
                size_t usedSize; // Size actually filled (set by API on receive, can be overridden by caller on send)
            };
            struct
            {
                void* internalReference1; // Used internally by the API
                void* internalReference2; // Used internally by the API
            } Internal;
        };
        char padding[64]; // Ensure struct is large enough for future additions
    };
};

struct easyrdma_ErrorInfo
{
    int errorCode;
    int errorSubCode;
    const char* filename;
    int fileLineNumber;
};

// Callback function for buffer completion
typedef void(C_CONV* easyrdma_BufferCompletionCallback)(void* context1, void* context2, int32_t completionStatus, size_t completedBytes);

struct easyrdma_BufferCompletionCallbackData
{
    easyrdma_BufferCompletionCallback callbackFunction = nullptr;
    void* context1 = nullptr;
    void* context2 = nullptr;
};

typedef struct easyrdma_Session_struct* easyrdma_Session;
#define easyrdma_InvalidSession nullptr

int32_t _RDMA_FUNC easyrdma_Enumerate(easyrdma_AddressString addresses[], size_t* numAddresses, int32_t filterAddressFamily = easyrdma_AddressFamily_AF_UNSPEC);
int32_t _RDMA_FUNC easyrdma_CreateConnectorSession(const char* localAddress, uint16_t localPort, easyrdma_Session* session);
int32_t _RDMA_FUNC easyrdma_CreateListenerSession(const char* localAddress, uint16_t localPort, easyrdma_Session* session);
int32_t _RDMA_FUNC easyrdma_AbortSession(easyrdma_Session session);
int32_t _RDMA_FUNC easyrdma_CloseSession(easyrdma_Session session, uint32_t flags = 0);
int32_t _RDMA_FUNC easyrdma_Connect(easyrdma_Session connectorSession, uint32_t direction, const char* remoteAddress, uint16_t remotePort, int32_t timeoutMs);
int32_t _RDMA_FUNC easyrdma_Accept(easyrdma_Session listenSession, uint32_t direction, int32_t timeoutMs, easyrdma_Session* connectedSession);
int32_t _RDMA_FUNC easyrdma_GetLocalAddress(easyrdma_Session session, easyrdma_AddressString* localAddress, uint16_t* localPort);
int32_t _RDMA_FUNC easyrdma_GetRemoteAddress(easyrdma_Session session, easyrdma_AddressString* remoteAddress, uint16_t* remotePort);
int32_t _RDMA_FUNC easyrdma_ConfigureBuffers(easyrdma_Session session, size_t maxTransactionSize, size_t maxConcurrentTransactions);
int32_t _RDMA_FUNC easyrdma_ConfigureExternalBuffer(easyrdma_Session session, void* externalBuffer, size_t bufferSize, size_t maxConcurrentTransactions);
int32_t _RDMA_FUNC easyrdma_AcquireSendRegion(easyrdma_Session session, int32_t timeoutMs, easyrdma_InternalBufferRegion* bufferRegion);
int32_t _RDMA_FUNC easyrdma_AcquireReceivedRegion(easyrdma_Session session, int32_t timeoutMs, easyrdma_InternalBufferRegion* bufferRegion);
int32_t _RDMA_FUNC easyrdma_QueueBufferRegion(easyrdma_Session session, easyrdma_InternalBufferRegion* bufferRegion, easyrdma_BufferCompletionCallbackData* callback);
int32_t _RDMA_FUNC easyrdma_QueueExternalBufferRegion(easyrdma_Session session, void* pointerWithinBuffer, size_t size, easyrdma_BufferCompletionCallbackData* callbackData, int32_t timeoutMs);
int32_t _RDMA_FUNC easyrdma_ReleaseReceivedBufferRegion(easyrdma_Session session, easyrdma_InternalBufferRegion* bufferRegion);
int32_t _RDMA_FUNC easyrdma_GetProperty(easyrdma_Session session, uint32_t propertyId, void* value, size_t* valueSize);
int32_t _RDMA_FUNC easyrdma_SetProperty(easyrdma_Session session, uint32_t propertyId, const void* value, size_t valueSize);
int32_t _RDMA_FUNC easyrdma_GetLastErrorString(char* buffer, size_t bufferSize);
int32_t _RDMA_FUNC easyrdma_ReleaseUserBufferRegionToIdle(easyrdma_Session session, easyrdma_InternalBufferRegion* bufferRegion);
int32_t _RDMA_FUNC easyrdma_GetLastError(easyrdma_ErrorInfo* status);

// Internal-use-only functions (for testing -- do not use)
void _RDMA_FUNC easyrdma_testsetLastOsError(int osErrorCode);

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif //_easyrdma_h_