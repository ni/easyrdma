// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaCommon.h"
#include "RdmaBuffer.h"
#include "RdmaConnectedSessionBase.h"
#include "RdmaBufferQueue.h"
#include <assert.h>

RdmaBufferQueue::RdmaBufferQueue(RdmaConnectedSessionBase& _connection, Direction _direction, bool _usePolling) : connection(_connection), direction(_direction), aborted(false), usePolling(_usePolling) {
    putBackToIdleOnCompletion = (direction == Direction::Send);
    #ifdef _WIN32
        if(usePolling) {
            RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable on Windows
        }
    #endif
    if(usePolling && direction == Direction::Send) {
        RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable for this situation
    }
}


RdmaBufferQueue::~RdmaBufferQueue() {
    Abort(easyrdma_Error_OperationCancelled);
    assert(!buffersQueuedWaitingForCredits.size());
    assert(!queuedBuffers.size());
    // Release buffers from userBuffers list before destroying
    // the buffers themselves since the ITL container asserts
    while(userBuffers.size()) {
        userBuffers.pop_front();
    }
    buffers.clear();
}

void RdmaBufferQueue::Abort(int32_t errorCode) {
    // Ensure callbacks are called outside of our mutex, so that the caller can potentially
    // call back into our API from within the callback without deadlocking
    std::vector<BufferCompletionCallbackData> callbacksToFire;
    {
        std::lock_guard<std::mutex> guard(queueLock);
        if(aborted) {
            return;
        }
        aborted = true;
        RDMA_SET_ERROR(queueStatus, errorCode);
        while(queuedBuffers.size()) {
            auto& buffer = queuedBuffers.front();
            auto callbackData = buffer->GetAndClearClearCallbackData();
            if(callbackData.IsSet()) {
                callbacksToFire.push_back(callbackData);
            }
            queuedBuffers.pop();
            idleBuffers.push(buffer);
        }
        while(buffersQueuedWaitingForCredits.size()) {
            auto& buffer = buffersQueuedWaitingForCredits.front();
            auto callbackData = buffer->GetAndClearClearCallbackData();
            if(callbackData.IsSet()) {
                callbacksToFire.push_back(callbackData);
            }
            buffersQueuedWaitingForCredits.pop();
            idleBuffers.push(buffer);
        }
        completedAvailableCond.notify_all();
        idleAvailableCond.notify_all();
    }
    for(auto& callback : callbacksToFire) {
        callback.Call(errorCode, 0);
    }
}

RdmaBuffer* RdmaBufferQueue::WaitForIdleBuffer(int32_t timeoutMs) {
    std::unique_lock<std::mutex> guard(queueLock);
    if((idleBuffers.size() == 0) && !queueStatus.IsError()) {
        if(timeoutMs == -1) {
            idleAvailableCond.wait(guard);
        }
        else {
            auto result = idleAvailableCond.wait_for(guard, std::chrono::milliseconds(timeoutMs));
            if(result == std::cv_status::timeout) {
                RDMA_THROW(easyrdma_Error_Timeout);
            }
        }
    }
    if(queueStatus.IsError()) {
        throw RdmaException(queueStatus);
        return nullptr;
    }
    if(!idleBuffers.size()) {
        RDMA_THROW(easyrdma_Error_Timeout);
    }
    RdmaBuffer* buffer = idleBuffers.front();
    idleBuffers.pop();
    userBuffers.push_back(*buffer);
    return buffer;
}



