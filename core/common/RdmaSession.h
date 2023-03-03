// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaAddress.h"
#include <memory>
#include <functional>
#include <boost/thread/shared_mutex.hpp>
#include "api/iAccessManaged.h"
#include "api/tAccessManager.h"

enum class Direction : uint32_t
{
    Unknown = 0xFF,
    Send = 0x00,
    Receive = 0x01
};

class RdmaBufferRegion
{
public:
    virtual void* GetPointer() const = 0;
    virtual size_t GetSize() const = 0;
    virtual void SetUsed(size_t size) = 0;
    virtual size_t GetUsed() const = 0;
    virtual void Requeue() = 0;
    virtual void Release() = 0;
};

typedef std::function<void(void*, void*, int32_t, size_t)> BufferCompletionCallback;

struct BufferCompletionCallbackData
{
    BufferCompletionCallback callbackFunction = nullptr;
    void* context1 = nullptr;
    void* context2 = nullptr;
    bool IsSet()
    {
        return callbackFunction != nullptr;
    };
    void Call(int32_t status, size_t completedBytes)
    {
        if (IsSet())
            callbackFunction(context1, context2, status, completedBytes);
    };
};

struct PropertyData
{
    PropertyData(){};
    template <typename T>
    PropertyData(const T& value)
    {
        data.resize(sizeof(value));
        memcpy(data.data(), &value, sizeof(value));
    }
    void CopyToOutput(void* outputBuf, size_t* size)
    {
        if (outputBuf) {
            if (*size < data.size()) {
                RDMA_THROW(easyrdma_Error_InvalidSize);
            }
            *size = data.size();
            std::copy(data.data(), data.data() + *size, reinterpret_cast<uint8_t*>(outputBuf));
        } else {
            *size = data.size();
        }
    }
    std::vector<uint8_t> data;
};

class RdmaSession : public iAccessManaged
{
public:
    virtual ~RdmaSession(){};

    virtual void Connect(Direction direction, const RdmaAddress& remoteAddress, int32_t timeoutMs = -1)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };
    virtual std::shared_ptr<RdmaSession> Accept(Direction direction, int32_t timeoutMs)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    virtual bool IsConnected() const
    {
        return false;
    };

    virtual void Cancel(){};

    virtual PropertyData GetProperty(uint32_t propertyId)
    {
        RDMA_THROW(easyrdma_Error_InvalidProperty);
    };
    virtual void SetProperty(uint32_t propertyId, const void* value, const size_t valueSize)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    virtual RdmaAddress GetLocalAddress()
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };
    virtual RdmaAddress GetRemoteAddress()
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    // Configures an internally-allocated memory buffer
    virtual void ConfigureBuffers(size_t maxTransactionSize, size_t maxConcurrentTransactions)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    // Configures to use an externally-allocated memory buffer
    virtual void ConfigureExternalBuffer(void* externalBuffer, size_t bufferSize, size_t maxConcurrentTransactions)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    //-----------------------------------------------
    // Below are used for internally-managed buffers
    // For LV, the buffer regions are returned as EDVRs
    //-----------------------------------------------
    // Used for Send and Recv with an internally-allocated buffer to retrieve a region of the buffer.
    // For send it is expected to be filled by the user before queueing.
    virtual RdmaBufferRegion* AcquireSendRegion(int32_t timeoutMs)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    // Used for Send and Recv to queue an idle buffer
    virtual void QueueBufferRegion(RdmaBufferRegion* region, const BufferCompletionCallbackData& callbackData)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    // Used for Recv to wait for a previously queued buffer to complete
    virtual RdmaBufferRegion* AcquireReceivedRegion(int32_t timeoutMs)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    //-----------------------------------------------
    // Below are used for externally-managed buffers
    //-----------------------------------------------
    // Used for Send and Recv with an externally-managed buffer to queue a region of that buffer for send or recv
    // For LV, this is just an EDVR coming from an API like RIO, and the callbacks are handled internally via the EDVR
    virtual void QueueExternalBufferRegion(void* pointerWithinBuffer, size_t size, const BufferCompletionCallbackData& callbackData, int32_t timeoutMs)
    {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    };

    virtual bool CheckDeferredDestructionConditionsMet()
    {
        return true;
    }

    // iAccessManaged
    tAccessManager& GetAccessManager() override
    {
        return accessManager;
    }

protected:
    tAccessManager accessManager;
};
