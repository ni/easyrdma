// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaCommon.h"
#include "RdmaConnectedSessionBase.h"
#include <thread>
#include <boost/thread.hpp>
#include "FdPoller.h"

class RdmaBufferQueue;
class RdmaBuffer;
class RdmaMemoryRegion;

class RdmaConnectedSession : public RdmaConnectedSessionBase {
    public:
        RdmaConnectedSession();
        RdmaConnectedSession(Direction _direction, rdma_cm_id* acceptedId, const std::vector<uint8_t>& connectionDataIn, const std::vector<uint8_t>& connectionDataOut);
        virtual ~RdmaConnectedSession();
        RdmaAddress GetLocalAddress() override;
        RdmaAddress GetRemoteAddress() override;

        virtual std::unique_ptr<RdmaMemoryRegion> CreateMemoryRegion(void* buffer, size_t bufferSize);
        virtual void QueueToQp(Direction _direction, RdmaBuffer* buffer);
    protected:
        void ConnectionHandlerThread();
        void SendReceiveHandlerThread(Direction _direction);
        void PollCompletionQueue(Direction _direction, ibv_wc* wc, bool blocking, int32_t nonBlockingPollTimeoutMs);
        void MakeCQsNonBlocking();
        void PostConnect() override;
        void PostConfigure() override;
        void SetupQueuePair() override;
        void DestroyQP() override;
        void Destroy();
        void PollForReceive(int32_t timeoutMs) override;

        rdma_cm_id* cm_id;
        RdmaAddress localAddress;
        RdmaAddress remoteAddress;
        boost::thread connectionHandler;

        boost::thread transferHandler;
        boost::thread ackHandler;
        FdPoller queueFdPoller;
        bool createdQp;
};
