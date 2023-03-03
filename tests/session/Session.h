// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "easyrdma.h"
#include "tests/utility/Utility.h"

class BufferCompletion
{
public:
    struct CompletionResult
    {
        int32_t status = 0;
        size_t completedBytes = 0;
        void* context = nullptr;
    };

    BufferCompletion()
    {
        completionFuture = completionPromise.get_future();
    }
    static void Signal(int32_t status, size_t completedBytes, void* bufferCompletionContext, void* userContext)
    {
        try {
            CompletionResult result;
            result.status = status;
            result.context = userContext;
            result.completedBytes = completedBytes;
            reinterpret_cast<BufferCompletion*>(bufferCompletionContext)->completionPromise.set_value(result);
        } catch (std::exception&) {
            // Cannot throw from within the callback
            assert(0);
        }
    }
    void WaitForCompletion(int32_t timeoutMs)
    {
        auto result = completionFuture.wait_for(std::chrono::milliseconds(timeoutMs));
        if (result == std::future_status::timeout) {
            RDMA_THROW_TEST_ERROR(-1);
        }
        auto completionResult = completionFuture.get();
        if (completionResult.status != 0) {
            RDMA_THROW_TEST_ERROR(completionResult.status);
        }
    }
    void* GetContext()
    {
        return completionFuture.get().context;
    };
    bool IsCompleted()
    {
        return completionFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };
    size_t GetCompletedBytes()
    {
        return completionFuture.get().completedBytes;
    };

protected:
    std::promise<CompletionResult> completionPromise;
    std::shared_future<CompletionResult> completionFuture;
};

class BufferRegion : public easyrdma_InternalBufferRegion
{
public:
    BufferRegion(const easyrdma_InternalBufferRegion& _region) :
        easyrdma_InternalBufferRegion(_region)
    {
    }
    BufferRegion() :
        easyrdma_InternalBufferRegion({})
    {
    }
    std::vector<uint8_t> ToVector() const
    {
        std::vector<uint8_t> returnedBuffer;
        if (usedSize > bufferSize) {
            RDMA_THROW_TEST_ERROR(-1);
        }
        returnedBuffer.insert(returnedBuffer.begin(), reinterpret_cast<uint8_t*>(buffer), reinterpret_cast<uint8_t*>(buffer) + usedSize);
        return std::move(returnedBuffer);
    }
    void CopyFromVector(const std::vector<uint8_t>& data)
    {
        if (size() < data.size()) {
            RDMA_THROW_TEST_ERROR(-1);
        }
        memcpy(buffer, data.data(), data.size());
        usedSize = data.size();
    }
    size_t size()
    {
        return usedSize;
    }
};

class Session
{
public:
    Session() :
        session(easyrdma_InvalidSession)
    {
    }
    Session(easyrdma_Session _session) :
        session(_session)
    {
    }
    Session(Session&& other)
    {
        session = other.session;
        other.session = easyrdma_InvalidSession;
    };
    ~Session()
    {
        if (session) {
            RDMA_EXPECT_NO_THROW(Close());
        }
    }
    Session& operator=(Session&& other)
    {
        if (this != &other) {
            session = other.session;
            other.session = easyrdma_InvalidSession;
        }
        return *this;
    }
    static Session CreateListener(const std::string& localAddress, uint16_t localPort = 0)
    {
        easyrdma_Session listenSession = easyrdma_InvalidSession;
        RDMA_THROW_IF_FATAL(easyrdma_CreateListenerSession(localAddress.c_str(), localPort, &listenSession));
        return Session(listenSession);
    }
    static Session CreateConnector(const std::string& localAddress, uint16_t localPort = 0)
    {
        easyrdma_Session connectorSession = easyrdma_InvalidSession;
        RDMA_THROW_IF_FATAL(easyrdma_CreateConnectorSession(localAddress.c_str(), localPort, &connectorSession));
        return std::move(Session(connectorSession));
    }
    void Close(uint32_t flags = 0)
    {
        RDMA_THROW_IF_FATAL(easyrdma_CloseSession(session, flags));
        session = easyrdma_InvalidSession;
    }
    void Abort()
    {
        RDMA_THROW_IF_FATAL(easyrdma_AbortSession(session));
    }
    Session Accept(uint32_t direction, int32_t timeoutMs = 5000)
    {
        easyrdma_Session connectedSession = easyrdma_InvalidSession;
        RDMA_THROW_IF_FATAL(easyrdma_Accept(session, direction, timeoutMs, &connectedSession));
        return std::move(Session(connectedSession));
    }
    void Connect(uint32_t direction, const std::string& remoteAddress, uint16_t remotePort, int32_t timeoutMs = 5000)
    {
        RDMA_THROW_IF_FATAL(easyrdma_Connect(session, direction, remoteAddress.c_str(), remotePort, timeoutMs));
    }

