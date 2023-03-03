// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaCommon.h"
#include "RdmaConnectedSession.h"
#include "RdmaConnectionData.h"
#include "RdmaBuffer.h"
#include "common/RdmaAddress.h"
#include "RdmaMemoryRegion.h"
#include <assert.h>
#include "rdma/rdma_verbs.h"
#include "EventManager.h"
#include "ThreadUtility.h"

using namespace EasyRDMA;

RdmaConnectedSession::RdmaConnectedSession() :
    RdmaConnectedSessionBase(), cm_id(nullptr), createdQp(false)
{
}

RdmaConnectedSession::RdmaConnectedSession(Direction _direction, rdma_cm_id* acceptedId, const std::vector<uint8_t>& connectionDataIn, const std::vector<uint8_t>& connectionDataOut) :
    RdmaConnectedSessionBase(connectionDataOut), cm_id(acceptedId), createdQp(false)
{
    try {
        GetEventManager().CreateConnectionQueue(acceptedId);

        PreConnect(_direction);
        try {
            ValidateConnectionData(connectionDataIn, _direction);
        } catch (const RdmaException& e) {
            // If validation of the private_data from the connector side fails, the listener calls reject
            rdma_reject(acceptedId, connectionDataIn.data(), connectionDataIn.size());
            throw;
        }
        // Accept
        rdma_conn_param connectParams = {};
        connectParams.private_data = connectionData.data();
        connectParams.private_data_len = connectionData.size();
        connectParams.retry_count = 10;
        connectParams.rnr_retry_count = 10;
        HandleError(rdma_accept(acceptedId, &connectParams));

        // Wait for connection to be established
        // NOTE: We use a fixed timeout here because this is not the portion of the Accept that waits for an incoming connection.
        //       A connection attempt callback already caused us to get to here, and the rdma_accept call is simply completing the handshake
        //       with the remote side, which should be minimal.
        auto establishedEvent = GetEventManager().WaitForEvent(acceptedId, 1000);
        if (establishedEvent.eventType != RDMA_CM_EVENT_ESTABLISHED) {
            TRACE("establishedEvent.eventType = %s\n", rdma_event_str(establishedEvent.eventType));
            RDMA_THROW(easyrdma_Error_UnableToConnect);
        }
        PostConnect();
        localAddress = RdmaAddress(rdma_get_local_addr(cm_id));
    } catch (std::exception&) {
        // Since we create threads inside our CTOR, we need to make sure we join them
        Destroy();
        throw;
    }
}

RdmaConnectedSession::~RdmaConnectedSession()
{
    Destroy();
}

void RdmaConnectedSession::Destroy()
{
    if (cm_id) {
        rdma_disconnect(cm_id);
    }

    queueFdPoller.Cancel();
    if (transferHandler.joinable()) {
        transferHandler.join();
    }
    if (ackHandler.joinable()) {
        ackHandler.join();
    }

    // Unblock connection handler
    if (cm_id) {
        GetEventManager().AbortWaits(cm_id);
    }
    if (connectionHandler.joinable()) {
        connectionHandler.join();
    }

    // Call after we join connectionHandler thread so we don't have to
    // worry about race conditions
    HandleDisconnect();

    if (cm_id) {
        if (createdQp) {
            rdma_destroy_qp(cm_id);
            createdQp = false;
        }
        GetEventManager().DestroyConnectionQueue(cm_id);
        rdma_destroy_id(cm_id);
        cm_id = nullptr;
    }
}

void RdmaConnectedSession::PostConnect()
{
    remoteAddress = RdmaAddress(rdma_get_peer_addr(cm_id));
    RdmaConnectedSessionBase::PostConnect();
    connectionHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::ConnectionHandlerThread, this), kThreadPriority::Normal, "ConnHandler");

    // Always start our ack handler at connection time, because the other side might configure first
    if (direction == Direction::Send) {
        ackHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::SendReceiveHandlerThread, this, Direction::Receive), kThreadPriority::Normal, "AckRecvHandler");
    } else {
        ackHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::SendReceiveHandlerThread, this, Direction::Send), kThreadPriority::Normal, "AckSendHandler");
    }
}

void RdmaConnectedSession::PostConfigure()
{
    if (direction == Direction::Receive) {
        if (!usePolling) {
            // Only if running a real-time kernel will we attempt to set our priority to rt. This is a pretty rough
            // distinction between Linux RT and Desktop, but holds up true enough for the time being. Really this comes down
            // more to privileges of the current process rather than OS capabilities.
            auto priority = IsRealtimeKernel() ? kThreadPriority::High : kThreadPriority::Normal;
            transferHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::SendReceiveHandlerThread, this, Direction::Receive), priority, "RecvHandler");
        }
    } else {
        transferHandler = CreatePriorityThread(boost::bind(&RdmaConnectedSession::SendReceiveHandlerThread, this, Direction::Send), kThreadPriority::Normal, "SendHandler");
    }
    RdmaConnectedSessionBase::PostConfigure();
}

