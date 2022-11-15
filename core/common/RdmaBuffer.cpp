// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaBuffer.h"
#include "RdmaBufferQueue.h"
#include "RdmaConnectedSessionBase.h"
#include <iostream>
#include <assert.h>
#include <memory>

RdmaBuffer::RdmaBuffer(RdmaConnectedSessionBase& _connection, RdmaBufferQueue& _bufferQueue, size_t index)
    : connection(_connection), bufferQueue(_bufferQueue), bufferIndex(index) {
}

RdmaBuffer::~RdmaBuffer() {
}


void RdmaBuffer::Requeue() {
    connection.QueueBuffer(this);
}

void RdmaBuffer::Release() {
    bufferQueue.ReleaseBuffer(this);
}

BufferCompletionCallbackData RdmaBuffer::GetAndClearClearCallbackData() {
    auto copy = completionCallbackData;
    completionCallbackData = BufferCompletionCallbackData({});
    return copy;
}

RdmaBufferInternal::RdmaBufferInternal(RdmaConnectedSessionBase& _connection, RdmaBufferQueue& _bufferQueue, size_t size, size_t index)
  : RdmaBuffer(_connection, _bufferQueue, index), allocatedBuffer(nullptr) {
    bufferMaxSize = size;
    bufferSize = size;

    // Make sure buffer is cache-aligned for best performance
    buffer = allocatedBuffer = AllocateAlignedMemory(size, 64);

    memoryRegion = connection.CreateMemoryRegion(buffer, bufferSize);
}

RdmaBufferInternal::~RdmaBufferInternal() {
    memoryRegion.reset();
    buffer = nullptr;
    if(allocatedBuffer) {
        FreeAlignedMemory(allocatedBuffer);
    }
}

void RdmaBufferInternal::SetBytesToSubmit(size_t size) {
    if(size > bufferMaxSize) {
        RDMA_THROW(easyrdma_Error_InvalidArgument);
    }
    bufferSize = size;
}

void RdmaBuffer::SetCompletionCallback(const BufferCompletionCallbackData& _completionCallbackData) {
    completionCallbackData = _completionCallbackData;
}

void RdmaBuffer::SetUsed(size_t size) { 
    if(size > bufferSize) {
        RDMA_THROW(easyrdma_Error_InvalidSize);
    }
    usedBytes = size;
}

void RdmaBuffer::HandleCompletion(RdmaError& completionStatus, size_t bytesTransferred) {
    usedBytes = bytesTransferred;
    bufferQueue.HandleCompletion(*this, completionStatus, false);
}

RdmaBufferExternal::RdmaBufferExternal(RdmaConnectedSessionBase& _connection, RdmaBufferQueue& _bufferQueue, RdmaMemoryRegion* _memoryRegion, size_t index)
: RdmaBuffer(_connection, _bufferQueue, index), memoryRegion(_memoryRegion) {
}

RdmaBufferExternal::~RdmaBufferExternal() {
}


void RdmaBufferExternal::SetBufferRegion(void* _buffer, size_t size) {
    buffer = _buffer;
    bufferSize = size;
    usedBytes = size;
}
