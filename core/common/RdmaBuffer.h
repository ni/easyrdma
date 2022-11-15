// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <vector>
#include <functional>
#include "RdmaSession.h"
#include "RdmaMemoryRegion.h"
#include <boost/intrusive/list.hpp>

class RdmaConnectedSessionBase;
class RdmaBufferQueue;

class RdmaBuffer : public RdmaBufferRegion {
    public:
        RdmaBuffer(RdmaConnectedSessionBase& _connection, RdmaBufferQueue& _bufferQueue, size_t index);
        virtual ~RdmaBuffer();

        // RdmaBufferRegion
        void* GetPointer() const override { return GetBuffer(); }
        size_t GetSize() const override { return GetBufferLen(); }
        void SetUsed(size_t size) override;
        size_t GetUsed() const override { return usedBytes; }
        void Requeue() override;
        void Release() override;

        void* GetBuffer() const { return buffer; }
        size_t GetBufferLen() const { return bufferSize; }
        size_t GetIndex() const { return bufferIndex; }
        void SetCompletionCallback(const BufferCompletionCallbackData& _completionCallbackData);
        virtual void HandleCompletion(RdmaError& completionStatus, size_t bytesTransferred);
        BufferCompletionCallbackData GetAndClearClearCallbackData();

        virtual RdmaMemoryRegion* GetMemoryRegion() = 0;

        // Used to store the buffer into an intrusive list of user-owned buffers
        boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> userBufferListNode;
    protected:
        size_t bufferIndex = 0;
        void* buffer =  nullptr;
        size_t bufferSize = 0;
        RdmaConnectedSessionBase& connection;
        RdmaBufferQueue& bufferQueue;
        size_t usedBytes = 0;
        BufferCompletionCallbackData completionCallbackData;
};


class RdmaBufferInternal : public RdmaBuffer {
    public:
        RdmaBufferInternal(RdmaConnectedSessionBase& _connection, RdmaBufferQueue& _bufferQueue, size_t size, size_t index);
        virtual ~RdmaBufferInternal();
        void SetBytesToSubmit(size_t size);
        RdmaMemoryRegion* GetMemoryRegion() { return memoryRegion.get(); }
    protected:
        std::unique_ptr<RdmaMemoryRegion> memoryRegion;
        void* allocatedBuffer;
        size_t bufferMaxSize = 0;
};

class RdmaBufferExternal : public RdmaBuffer {
    public:
        RdmaBufferExternal(RdmaConnectedSessionBase& _connection, RdmaBufferQueue& _bufferQueue, RdmaMemoryRegion* _memoryRegion, size_t index);
        virtual ~RdmaBufferExternal();

        void SetBufferRegion(void* buffer, size_t size);
        RdmaMemoryRegion* GetMemoryRegion() { return memoryRegion; }
    protected:
        RdmaMemoryRegion* memoryRegion;
};