RdmaAddress RdmaConnectedSession::GetLocalAddress()
{
    return localAddress;
}

RdmaAddress RdmaConnectedSession::GetRemoteAddress()
{
    return remoteAddress;
}

void RdmaConnectedSession::ConnectionHandlerThread()
{
    try {
        bool cancelled = false;
        while (true) {
            auto event = GetEventManager().WaitForEvent(cm_id, -1, &cancelled);
            if (cancelled) {
                break;
            }
            switch (event.eventType) {
                case RDMA_CM_EVENT_DISCONNECTED:
                    HandleDisconnect();
                    break;
                default:
                    break;
            }
        }
    } catch (std::exception&) {
        assert(0);
    }
}

void RdmaConnectedSession::QueueToQp(Direction _direction, RdmaBuffer* buffer)
{
    if (_direction == Direction::Send) {
        HandleError(rdma_post_send(cm_id, buffer, buffer->GetPointer(), buffer->GetUsed(), buffer->GetMemoryRegion()->GetMR(), IBV_SEND_SIGNALED));
    } else {
        // If this process is being instrumented by Valgrind, it has no way of knowing that this buffer for RDMA is going to be written to
        // by the hardware. This has the downside of flagging memory passed for recv as possibly uninitialized, unless the entity allocating it
        // initializes it. For recv, this is unnecessary (and costs performance), so it is expected to not do this. Our own internal allocations are not
        // initialized either. The problem with this is that valgrind becomes unusable, because the possibly-uninitalized data taints a bunch of code paths
        // (like the credit mechanism). So to be nice for that use case, if we detect we're running under valgrind, we will initialize their memory on every
        // recv. Note that valgrind already adds so much overhead that this isn't a big deal.
        if (IsValgrindRunning()) {
            memset(buffer->GetPointer(), 0, buffer->GetSize());
        }
        HandleError(rdma_post_recv(cm_id, buffer, buffer->GetPointer(), buffer->GetSize(), buffer->GetMemoryRegion()->GetMR()));
    }
}

std::unique_ptr<RdmaMemoryRegion> RdmaConnectedSession::CreateMemoryRegion(void* buffer, size_t bufferSize)
{
    return std::unique_ptr<RdmaMemoryRegion>(new RdmaMemoryRegion(cm_id, buffer, bufferSize));
}

void RdmaConnectedSession::MakeCQsNonBlocking()
{
    int flags = 0;
    flags = fcntl(cm_id->recv_cq_channel->fd, F_GETFL);
    HandleError(fcntl(cm_id->recv_cq_channel->fd, F_SETFL, flags | O_NONBLOCK));

    flags = fcntl(cm_id->send_cq_channel->fd, F_GETFL);
    HandleError(fcntl(cm_id->send_cq_channel->fd, F_SETFL, flags | O_NONBLOCK));
}

void RdmaConnectedSession::PollForReceive(int32_t timeoutMs)
{
    ibv_wc wc;
    PollCompletionQueue(Direction::Receive, &wc, false, timeoutMs);
    // TRACE("Completed buffer: direction = %s, status = %d, size = %d", direction == Direction::Receive ? "Recv" : "Send", wc.status, wc.byte_len);
    RdmaBuffer* buffer = reinterpret_cast<RdmaBuffer*>(wc.wr_id);
    RdmaError completionStatus;
    if (wc.status != IBV_WC_SUCCESS) {
        try {
            RDMA_THROW_WITH_SUBCODE(RdmaErrorTranslation::IBVErrorToRdmaError(wc.status), wc.status);
        } catch (const RdmaException& e) {
            completionStatus.Assign(e.rdmaError);
        }
    }
    size_t bytesTransferred;
    switch (wc.opcode) {
        case IBV_WC_RECV:
            bytesTransferred = wc.byte_len;
            break;
        case IBV_WC_SEND:
            bytesTransferred = completionStatus.IsSuccess() ? buffer->GetUsed() : 0;
            break;
        default:
            RDMA_THROW(easyrdma_Error_InternalError);
    }
    buffer->HandleCompletion(completionStatus, bytesTransferred);
}