    void ConfigureBuffers(size_t maxTransactionSize, size_t maxConcurrentTransactions)
    {
        RDMA_THROW_IF_FATAL(easyrdma_ConfigureBuffers(session, maxTransactionSize, maxConcurrentTransactions));
    }
    void ConfigureExternalBuffer(void* externalBuffer, size_t bufferSize, size_t maxConcurrentTransactions)
    {
        RDMA_THROW_IF_FATAL(easyrdma_ConfigureExternalBuffer(session, externalBuffer, bufferSize, maxConcurrentTransactions));
    }

    BufferRegion GetSendRegion(int32_t timeoutMs = 5000)
    {
        BufferRegion bufferRegion;
        RDMA_THROW_IF_FATAL(easyrdma_AcquireSendRegion(session, timeoutMs, &bufferRegion));
        return std::move(bufferRegion);
    }

    BufferRegion GetReceivedRegion(int32_t timeoutMs = 5000)
    {
        BufferRegion bufferRegion = {};
        RDMA_THROW_IF_FATAL(easyrdma_AcquireReceivedRegion(session, timeoutMs, &bufferRegion));
        return std::move(bufferRegion);
    }

    void QueueRegionWithCallback(BufferRegion& bufferRegion, BufferCompletion* completionCallback, void* context = nullptr)
    {
        ASSERT_TRUE(!completionCallback || !completionCallback->IsCompleted());
        easyrdma_BufferCompletionCallbackData callbackData;
        auto CallbackFunc = [](void* _context1, void* _context2, int32_t _status, size_t _completedBytes) {
            BufferCompletion::Signal(_status, _completedBytes, _context1, _context2);
        };
        callbackData.callbackFunction = CallbackFunc;
        callbackData.context1 = completionCallback;
        callbackData.context2 = context;
        RDMA_THROW_IF_FATAL(easyrdma_QueueBufferRegion(session, &bufferRegion, completionCallback ? &callbackData : nullptr));
    }

    void QueueRegion(BufferRegion& bufferRegion)
    {
        QueueRegionWithCallback(bufferRegion, nullptr, nullptr);
    }

    void ReleaseReceivedRegion(BufferRegion& bufferRegion)
    {
        RDMA_THROW_IF_FATAL(easyrdma_ReleaseReceivedBufferRegion(session, &bufferRegion));
    }

    static void ReleaseUserRegionToIdle(easyrdma_Session sessionHandle, BufferRegion& bufferRegion)
    {
        RDMA_THROW_IF_FATAL(easyrdma_ReleaseUserBufferRegionToIdle(sessionHandle, &bufferRegion));
    }

    std::vector<uint8_t> Receive(int32_t timeoutMs = 5000)
    {
        auto region = GetReceivedRegion(timeoutMs);
        std::vector<uint8_t> returnedBuffer = region.ToVector();
        ReleaseReceivedRegion(region);
        return std::move(returnedBuffer);
    }

    void SendWithCallback(const std::vector<uint8_t>& buffer, BufferCompletion* completionCallback, void* context = nullptr, int32_t timeoutMs = 5000)
    {
        auto bufferRegion = GetSendRegion(timeoutMs);
        bufferRegion.CopyFromVector(buffer);
        QueueRegionWithCallback(bufferRegion, completionCallback, context);
    }

