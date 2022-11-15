// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <condition_variable>
#include <queue>
#include <map>
#include "RdmaCommon.h"

class iEventHandler {
public:
    virtual void SignalEvent(rdma_cm_event* event) = 0;
};

class EventManager : public iEventHandler {
    public:
        struct ConnectionEvent {
            rdma_cm_event_type eventType;
            rdma_cm_id* incomingConnectionId;
            std::vector<uint8_t> connectionData;
        };

        struct ConnectionQueue {
            std::queue<ConnectionEvent> events;
            std::mutex queueMutex;
            std::condition_variable moreEvents;
            bool waitAborted = false;

            // Note: There should only ever be one thread waiting on a specific connection at a time
            //       -cancelledResult is an optional parameter to communicate cancellation without
            //        using exceptions. Used when the code path expects to be cancelled without treating
            //        it as an error.
             ConnectionEvent WaitForEvent(int32_t timeoutMs, bool* cancelledResult = nullptr) {
                std::unique_lock<std::mutex> lock(queueMutex);
                if(!waitAborted && events.empty()) {
                    if(timeoutMs == -1) {
                        moreEvents.wait(lock);
                    }
                    else {
                        auto result = moreEvents.wait_for(lock, std::chrono::milliseconds(timeoutMs));
                        if(result == std::cv_status::timeout) {
                            RDMA_THROW(easyrdma_Error_Timeout);
                        }
                    }
                }
                if(waitAborted) {
                    if(cancelledResult) {
                        *cancelledResult = true;
                        return ConnectionEvent({});
                    }
                    else {
                        RDMA_THROW(easyrdma_Error_OperationCancelled);
                    }
                }
                else if(events.empty()) {
                    RDMA_THROW(easyrdma_Error_InternalError);
                }
                auto eventToReturn = events.front();
                events.pop();
                return eventToReturn;
            }

            void SignalEvent(rdma_cm_event* event) {
                std::lock_guard<std::mutex> lock(queueMutex);
                ConnectionEvent incomingEvent = {};
                incomingEvent.eventType = event->event;
                incomingEvent.incomingConnectionId = (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) ? event->id : nullptr;
                if(event->param.conn.private_data_len) {
                    const uint8_t* bufferStart = static_cast<const uint8_t*>(event->param.conn.private_data);
                    std::copy(bufferStart, bufferStart + event->param.conn.private_data_len, std::back_inserter(incomingEvent.connectionData));
                }
                events.push(incomingEvent);
                moreEvents.notify_one();
            }

            void CancelWaits() {
                std::lock_guard<std::mutex> lock(queueMutex);
                waitAborted = true;
                moreEvents.notify_one();
            }
        };

        virtual void SignalEvent(rdma_cm_event* event) {
            rdma_cm_id* eventConnection = nullptr;

            // RDMA_CM_EVENT_CONNECT_REQUEST is a little weird and puts the new connection
            // id as the event context
            if(event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
                eventConnection = event->listen_id;
            }
            else {
                eventConnection = event->id;
            }
            auto connectionQueue = GetConnectionQueueInternal(eventConnection);
            if(connectionQueue) {
                connectionQueue->SignalEvent(event);
            }
            else {
                TRACE("Saw event %s for unknown connection = %p", rdma_event_str(event->event), eventConnection);
            }
        }

        ConnectionEvent WaitForEvent(rdma_cm_id* connection, int32_t timeoutMs, bool* cancelledResult = nullptr) {
            auto connectionQueue = GetConnectionQueue(connection);
            return connectionQueue->WaitForEvent(timeoutMs, cancelledResult);
        }

        void AbortWaits(rdma_cm_id* connection) {
            auto connectionQueue = GetConnectionQueue(connection);
            connectionQueue->CancelWaits();
        }

        void CreateConnectionQueue(rdma_cm_id* connection) {
            std::lock_guard<std::mutex> lock(mapMutex);
            ASSERT_ALWAYS(connectionMap.find(connection) == connectionMap.end());
            std::shared_ptr<ConnectionQueue> createdQueue(new ConnectionQueue());
            connectionMap.insert(std::move(std::make_pair(connection, std::move(createdQueue))));
        }

        std::shared_ptr<ConnectionQueue> GetConnectionQueue(rdma_cm_id* connection) {
            auto foundQueue = GetConnectionQueueInternal(connection);
            ASSERT_ALWAYS(foundQueue);
            return foundQueue;
        }

        void DestroyConnectionQueue(rdma_cm_id* connection) {
            std::lock_guard<std::mutex> lock(mapMutex);
            auto foundConnection = connectionMap.find(connection);
            if(foundConnection != connectionMap.end()) {
                connectionMap.erase(foundConnection);
            }
        }

    private:
        std::shared_ptr<ConnectionQueue> GetConnectionQueueInternal(rdma_cm_id* connection) {
            std::lock_guard<std::mutex> lock(mapMutex);
            auto foundConnection = connectionMap.find(connection);
            if(foundConnection != connectionMap.end()) {
                return foundConnection->second;
            }
            else {
                return std::shared_ptr<ConnectionQueue>();
            }
        }

        std::mutex mapMutex;
        std::map<rdma_cm_id*, std::shared_ptr<ConnectionQueue>> connectionMap;
};