// This function is modeled after the inline functions rdma_get_send_comp/rdma_get_recv_comp.
// The main changes are:
//  - Combine send/recv into a single function with direction specified
//  - Use the ibv dynlib wrapper instead of directly calling exported functions
//  - Make use of poll instead of blocking in ibv_get_cq_event and allow cancellation
void RdmaConnectedSession::PollCompletionQueue(Direction _direction, ibv_wc* wc, bool blocking, int32_t nonBlockingPollTimeoutMs)
{
    ibv_cq* cq = _direction == Direction::Send ? cm_id->send_cq : cm_id->recv_cq;
    ibv_comp_channel* channel = _direction == Direction::Send ? cm_id->send_cq_channel : cm_id->recv_cq_channel;

    struct ibv_cq* eventCq;
    void* context;
    int ret;

    auto pollStart = std::chrono::steady_clock::now();

    do {
        // The below are inline functions in the verbs header
        ret = ibv_poll_cq(cq, 1, wc);
        if (ret)
            break;

        if (blocking) {
            ret = ibv_req_notify_cq(cq, 0);
            if (ret)
                HandleError(rdma_seterrno(ret));

            ret = ibv_poll_cq(cq, 1, wc);
            if (ret)
                break;

            // This is the special part that isn't in rdma_get_send_comp/rdma_get_recv_comp.
            // Since we make the channel fd non-blocking, we need to call poll() on the fd here
            // instead of expecting ibv_get_cq_event to block below. However, we want to make sure
            // we can cancel, so we use the FdPoller.
            if (!queueFdPoller.PollOnFd(channel->fd, -1)) {
                RDMA_THROW(easyrdma_Error_OperationCancelled);
            }

            HandleError(ibv_get_cq_event(channel, &eventCq, &context));
            assert(eventCq == cq && context == cm_id);
            ibv_ack_cq_events(cq, 1);
        } else {
            CheckQueueStatus();
            if (nonBlockingPollTimeoutMs != -1) {
                if (std::chrono::steady_clock::now() - pollStart > std::chrono::milliseconds(nonBlockingPollTimeoutMs)) {
                    RDMA_THROW(easyrdma_Error_Timeout);
                }
            }
        }
    } while (1);

    if (ret < 0) {
        HandleError(rdma_seterrno(ret));
    }
}

void RdmaConnectedSession::SendReceiveHandlerThread(Direction _direction)
{
    try {
        ibv_wc wc;
        MakeCQsNonBlocking();
        while (IsConnected()) {
            PollCompletionQueue(_direction, &wc, true, 0);
            // TRACE("Completed buffer: direction = %s, status = %d, size = %d", direction == Direction::Receive ? "Recv" : "Send", wc.status, wc.byte_len);
            RdmaBuffer* buffer = reinterpret_cast<RdmaBuffer*>(wc.wr_id);
            RdmaError completionStatus;
            if (wc.status != IBV_WC_SUCCESS) {
                try {
                    RDMA_THROW_WITH_SUBCODE(RdmaErrorTranslation::IBVErrorToRdmaError(wc.status), wc.status);
                } catch (const RdmaException& e) {
                    completionStatus.Assign(e.rdmaError);
                }
            }
            size_t bytesTransferred;
            switch (wc.opcode) {
                case IBV_WC_RECV:
                    bytesTransferred = wc.byte_len;
                    break;
                case IBV_WC_SEND:
                    bytesTransferred = completionStatus.IsSuccess() ? buffer->GetUsed() : 0;
                    break;
                default:
                    RDMA_THROW(easyrdma_Error_InternalError);
            }
            buffer->HandleCompletion(completionStatus, bytesTransferred);
        }
    } catch (std::exception& e) {
        // TRACE("Error in buffer completion: direction = %s, error = %s\n", direction == Direction::Receive ? "Recv" : "Send", e.what());
        //  No-op, silently exit thread. Normal errors are handled within the completion methods.
    }
}

void RdmaConnectedSession::SetupQueuePair()
{
    assert(!createdQp);
    ibv_qp_init_attr qp_init = {};
    // The sizes below are somewhat arbitrary. They will limit the
    // maximum number of queued send/recv requests allowed. There are ways
    // to query the maximum allowed, but it can get complicated with how resources
    // might be shared. On Windows we set this to the maximum returned, but we haven't
    // added code to get that on Linux. We can revisit raising these or querying the max
    // if this becomes an issue. There are not many practical applications for having
    // this many concurrently queued requests.
    qp_init.cap.max_send_wr = 1024;
    qp_init.cap.max_recv_wr = 1024;
    // We always use a single buffer per request
    qp_init.cap.max_recv_sge = 1;
    qp_init.cap.max_send_sge = 1;
    qp_init.qp_type = IBV_QPT_RC;
    qp_init.qp_context = cm_id;
    HandleError(rdma_create_qp(cm_id, nullptr, &qp_init));
    createdQp = true;
}

void RdmaConnectedSession::DestroyQP()
{
    if (createdQp) {
        rdma_destroy_qp(cm_id);
        createdQp = false;
    }
}
