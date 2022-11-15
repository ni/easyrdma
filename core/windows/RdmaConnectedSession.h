// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaCommon.h"
#include "RdmaConnectedSessionBase.h"
#include <thread>
#include <deque>
#include <mutex>

class RdmaBufferQueue;
class RdmaBuffer;
class RdmaMemoryRegion;

class RdmaConnectedSession : public RdmaConnectedSessionBase {
    public:
        RdmaConnectedSession();
        RdmaConnectedSession(Direction _direction, IND2Adapter* _adapter, HANDLE _adapterFile, IND2Connector* _incomingConnection, const std::vector<uint8_t>& _connectionData, int32_t timeoutMs);

        virtual ~RdmaConnectedSession();
        RdmaAddress GetLocalAddress() override;
        RdmaAddress GetRemoteAddress() override;

        virtual std::unique_ptr<RdmaMemoryRegion> CreateMemoryRegion(void* buffer, size_t bufferSize);
        virtual void QueueToQp(Direction _direction, RdmaBuffer* buffer);
        void PollForReceive(int32_t timeoutMs) override;
    protected:
        enum class BufferOwnership {
            Unknown,
            Internal,
            External
        };
        enum class BufferType {
            Unknown,
            Single,
            Multiple
        };

        void SetupQueuePair() override;
        void DestroyQP() override;
        void ConnectionHandlerThread();
        void Destroy();

        virtual void PostConnect() override;

        void EventHandlerThread();

        void AcquireAndValidateConnectionData(IND2Connector* connector, Direction direction);

        IND2QueuePair* GetQP() const { return qp.get(); }

        boost::thread eventHandler;

        AutoRef<IND2Adapter> adapter;
        HANDLE adapterFile = nullptr;

        AutoRef<IND2Connector> connector;
        AutoRef<IND2CompletionQueue> cq;
        AutoRef<IND2QueuePair> qp;
        boost::thread connectionHandler;
        bool _closing;
        RdmaAddress remoteAddress;
};
