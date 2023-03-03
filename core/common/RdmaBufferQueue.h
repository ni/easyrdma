// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaBuffer.h"
#include "RdmaMemoryRegion.h"
#include "tCircularFifo.h"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <set>
#include <atomic>
#include <boost/intrusive/list.hpp>

class RdmaConnectedSessionBase;

class RdmaBufferQueue
{
public:
    RdmaBufferQueue(RdmaConnectedSessionBase& _connection, Direction _direction, bool _usePolling);
    virtual ~RdmaBufferQueue();

    void Abort(int32_t errorCode);
    void HandleCompletion(RdmaBuffer& buffer, RdmaError& completionStatus, bool putBackToIdle);

    enum class IgnoreCredits : uint32_t
    {
        Yes,
        No,
    };
    void QueueBuffer(RdmaBuffer* buffer, IgnoreCredits ignoreCredits);
    void ReleaseBuffer(RdmaBuffer* buffer);
    void AddCredit(uint64_t bufferSize);

    RdmaBuffer* WaitForCompletedBuffer(int32_t timeoutMs);
    RdmaBuffer* WaitForIdleBuffer(int32_t timeoutMs);
    size_t size() const
    {
        return buffers.size();
    }
    Direction getDirection() const
    {
        return direction;
    }

    PropertyData GetProperty(uint32_t propertyId);
    bool HasUserBuffersOutstanding();
    RdmaError GetQueueStatus();

protected:
    void AllocateBufferQueues(size_t numBuffers);

    RdmaConnectedSessionBase& connection;
    Direction direction;
    std::vector<std::unique_ptr<RdmaBuffer>> buffers;
    tCircularFifo<RdmaBuffer*> idleBuffers;
    tCircularFifo<RdmaBuffer*> queuedBuffers;
    tCircularFifo<RdmaBuffer*> completedBuffers;
    tCircularFifo<RdmaBuffer*> buffersQueuedWaitingForCredits;
    boost::intrusive::list<RdmaBuffer,
        boost::intrusive::member_hook<RdmaBuffer, boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>, &RdmaBuffer::userBufferListNode>,
        boost::intrusive::constant_time_size<false> >
        userBuffers;
    std::mutex queueLock;
    RdmaError queueStatus;
    std::condition_variable completedAvailableCond;
    std::condition_variable idleAvailableCond;
    std::condition_variable noneQueuedCond;
    bool putBackToIdleOnCompletion;
    std::queue<uint64_t> availableCredits;
    bool aborted;
    bool usePolling;
};

class RdmaBufferQueueMultipleBuffer : public RdmaBufferQueue
{
public:
    RdmaBufferQueueMultipleBuffer(RdmaConnectedSessionBase& _connection, Direction _direction, size_t numBuffers, size_t bufferSize, bool _usePolling);

protected:
};

class RdmaBufferQueueSingleBuffer : public RdmaBufferQueue
{
public:
    RdmaBufferQueueSingleBuffer(RdmaConnectedSessionBase& _connection, Direction _direction, void* _buffer, size_t _bufferSize, size_t numOverlapped, bool _usePolling);
    virtual ~RdmaBufferQueueSingleBuffer();

protected:
    bool internallyAllocated;
    void* buffer = nullptr;
    size_t bufferSize = 0;
    std::unique_ptr<RdmaMemoryRegion> memoryRegion;
};
