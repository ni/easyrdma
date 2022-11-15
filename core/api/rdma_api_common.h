// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaError.h"
#include "RdmaSession.h"
#include "api/tAccessManagedRef.h"
#include <map>
#include <mutex>
#include "easyrdma.h"

namespace EasyRDMA {

enum class tCheckDeferredCloseTable {
    No,
    Yes,
};

typedef tAccessManagedRef<RdmaSession> RdmaSessionRef;

class SessionManager {
    public:
        easyrdma_Session RegisterSession(RdmaSessionRef& session) {
            std::unique_lock<std::mutex> guard(mapLock);
            easyrdma_Session sessionHandle = reinterpret_cast<easyrdma_Session>(nextSession++);
            sessionMap[sessionHandle] = session.GetResource();
            return sessionHandle;
        }
        RdmaSessionRef GetSession(easyrdma_Session session, tAccessType access = kAccess_Exclusive, tCheckDeferredCloseTable checkDeferredCloseTable = tCheckDeferredCloseTable::No) {
            std::unique_lock<std::mutex> guard(mapLock);
            auto it = sessionMap.find(session);
            if(it != sessionMap.end()) {
                return RdmaSessionRef(it->second, access);
            }
            if(checkDeferredCloseTable == tCheckDeferredCloseTable::Yes) {
                auto deferedIt = deferredCloseSessionMap.find(session);
                if(deferedIt != deferredCloseSessionMap.end()) {
                    return RdmaSessionRef(deferedIt->second, access, true);
                }
            }
            RDMA_THROW(easyrdma_Error_InvalidSession);
         }
        void DestroySession(easyrdma_Session session, uint32_t flags) {
            RdmaSessionRef erasedSession;
            bool deferredDestruction = false;
            {
                std::unique_lock<std::mutex> guard(mapLock);
                auto it = sessionMap.find(session);
                if(it != sessionMap.end()) {
                    erasedSession = RdmaSessionRef(it->second, kAccess_Exclusive);
                    sessionMap.erase(it);

                    if(flags & easyrdma_CloseFlags_DeferWhileUserBuffersOutstanding) {
                        if(!erasedSession->CheckDeferredDestructionConditionsMet()) {
                            deferredCloseSessionMap[session] = erasedSession.GetResource();
                            deferredDestruction = true;
                        }
                    }
                }
                else {
                    RDMA_THROW(easyrdma_Error_InvalidSession);
                }
            }

            erasedSession->Cancel();

            // Now release and wait for all references to be gone
            if(!deferredDestruction) {
                erasedSession.ReleaseAndWaitForAllReferencesGone();
            }
        }

        void CheckDeferredSessionDestructionReady(RdmaSessionRef& sessionRef, easyrdma_Session sessionHandle) {
            assert(sessionRef.IsDestructionPending());
            if(sessionRef->CheckDeferredDestructionConditionsMet()) {
                assert(deferredCloseSessionMap.find(sessionHandle) != deferredCloseSessionMap.end());
                deferredCloseSessionMap.erase(sessionHandle);
                sessionRef.ReleaseAndWaitForAllReferencesGone();
            }
        }

        uint64_t GetOpenedSessions() const {
            std::unique_lock<std::mutex> guard(mapLock);
            return sessionMap.size();
        }

        uint64_t GetDeferredCloseSessions() const {
            std::unique_lock<std::mutex> guard(mapLock);
            return deferredCloseSessionMap.size();
        }

    private:
        mutable std::mutex mapLock;
        std::map<easyrdma_Session, std::shared_ptr<RdmaSession>> sessionMap;
        std::map<easyrdma_Session, std::shared_ptr<RdmaSession>> deferredCloseSessionMap;
        uintptr_t nextSession = 1;
};

extern SessionManager sessionManager;

void GlobalInitializeIfNeeded();

};