RdmaBuffer* RdmaBufferQueue::WaitForCompletedBuffer(int32_t timeoutMs) {
    std::unique_lock<std::mutex> guard(queueLock);
    if(putBackToIdleOnCompletion) {
        RDMA_THROW(easyrdma_Error_InvalidOperation); // not applicable for this situation
    }
    if((completedBuffers.size() == 0) && !queueStatus.IsError()) {
        if(queuedBuffers.size() == 0 && buffersQueuedWaitingForCredits.size() == 0) {
            RDMA_THROW(easyrdma_Error_NoBuffersQueued);
        }
        if(usePolling) {
            queueLock.unlock();
            connection.PollForReceive(timeoutMs);
            queueLock.lock();
        }
        else {
            if(timeoutMs == -1) {
                completedAvailableCond.wait(guard);
            }
            else {
                auto result = completedAvailableCond.wait_for(guard, std::chrono::milliseconds(timeoutMs));
                if(result == std::cv_status::timeout) {
                    RDMA_THROW(easyrdma_Error_Timeout);
                }
            }
        }
    }
    // If we have an error in the queue (such as disconnection) but we have a completed
    // buffer, we can return it without erroring
    if(queueStatus.IsError() && completedBuffers.size() == 0) {
        throw RdmaException(queueStatus);
        return nullptr;
    }
    if(!completedBuffers.size()) {
        RDMA_THROW(easyrdma_Error_Timeout);
    }
    RdmaBuffer* buffer = completedBuffers.front();
    completedBuffers.pop();
    userBuffers.push_back(*buffer);
    return buffer;
}


void RdmaBufferQueue::HandleCompletion(RdmaBuffer& buffer, RdmaError& completionStatus, bool putBackToIdle) {

    // Cache and clear completion data before returning it to the accessible queues. Once
    // it has been returned it could get queued again.
    // Ensure callbacks are called outside of our mutex, so that the caller can potentially
    // call back into our API from within the callback without deadlockin
    BufferCompletionCallbackData cachedCompletionData;
    size_t completedBytes = 0;

    {
        std::lock_guard<std::mutex> guard(queueLock);

        if(!aborted) {
            cachedCompletionData = buffer.GetAndClearClearCallbackData();
            completedBytes = buffer.GetUsed();

            // Buffers should be completed in-order
            ASSERT_ALWAYS(&buffer == queuedBuffers.front());
            queuedBuffers.pop();
            if(!putBackToIdleOnCompletion) {
                completedBuffers.push(&buffer);
                completedAvailableCond.notify_all();
            }
            else {
                idleBuffers.push(&buffer);
                idleAvailableCond.notify_all();
            }

            if(queuedBuffers.empty()) {
                noneQueuedCond.notify_all();
            }
            if(completionStatus.IsError()) {
                queueStatus.Assign(completionStatus);
            }
        }
    }
    cachedCompletionData.Call(completionStatus.GetCode(), completedBytes);
}

void RdmaBufferQueue::QueueBuffer(RdmaBuffer* buffer, IgnoreCredits ignoreCredits) {
    bool queueToQp = false;
    {
        std::lock_guard<std::mutex> guard(queueLock);
        if(queueStatus.IsError()) {
            throw RdmaException(queueStatus);
        }
        if(!buffer->userBufferListNode.is_linked()) {
            RDMA_THROW(easyrdma_Error_InvalidOperation);
        }
        if(direction == Direction::Send && ignoreCredits == IgnoreCredits::No) {
            if(availableCredits.size()) {
                uint64_t poppedCreditSize = availableCredits.front();
                try
                {
                    if(buffer->GetUsed() > poppedCreditSize) {
                        RDMA_THROW(easyrdma_Error_SendTooLargeForRecvBuffer);
                    }
                    queueToQp = true;
                    queuedBuffers.push(buffer);
                    availableCredits.pop();
                }
                catch(const RdmaException& e) {
                    // Store error in global queue status, then re-throw to caller
                    queueStatus.Assign(e.rdmaError);
                    throw;
                }
            }
            else {
                buffersQueuedWaitingForCredits.push(buffer);
            }
        }
        else {
            queueToQp = true;
            queuedBuffers.push(buffer);
        }
        // Remove from userBuffers only after nothing above threw
        buffer->userBufferListNode.unlink();
    }
    if(queueToQp) {
        connection.QueueToQp(direction, buffer);
    }
}

