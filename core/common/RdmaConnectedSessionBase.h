// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaSession.h"
#include <boost/thread.hpp>
#include <queue>
#include <mutex>

class RdmaBufferQueue;
class RdmaBuffer;
class RdmaMemoryRegion;

class RdmaConnectedSessionBase : public RdmaSession
{
public:
    RdmaConnectedSessionBase();
    RdmaConnectedSessionBase(const std::vector<uint8_t>& _connectionData);
    virtual ~RdmaConnectedSessionBase();

    void ConfigureBuffers(size_t maxTransactionSize, size_t maxConcurrentTransactions) override;
    void ConfigureExternalBuffer(void* externalBuffer, size_t bufferSize, size_t maxConcurrentTransactions) override;
    RdmaBufferRegion* AcquireSendRegion(int32_t timeoutMs) override;
    void QueueBufferRegion(RdmaBufferRegion* region, const BufferCompletionCallbackData& callbackData) override;
    void QueueExternalBufferRegion(void* pointerWithinBuffer, size_t size, const BufferCompletionCallbackData& callbackData, int32_t timeoutMs) override;
    RdmaBufferRegion* AcquireReceivedRegion(int32_t timeoutMs) override;
    bool IsConnected() const override;
    void Cancel() override;
    PropertyData GetProperty(uint32_t propertyId) override;
    virtual void SetProperty(uint32_t propertyId, const void* value, size_t valueSize) override;

    void QueueBuffer(RdmaBuffer* buffer);

    virtual std::unique_ptr<RdmaMemoryRegion> CreateMemoryRegion(void* buffer, size_t bufferSize) = 0;
    virtual void QueueToQp(Direction _direction, RdmaBuffer* buffer) = 0;
    virtual bool CheckDeferredDestructionConditionsMet() override;

    virtual void PollForReceive(int32_t timeoutMs) = 0;

protected:
    enum class BufferOwnership
    {
        Unknown,
        Internal,
        External
    };
    enum class BufferType
    {
        Unknown,
        Single,
        Multiple
    };

    virtual void PreConnect(Direction _direction);
    virtual void PostConnect();
    virtual void PostConfigure();
    virtual void HandleDisconnect();
    virtual void SetupQueuePair() = 0;
    virtual void DestroyQP() = 0;
    void AckHandlerThread();

    void QueueSendBuffer(RdmaBuffer* buffer);
    void QueueRecvBuffer(RdmaBuffer* buffer, bool sendCreditUpdate);

    void CheckQueueStatus();

    Direction direction;
    std::vector<uint8_t> connectionData;
    bool usePolling = false;

private:
    void AddCredit(uint64_t bufferSize);
    void ProcessPreConfigureCredits();
    void SendCreditUpdate(uint64_t* bufferLengths, size_t numBuffers);

    std::unique_ptr<RdmaBufferQueue> transferBuffers;
    std::unique_ptr<RdmaBufferQueue> creditBuffers;
    std::queue<uint64_t> preConfigureCredits;
    bool _closing = false;
    boost::thread ackHandler;
    bool autoQueueRx;
    BufferOwnership bufferOwnership;
    BufferType bufferType;
    std::mutex configureLock;
    bool connected = false;
    bool bufferWaitInProgress = false;
};
