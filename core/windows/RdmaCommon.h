// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <ndsupport.h>
#include <ndstatus.h>
#include "RdmaError.h"
#include <chrono>
#include <algorithm>
#include <ws2ipdef.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <functional>
#include "api/easyrdma.h"
#include "RdmaErrorTranslation.h"
#include <assert.h>

#define THROW_HRESULT_ERROR(osError)                                                                 \
    {                                                                                                \
        RdmaError s(RdmaErrorTranslation::OSErrorToRdmaError(osError), osError, __FILE__, __LINE__); \
        throw RdmaException(s);                                                                      \
    }

#define THROW_WIN32_ERROR(osError)                                                                   \
    {                                                                                                \
        RdmaError s(RdmaErrorTranslation::OSErrorToRdmaError(osError), osError, __FILE__, __LINE__); \
        throw RdmaException(s);                                                                      \
    }

#define HandleHR(expr)                    \
    {                                     \
        HRESULT hrLocal = (expr);         \
        if (FAILED(hrLocal)) {            \
            THROW_HRESULT_ERROR(hrLocal); \
        }                                 \
    }

#define HandleHROverlapped(expr, obj, overlap)                 \
    {                                                          \
        HRESULT hrLocal = (expr);                              \
        if (hrLocal == ND_PENDING) {                           \
            hrLocal = obj->GetOverlappedResult(overlap, TRUE); \
        }                                                      \
        if (FAILED(hrLocal)) {                                 \
            THROW_HRESULT_ERROR(hrLocal);                      \
        }                                                      \
    }

inline void HandleHROverlappedWithTimeoutInternal(HRESULT hr, IND2Overlapped* obj, OVERLAPPED* overlap, int32_t timeoutMs, const char* filename, int32_t lineNumber)
{
    HRESULT hrLocal = hr;
    RdmaError status;
    if (hrLocal == ND_PENDING) {
        if (timeoutMs != -1) {
            hrLocal = WaitForSingleObjectEx(static_cast<OVERLAPPED*>(overlap)->hEvent, timeoutMs, TRUE);
            if (hrLocal != WAIT_OBJECT_0) {
                status.Assign(easyrdma_Error_Timeout, 0, filename, lineNumber);
                throw RdmaException(status);
            }
        }
        hrLocal = obj->GetOverlappedResult(overlap, TRUE);
    }
    if (FAILED(hrLocal)) {
        status.Assign(RdmaErrorTranslation::OSErrorToRdmaError(hrLocal), hrLocal, filename, lineNumber);
        throw RdmaException(status);
    }
}

#define HandleHROverlappedWithTimeout(expr, obj, overlap, timeoutMs) \
    HRESULT hr = (expr);                                             \
    HandleHROverlappedWithTimeoutInternal(hr, obj, overlap, timeoutMs, __FILE__, __LINE__)

#ifdef _DEBUG
#define ASSERT_ALWAYS_INTERNAL(statement, statement_str) assert(statement)
#else
#define ASSERT_ALWAYS_INTERNAL(statement, statement_str)                                                                       \
    {                                                                                                                          \
        if (!(statement)) {                                                                                                    \
            std::cerr << "Fatal error:" << statement_str << " failed in " << __FILE__ << " at line " << __LINE__ << std::endl; \
            RDMA_THROW(easyrdma_Error_InternalError);                                                                          \
        }                                                                                                                      \
    }
#endif

#define ASSERT_ALWAYS(statement) ASSERT_ALWAYS_INTERNAL(statement, #statement)

class OverlappedWrapper
{
public:
    OverlappedWrapper()
    {
        ov.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (ov.hEvent == nullptr) {
            THROW_WIN32_ERROR(GetLastError());
        }
    }
    ~OverlappedWrapper()
    {
        if (ov.hEvent) {
            CloseHandle(ov.hEvent);
            ov.hEvent = nullptr;
        }
    }
    operator OVERLAPPED*()
    {
        return &ov;
    }
    OVERLAPPED ov = {};
};

template <typename T>
class AutoRef
{
public:
    AutoRef()
    {
    }
    AutoRef(T* handle) :
        _handle(handle)
    {
        _handle->AddRef();
    }
    AutoRef(AutoRef<T>&& other)
    {
        _handle = other._handle;
        other._handle = 0;
    };

    T* operator->()
    {
        return _handle;
    }
    T* get() const
    {
        return _handle;
    }
    operator IUnknown*()
    {
        return _handle;
    }
    operator T*()
    {
        return _handle;
    }
    ~AutoRef()
    {
        reset();
    }
    void reset()
    {
        if (_handle) {
            _handle->Release();
            _handle = nullptr;
        }
    }
    operator void**()
    {
        return reinterpret_cast<void**>(&_handle);
    }

protected:
    T* _handle = nullptr;
};

#define RDMA_TIMEOUT_INFINITE ((int32_t)(-1))

//////////////////////////////////////////////////////////////////////////////
//
//  TimeoutCalculator
//
//  Description:
//      Manages a cumulative timeout for operations split into multiple waits
//
//////////////////////////////////////////////////////////////////////////////
class TimeoutCalculator
{
public:
    TimeoutCalculator(int32_t timeoutMs) :
        _timeoutMs(timeoutMs),
        _start(std::chrono::high_resolution_clock::now())
    {
    }
    bool timedOut() const
    {
        return ((_timeoutMs != RDMA_TIMEOUT_INFINITE) && (getRemainingMs() == 0));
    }
    int32_t getRemainingMs() const
    {
        if (_timeoutMs == RDMA_TIMEOUT_INFINITE) {
            return _timeoutMs;
        } else {
            auto elapsed = std::chrono::high_resolution_clock::now() - _start;
            return static_cast<int32_t>(std::max(INT64_C(0), _timeoutMs - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
        }
    }

private:
    int32_t _timeoutMs;
    std::chrono::high_resolution_clock::time_point _start;
};

inline void* AllocateAlignedMemory(size_t size, size_t alignment)
{
    void* allocatedBuffer = _aligned_malloc(size, alignment);
    if (!allocatedBuffer) {
        RDMA_THROW(easyrdma_Error_OutOfMemory);
    }
    return allocatedBuffer;
}

inline void FreeAlignedMemory(void* ptr)
{
    _aligned_free(ptr);
}