    void SendBlankData(size_t size, int32_t timeoutMs = 5000)
    {
        auto bufferRegion = GetSendRegion(timeoutMs);
        if (bufferRegion.bufferSize < size) {
            RDMA_THROW_TEST_ERROR(-1);
        }
        QueueRegion(bufferRegion);
    }

    size_t ReceiveBlankData(int32_t timeoutMs = 5000)
    {
        auto region = GetReceivedRegion(timeoutMs);
        size_t size = region.size();
        ReleaseReceivedRegion(region);
        return size;
    }

    void Send(const std::vector<uint8_t>& buffer, int32_t timeoutMs = 5000)
    {
        SendWithCallback(buffer, nullptr, nullptr, timeoutMs);
    }

    void QueueExternalBufferWithCallback(void* buffer, size_t bufferLength, BufferCompletion* completionCallback, void* context = nullptr, int32_t timeoutMs = 5000)
    {
        auto CallbackFunc = [](void* _context1, void* _context2, int32_t _status, size_t _completedBytes) {
            BufferCompletion::Signal(_status, _completedBytes, _context1, _context2);
        };
        easyrdma_BufferCompletionCallbackData callbackData;
        callbackData.callbackFunction = CallbackFunc;
        callbackData.context1 = completionCallback;
        callbackData.context2 = context;
        RDMA_THROW_IF_FATAL(easyrdma_QueueExternalBufferRegion(session, buffer, bufferLength, completionCallback ? &callbackData : nullptr, timeoutMs));
    }

    void QueueExternalBuffer(void* buffer, size_t bufferLength, int32_t timeoutMs = 5000)
    {
        QueueExternalBufferWithCallback(buffer, bufferLength, nullptr, nullptr, timeoutMs);
    }

    void GetProperty(uint32_t property, void* value, size_t* valueSize)
    {
        RDMA_THROW_IF_FATAL(easyrdma_GetProperty(session, property, value, valueSize));
    }

    template <typename T>
    static T GetPropertyOnSession(easyrdma_Session session, uint32_t property)
    {
        T value = 0;
        size_t valueSize = sizeof(value);
        RDMA_THROW_IF_FATAL(easyrdma_GetProperty(session, property, &value, &valueSize));
        return value;
    }

    uint64_t GetPropertyU64(uint32_t property)
    {
        return GetPropertyOnSession<uint64_t>(session, property);
    }

    bool GetPropertyBool(uint32_t property)
    {
        return GetPropertyOnSession<bool>(session, property);
    }

    void SetProperty(uint32_t propertyId, void* value, size_t valueSize)
    {
        RDMA_THROW_IF_FATAL(easyrdma_SetProperty(session, propertyId, value, valueSize));
    }

    template <typename T>
    static void SetPropertyOnSession(easyrdma_Session session, uint32_t property, const T& value)
    {
        size_t valueSize = sizeof(value);
        RDMA_THROW_IF_FATAL(easyrdma_SetProperty(session, property, &value, valueSize));
    }

    void SetPropertyBool(uint32_t propertyId, bool value)
    {
        SetPropertyOnSession(session, propertyId, value);
    }

    std::string GetLocalAddress()
    {
        easyrdma_AddressString address = {};
        RDMA_THROW_IF_FATAL(easyrdma_GetLocalAddress(session, &address, nullptr));
        return address.addressString;
    }

    std::string GetRemoteAddress()
    {
        easyrdma_AddressString address = {};
        RDMA_THROW_IF_FATAL(easyrdma_GetRemoteAddress(session, &address, nullptr));
        return address.addressString;
    }

    uint16_t GetLocalPort()
    {
        uint16_t port = 0;
        RDMA_THROW_IF_FATAL(easyrdma_GetLocalAddress(session, nullptr, &port));
        return port;
    }

    uint16_t GetRemotePort()
    {
        uint16_t port = 0;
        RDMA_THROW_IF_FATAL(easyrdma_GetRemoteAddress(session, nullptr, &port));
        return port;
    }

    easyrdma_Session GetSessionHandle() const
    {
        return session;
    }

protected:
    easyrdma_Session session;
};