void RdmaBufferQueue::AddCredit(uint64_t bufferSize) {
    RdmaBuffer* bufferToQueueToQp = nullptr;
    try
    {
        {
            std::lock_guard<std::mutex> guard(queueLock);
            availableCredits.push(bufferSize);
            if(buffersQueuedWaitingForCredits.size()) {
                uint64_t poppedCreditSize = availableCredits.front();
                bufferToQueueToQp = buffersQueuedWaitingForCredits.front();
                if(bufferToQueueToQp->GetUsed() > poppedCreditSize) {
                    RDMA_THROW(easyrdma_Error_SendTooLargeForRecvBuffer);
                }
                buffersQueuedWaitingForCredits.pop();
                queuedBuffers.push(bufferToQueueToQp);
                availableCredits.pop();
            }
        }
        if(bufferToQueueToQp) {
            connection.QueueToQp(direction, bufferToQueueToQp);
        }
    }
    catch(const RdmaException& e) {
        // Store error in global queue status, then re-throw to caller
        queueStatus.Assign(e.rdmaError);
        throw;
    }
}

void RdmaBufferQueue::ReleaseBuffer(RdmaBuffer* buffer) {
    std::lock_guard<std::mutex> guard(queueLock);
    if(!buffer->userBufferListNode.is_linked()) {
        RDMA_THROW(easyrdma_Error_InvalidOperation);
    }
    buffer->userBufferListNode.unlink();
    idleBuffers.push(buffer);
}

PropertyData RdmaBufferQueue::GetProperty(uint32_t propertyId) {
    std::lock_guard<std::mutex> guard(queueLock);
    switch(propertyId) {
        case easyrdma_Property_QueuedBuffers: {
            uint64_t numQueued = queuedBuffers.size() + buffersQueuedWaitingForCredits.size();
            return PropertyData(numQueued);
        }
        case easyrdma_Property_UserBuffers: {
            uint64_t numUser = userBuffers.size();
            return PropertyData(numUser);
        }
        default:
            RDMA_THROW(easyrdma_Error_InvalidProperty);
    }
}

bool RdmaBufferQueue::HasUserBuffersOutstanding() {
    std::lock_guard<std::mutex> guard(queueLock);
    return !userBuffers.empty();
}

RdmaError RdmaBufferQueue::GetQueueStatus() {
    std::lock_guard<std::mutex> guard(queueLock);
    return queueStatus;
}


void RdmaBufferQueue::AllocateBufferQueues(size_t numBuffers) {
    buffers.resize(numBuffers);
    idleBuffers.reallocate(numBuffers);
    queuedBuffers.reallocate(numBuffers);
    completedBuffers.reallocate(numBuffers);
    buffersQueuedWaitingForCredits.reallocate(numBuffers);
}

RdmaBufferQueueMultipleBuffer::RdmaBufferQueueMultipleBuffer(RdmaConnectedSessionBase& _connection, Direction _direction, size_t numBuffers, size_t bufferSize, bool _usePolling) : RdmaBufferQueue(_connection, _direction, _usePolling) {
    AllocateBufferQueues(numBuffers);
    size_t index = 0;
    for(auto& buffer : buffers) {
        buffer.reset(new RdmaBufferInternal(_connection, *this, bufferSize, index++));
        idleBuffers.push(buffer.get());
    }
}


RdmaBufferQueueSingleBuffer::RdmaBufferQueueSingleBuffer(RdmaConnectedSessionBase& _connection, Direction _direction, void* _buffer, size_t _bufferSize, size_t numOverlapped, bool _usePolling) : RdmaBufferQueue(_connection, _direction, _usePolling), buffer(_buffer), bufferSize(_bufferSize), internallyAllocated(false) {
    putBackToIdleOnCompletion = true;
    memoryRegion = _connection.CreateMemoryRegion(_buffer, _bufferSize);
    AllocateBufferQueues(numOverlapped);
    size_t index = 0;
    for(auto& buffer : buffers) {
        buffer.reset(new RdmaBufferExternal(_connection, *this, memoryRegion.get(), index++));
        idleBuffers.push(buffer.get());
    }
}


RdmaBufferQueueSingleBuffer::~RdmaBufferQueueSingleBuffer() {
    Abort(easyrdma_Error_OperationCancelled);
    buffers.clear();
    memoryRegion.reset();
}
