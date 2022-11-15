// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

//============================================================================
//  Includes
//============================================================================
#include "common/RdmaError.h"
#include <mutex>
#include <atomic>
#include <assert.h>
#include <algorithm>
#include <thread>
#include <sstream>
#include <condition_variable>
#include "api/easyrdma.h"
#include "iAccessManaged.h"


//============================================================================
//  Class tAccessManager
//============================================================================
class tAccessManager {
    public:
        //--------------------------------------------------------------------
        //  Constraints
        //--------------------------------------------------------------------
        static const size_t kMaxNestLevel = 32;
        static const size_t kInitialNumberOfRequestsInAccessManager = 32;
        //--------------------------------------------------------------------
        //  Structs
        //--------------------------------------------------------------------
        struct tAccessStack {
            size_t size;
            bool val[kMaxNestLevel];
        };
        //--------------------------------------------------------------------
        //  Constructor
        //--------------------------------------------------------------------
        tAccessManager();
        ~tAccessManager();
        //--------------------------------------------------------------------
        //  Methods
        //--------------------------------------------------------------------
        void Acquire(bool exclusive);
        bool Release();
        void SuspendAccess();
        void ResumeAccess();
        void IncRef();
        void DecRef();
        void WaitForAllReferencesToBeReleased();
        bool HasExclusiveAccess();
        bool HasSharedAccess();
        void AcquireAll(const tAccessStack& accessStack);
        tAccessStack ReleaseAll();
        //--------------------------------------------------------------------
        //  Debug methods - some used by unit test infrastructure
        //--------------------------------------------------------------------
        void DebugAssertActive(std::thread::id tid);
        void DebugDump();
        uint32_t DebugGetRefCount() const;
        uint32_t DebugGetActiveCount() const;
        uint32_t DebugGetActiveSharedCount() const;
        uint32_t DebugGetActiveExclusiveCount() const;
        void WaitForAllReferencesToBeReleasedWithTimeout(int32_t timeout);
    private:
        tAccessManager(const tAccessManager&) = delete;
        tAccessManager& operator=(const tAccessManager&) = delete;
        //--------------------------------------------------------------------
        //  Local Enums
        //--------------------------------------------------------------------
        enum tSatisfyFlags {
            kHighPriority    = 1 << 0,
            kDifferentThread = 1 << 1
        };
        enum tRequestType {
            kShared,
            kExclusive,
            kYieldedTo
        };
        //--------------------------------------------------------------------
        //  Local Classes
        //--------------------------------------------------------------------
        class tEvent {
            public:
                tEvent(bool _autoReset, bool signalledInitially) : autoReset(_autoReset), signalled(signalledInitially) {
                }
                void acquireWithTimeout(int32_t timeoutMs) {
                    std::unique_lock<std::mutex> guard(signalLock);
                    while(!signalled) {
                        if(timeoutMs == -1) {
                            signalCond.wait(guard);
                        }
                        else {
                            auto result = signalCond.wait_for(guard, std::chrono::milliseconds(timeoutMs));
                            if(result == std::cv_status::timeout) {
                                RDMA_THROW(easyrdma_Error_Timeout);
                            }
                        }
                    }
                    if(autoReset) {
                        signalled = false;
                    }
                }
                void release() {
                    std::unique_lock<std::mutex> guard(signalLock);
                    signalled = true;
                    signalCond.notify_all();
                }
                void reset() {
                    std::unique_lock<std::mutex> guard(signalLock);
                    signalled = false;
                }
            private:
                bool autoReset;
                bool signalled;
                std::mutex signalLock;
                std::condition_variable signalCond;
        };

        class tRequest {
            public:
                //------------------------------------------------------------
                //  Constructors & Destructor
                //------------------------------------------------------------
                tRequest(std::thread::id tid, tRequestType type);
                ~tRequest();
                //------------------------------------------------------------
                //  New and delete
                //------------------------------------------------------------
                void* operator new(size_t size, tAccessManager* requestingManager);
                void operator delete(void*, tAccessManager*);
                //------------------------------------------------------------
                //  Methods
                //------------------------------------------------------------
                void Add(tRequestType type);
                bool RemoveLast();
                uint32_t Count() const;
                uint32_t CountExclusive() const;
                uint32_t CountShared() const;
                tRequest*& Next();
                std::thread::id GetTid() const;
                void WaitForSignal();
                void Signal();
                void InitSignal();
                void TermSignal();
                void DebugDump();
            private:
                tRequest(const tRequest&) = delete;
                tRequest& operator=(const tRequest&) = delete;
                //------------------------------------------------------------
                //  Data Members
                //------------------------------------------------------------
                std::thread::id tid;
                uint32_t shared;
                uint32_t exclusive;
                uint32_t nesting;
                bool yieldedTo;
                tEvent* signal;
                tRequest* next;
                //------------------------------------------------------------
                //  Undefined Canonicals
                //------------------------------------------------------------
                void* operator new(size_t size);
                void operator delete(void*);
        };
        class tRequestList {
            public:
                //------------------------------------------------------------
                //  Constructors & Destructor
                //------------------------------------------------------------
                tRequestList();
                ~tRequestList();
                //------------------------------------------------------------
                //  Methods
                //------------------------------------------------------------
                tRequest* RemoveHead();
                tRequest* Remove(std::thread::id tid);
                void AddAtHead(tRequest* request);
                void AddAtTail(tRequest* request);
                uint32_t Count() const;
                uint32_t CountExclusive() const;
                uint32_t CountShared() const;
                void DebugDump(const char*);
            private:
                //------------------------------------------------------------
                //  Data Members
                //------------------------------------------------------------
                tRequest* head;
                uint32_t shared;
                uint32_t exclusive;
        };
        //--------------------------------------------------------------------
        //  Data Members
        //--------------------------------------------------------------------
        mutable std::recursive_mutex criticalSection;
        std::atomic<uint32_t> refcount;
        tRequestList activeRequests;
        tRequestList pendingRequests;
        tRequestList suspendedRequests;
        tRequestList emptyRequests;
        tEvent allRefsReleased;
        //--------------------------------------------------------------------
        //  Private Functions
        //--------------------------------------------------------------------
        void GetSharedAccess();
        void GetExclusiveAccess();
        void SatisfyRequest(tRequest* request, tSatisfyFlags = static_cast<tSatisfyFlags>(0));
        tRequestList& GetEmptyRequestList();
        tRequest* MakeNewRequest();
        void DestroyRequest(tRequest* request);
        //--------------------------------------------------------------------
        //  Unused functions (implemented, but not yet used by AE codebase)
        //--------------------------------------------------------------------
        void ReleaseAndReacquireAtEnd();
        void YieldExclusive(std::thread::id yieldToTid);
};


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager Constructor
//
//  Description:
//      Builds a new access manager object.
//
//  Parameters:
//      object - The object that we're tracking access to.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tAccessManager() :
    refcount(0),
    allRefsReleased(false, true)
{
    //------------------------------------------------------------------------
    //  Make enough requests to satisfy our maximum number of concurrent threads
    //  constraint.
    //------------------------------------------------------------------------
    for (uint32_t i = 0; i < kInitialNumberOfRequestsInAccessManager; ++i) {
        tRequest* request = MakeNewRequest();
        if (!request) {
            //----------------------------------------------------------------
            //  Couldn't make the requests.  Clean up.
            //----------------------------------------------------------------
            tRequest* destroy;
            while ((destroy = emptyRequests.RemoveHead()) != NULL)
                DestroyRequest(destroy);
            return;
        }
        //--------------------------------------------------------------------
        //  Put the node on the list of empty (i.e. free) nodes.
        //--------------------------------------------------------------------
        emptyRequests.AddAtHead(request);
    }
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::~tAccessManager
//
//  Description:
//      Destructs a tAccessManager.
//
//////////////////////////////////////////////////////////////////////////////
inline tAccessManager::~tAccessManager() {
    //------------------------------------------------------------------------
    //  Go through the empty list and destroy the nodes.
    //------------------------------------------------------------------------
    assert(activeRequests.RemoveHead() == NULL);
    assert(pendingRequests.RemoveHead() == NULL);
    assert(suspendedRequests.RemoveHead() == NULL);
    tRequest* destroy;
    while ((destroy = emptyRequests.RemoveHead()) != NULL)
        DestroyRequest(destroy);
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::Acquire
//
//  Description:
//      Adds a single shared reference to the entity that we're managing access
//      to.  All Acquire calls must be eventually followed by a Release.
//      Nesting Acquires is legal.
//
//  Parameters:
//      exclusive - If true, we'll acquire exclusive access; otherwise, we
//                  acquire shared access.
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::Acquire(bool exclusive) {
    IncRef();
    if (exclusive) {
        GetExclusiveAccess();
    }
    else {
        GetSharedAccess();
    }
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::Release
//
//  Description:
//      Releases a single shared reference to the entity that we're managing
//      access to.  You are required to have called Acquire prior to releasing
//      the reference.
//
//  Return Value:
//      True if the access that was release is exclusive, false if shared.
//
//////////////////////////////////////////////////////////////////////////////
inline bool tAccessManager::Release() {
    bool exclusive;
    //------------------------------------------------------------------------
    //  Remove us from the active request list.  Remove our last request.
    //------------------------------------------------------------------------
    {
        std::lock_guard<std::recursive_mutex> lock(criticalSection);
        std::thread::id ourTid = std::this_thread::get_id();
        DebugAssertActive(ourTid);
        tRequest* ourRequest = activeRequests.Remove(ourTid);
        exclusive = ourRequest->RemoveLast();
        //--------------------------------------------------------------------
        //  If we still have requests, we need to go back into the list, otherwise
        //  we can delete our request object.  Either way, another thread might
        //  be able to run now, so try to satisfy the head of the list.
        //--------------------------------------------------------------------
        if (ourRequest->Count())
            activeRequests.AddAtHead(ourRequest);
        else
            tRequest::operator delete(ourRequest, this);
        SatisfyRequest(pendingRequests.RemoveHead(), static_cast<tSatisfyFlags>(kHighPriority | kDifferentThread));
    }
    //------------------------------------------------------------------------
    //  Lastly, decrement our refcount.  This will handle the object auto-
    //  deletion if necessary.
    //------------------------------------------------------------------------
    DecRef();
    return exclusive;
}

//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::AcquireAll
//
//  Description:
//      Aquire access to the entity we're managing in the same order as in the
//      access stack.  This is designed to use the stack created in the
//      ReleaseAll() method.
//
//  Parameters:
//      accessStack - The stack that has access order to acquire.
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::AcquireAll(const tAccessStack& accessStack) {
    assert(accessStack.size > 0);
    assert(accessStack.size <= (sizeof(accessStack.val) / sizeof(*accessStack.val)));
    for(size_t i = accessStack.size; i > 0; --i) {
        Acquire(accessStack.val[i-1]);
    }
}

//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::ReleaseAll
//
//  Description:
//      Releases all access to the entity that we're managing and add the
//      order of access to the accessStack parameter.
//
//  Parameters:
//      accessStack - The order of accesses prior to releasing them all.
//
//////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tAccessStack tAccessManager::ReleaseAll() {
    tAccessStack accessStack;
    accessStack.size = 0;
    std::fill(accessStack.val, (accessStack.val + sizeof(accessStack.val)), false);
    while (HasExclusiveAccess() || HasSharedAccess()) {
        assert(accessStack.size < sizeof(accessStack.val));
        accessStack.val[accessStack.size++] = Release();
    }
    return accessStack;
}

//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::WaitForAllReferencesToBeReleased
//
//  Description:
//      Waits until all references have been released. This does not guarantee
//      no new access are added afterwards---the caller must manage that.
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::WaitForAllReferencesToBeReleased() {
    WaitForAllReferencesToBeReleasedWithTimeout(-1);
}

//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::WaitForAllReferencesToBeReleasedWithTimeout
//
//  Description:
//      Waits until all references have been released. This does not guarantee
//      no new access are added afterwards---the caller must manage that.
//      This is expected to be used for debugging/test purposes or called by
//      WaitForAllReferencesToBeReleased() with an infinite timeout.
//
//  Parameters:
//      debugTimeoutMs  - timeout in ms or -1
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::WaitForAllReferencesToBeReleasedWithTimeout(int32_t timeout) {
    allRefsReleased.acquireWithTimeout(timeout);
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:ReleaseAndReacquireAtEnd
//
//  Description:
//      Atomically releases our access and moves ourselves to the back of the
//      line to acquire a new access.  We'll acquire the same type of access
//      that we originally had (shared vs. exclusive).  It's likely very unusual
//      that you'd ever need to use this interface.  The two instances where
//      we need to use it today is this:
//
//      1.  - The UMI(A) thread wakes up and yields to an endpoint state change
//            request(B) which creates another EP state change request(C). C will
//            then suspend and wait to be signaled by A. At this point B will resume
//            and it needs A to complete it's interation before it can perform it's work.
//            However, we need to guarentee that B always complete before C.  So when B
//            releases, it needs to ensure that C can never "jump ahead" of B to perform
//            the state change.
//
//       2.  When we are preparing the stream for destruction, we need to
//           ensure that there are no other threads that tried to grab access to the
//           stream after we removed it from the stream table. In practice, this
//           only happens during the EP state change request when it releases
//           and reacquires w/o going through the stream table. This will flush
//           all the pending access to the stream and allow them to complete
//           before we finish tearing down the stream.
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::ReleaseAndReacquireAtEnd() {
    //------------------------------------------------------------------------
    //  The caller expects us to release our access and reacquire access after
    //  adding ourselves to the end of the pending requests list.  If the
    //  pending requests list is empty, however, then there's no one else to
    //  give access to, so we just retain access and return.  Otherwise, if
    //  there are other requests in the pending list, then we'll need to go to
    //  the back of the line, so first remove ourselves from the active list.
    //------------------------------------------------------------------------
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    DebugAssertActive(ourTid);
    if (pendingRequests.Count() == 0)
        return;
    tRequest* ourRequest = activeRequests.Remove(ourTid);
    //------------------------------------------------------------------------
    //  Now simply try to satisfy our request again.  Because we're not identifying
    //  ourselves as high priority, this will put us at the back of the pending
    //  requests list and will block this thread until we get access.  This
    //  function will automatically try to satisfy the request at the head of
    //  the line.
    //------------------------------------------------------------------------
    SatisfyRequest(ourRequest);
    DebugAssertActive(ourTid);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::SuspendAccess
//
//  Description:
//      Removes all accesses by the current thread.  Places them off to the
//      side where they can be restored later.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::SuspendAccess() {
    //------------------------------------------------------------------------
    //  Remove our entity from the active request list.
    //------------------------------------------------------------------------
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    DebugAssertActive(ourTid);
    tRequest* ourRequest = activeRequests.Remove(ourTid);
    //------------------------------------------------------------------------
    //  This is weird.  If we were yielded to, but we didn't ask for exclusive
    //  access, we need to purge the temporary exclusive access we were given.
    //------------------------------------------------------------------------
    if (ourRequest->CountExclusive()) {
        ourRequest->Add(kExclusive);
        ourRequest->RemoveLast();
    }
    //------------------------------------------------------------------------
    //  Now, put our request on the suspended list, and satisfy the head of
    //  the pending list.
    //------------------------------------------------------------------------
    suspendedRequests.AddAtHead(ourRequest);
    SatisfyRequest(pendingRequests.RemoveHead(), static_cast<tSatisfyFlags>(kHighPriority | kDifferentThread));
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::ResumeAccess
//
//  Description:
//     Restores the access state by the calling thread.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::ResumeAccess() {
    //------------------------------------------------------------------------
    //  Find us in the list of suspended access, then satsify our request.
    //------------------------------------------------------------------------
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    tRequest* ourRequest = suspendedRequests.Remove(ourTid);
    if (ourRequest) {
        SatisfyRequest(ourRequest);
        DebugAssertActive(ourTid);
    }
    else {
        //--------------------------------------------------------------------
        //  Well, we weren't suspended.  If we were yielded to while suspended
        //  then we became active without knowing it.
        //--------------------------------------------------------------------
        DebugAssertActive(ourTid);
    }
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::YieldExclusive
//
//  Description:
//      Yields exclusive access to a particular thread.
//
//  Parameters:
//      yieldTid - The thread to yield to.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::YieldExclusive(std::thread::id yieldTid) {
    //------------------------------------------------------------------------
    //  We must already have exclusive access to do this.  Get our request out
    //  of the active requests.  It will be the only one (that's what exclusive
    //  means after all!).
    //------------------------------------------------------------------------
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    tRequest* ourRequest = activeRequests.Remove(ourTid);
    assert(ourRequest && ourRequest->CountExclusive());
    //------------------------------------------------------------------------
    //  See if the thread to which we are yielding is pending.  We can add a
    //  yielded to request, and try to satisfy the request (it will always
    //  work, since nothing else has access.  We satisfy it to signal its
    //  event).
    //------------------------------------------------------------------------
    tRequest* yieldToRequest;
    if ((yieldToRequest = pendingRequests.Remove(yieldTid)) != NULL) {
        yieldToRequest->Add(kYieldedTo);
        SatisfyRequest(yieldToRequest, static_cast<tSatisfyFlags>(kDifferentThread | kHighPriority));
    }
    //------------------------------------------------------------------------
    //  See if the thread is suspended.  If so, make it active by moving it
    //  to the active list.  We don't want to call satisfy on it because that
    //  would fire the event, but the thread isn't waiting on the event.
    //  If the resumed request had an exculsive access, life is good.  Otherwise,
    //  we need to hook it up with one.
    //------------------------------------------------------------------------
    else if ((yieldToRequest = suspendedRequests.Remove(yieldTid)) != NULL) {
        if (!yieldToRequest->CountExclusive())
            yieldToRequest->Add(kYieldedTo);
        activeRequests.AddAtHead(yieldToRequest);
    }
    else {
        //--------------------------------------------------------------------
        //  No request for it yet.  Make one with a yielded to request.
        //--------------------------------------------------------------------
        yieldToRequest = new (this) tRequest(yieldTid, kYieldedTo);
        activeRequests.AddAtHead(yieldToRequest);
    }
    //------------------------------------------------------------------------
    //  Try to satisfy the request for the yielding thread.  It will fail,
    //  since the yielded to thread has exclusive access, but doing so puts
    //  us at the the head of the line.
    //------------------------------------------------------------------------
    SatisfyRequest(ourRequest, kHighPriority);
    DebugAssertActive(ourTid);
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::IncRef
//
//  Description:
//      Increments our internal reference count to our associated object.
//      Note that this should generally only be used internally by this class.
//      The one exception is any external code that's generally tasked with
//      looking up these objects in a table.  In this case, we usually have a
//      table lock, but we generally don't want to hold the table lock while
//      waiting to acquire access to our object (which could cause deadlock).
//      At the same time, the table look-up code doesn't want the object to
//      go away from underneath it after releasing the table lock but before
//      it acquires a reference to it.  Exposing this method allows this
//      look-up code to mark that a reference is "pending" while it still holds
//      it's own table lock, thus preventing the object from inadvertently
//      going away.
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::IncRef() {
    if(++refcount == 1) {
        allRefsReleased.reset();
    }
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::DecRef
//
//  Description:
//      Decrements the internal reference count to our associated object.
//      This method *MUST* be called for *EVERY* call to IncRef.
//
//////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::DecRef() {
    //------------------------------------------------------------------------
    //  Decrement the refcount.
    //------------------------------------------------------------------------
    assert(refcount != 0);
    if(--refcount == 0) {
        allRefsReleased.release();
    }
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::DebugAssertActive
//
//  Description:
//      A debug only function that asserts the given thread id has an active
//      request.
//
//  Parameters:
//      tid - The thread id to test.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::DebugAssertActive(std::thread::id tid) {
    #ifdef _DEBUG
        std::lock_guard<std::recursive_mutex> lock(criticalSection);
        tRequest* request =  activeRequests.Remove(tid);
        if (!request) {
            TRACE("Thread should have been active, but wasn't");
            DebugDump();
            assert(0);
        }
        activeRequests.AddAtHead(request);
    #endif
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::DebugDump
//
//  Description:
//     Dumps out information about the manager to the debugger.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::DebugDump() {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    TRACE("Status of access manager @ %p\n", this);
    activeRequests.DebugDump("active");
    pendingRequests.DebugDump("pending");
    suspendedRequests.DebugDump("suspended");
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::HasExclusiveAccess
//
//  Description:
//      Returns true if the calling thread has exclusive access; false otherwise.
//
//  Parameters:
//      none
//
//  Return Value:
//      see above
//
//////////////////////////////////////////////////////////////////////////////
inline bool tAccessManager::HasExclusiveAccess() {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    //------------------------------------------------------------------------
    //  First see if we have an existing active request.  If not, we definitely
    //  don't have exclusive access.
    //------------------------------------------------------------------------
    tRequest* request = activeRequests.Remove(ourTid);
    if (!request) {
        return false;
    }
    //------------------------------------------------------------------------
    //  We have an active request.  Re-add ourselves back to the list.  If the
    //  exclusive count is non-zero, then by definition we have exclusive
    //  access.
    //------------------------------------------------------------------------
    activeRequests.AddAtHead(request);
    return activeRequests.CountExclusive() > 0;
}


//////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::HasSharedAccess
//
//  Description:
//      Returns true if the calling thread has shared access; false otherwise.
//
//  Parameters:
//      none
//
//  Return Value:
//      see above
//
//////////////////////////////////////////////////////////////////////////////
inline bool tAccessManager::HasSharedAccess() {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    //------------------------------------------------------------------------
    //  First see if we have an existing active request.  If not, we definitely
    //  don't have shared access.
    //------------------------------------------------------------------------
    tRequest* request = activeRequests.Remove(ourTid);
    if (!request) {
        return false;
    }
    //------------------------------------------------------------------------
    //  We have an active request.  Re-add ourselves back to the list.  If the
    //  exclusive count is zero and the shared count is non-zero, then we know
    //  we have shared access.
    //------------------------------------------------------------------------
    activeRequests.AddAtHead(request);
    return (activeRequests.CountExclusive() == 0) && (activeRequests.CountShared() > 0);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::GetSharedAccess
//
//  Description:
//      Adds a reference that assumes the execution state of the associated board
//      isn't going to change.
//
//  Parameters:
//
//  Return Value:
//      The new number of accesses by this thread.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::GetSharedAccess() {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    //------------------------------------------------------------------------
    //  First see if we have an existing active request.  If we have one,
    //  we can just add a shared onto it (since shared is always valid) and
    //  return.
    //------------------------------------------------------------------------
    tRequest* ourRequest = activeRequests.Remove(ourTid);
    if (ourRequest) {
        ourRequest->Add(kShared);
        activeRequests.AddAtHead(ourRequest);
        return;
    }
    //------------------------------------------------------------------------
    //  We didn't have an existing request object.  (The rules of the class don't
    //  allow for calling this with a suspended request, and if we had a pending
    //  request, we'd be suspended).  Make a new one, then ask to have it satisfied.
    //------------------------------------------------------------------------
    ourRequest = new (this) tRequest(ourTid, kShared);
    SatisfyRequest(ourRequest);
    DebugAssertActive(ourTid);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::GetExclusiveAccess
//
//  Description:
//      Adds a reference that assumes the execution state of the associated board
//      isn't going to change.
//
//  Parameters:
//      optimize - If true, the caller is absolutely sure the calling thread
//                 has at least one current access reference.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::GetExclusiveAccess() {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    std::thread::id ourTid = std::this_thread::get_id();
    //------------------------------------------------------------------------
    //  First see if we have an existing active request.
    //------------------------------------------------------------------------
    tRequest* ourRequest = activeRequests.Remove(ourTid);
    if (ourRequest) {
        //--------------------------------------------------------------------
        //  We had one.  If it had exclusive already, just increment the
        //  exclusive count, and reinstall it on the active list.
        //--------------------------------------------------------------------
        if (ourRequest->CountExclusive()) {
            ourRequest->Add(kExclusive);
            activeRequests.AddAtHead(ourRequest);
            return;
        }
        //--------------------------------------------------------------------
        //  We didn't already have exclusive.  Add on an exclusive access.
        //--------------------------------------------------------------------
        ourRequest->Add(kExclusive);
    }
    else {
        //--------------------------------------------------------------------
        //  There wasn't a request yet.  Make one, with exclusive access.
        //--------------------------------------------------------------------
        ourRequest = new (this) tRequest(ourTid, kExclusive);
    }
    //------------------------------------------------------------------------
    //  Satisfy the updated request.
    //------------------------------------------------------------------------
    SatisfyRequest(ourRequest);
    DebugAssertActive(ourTid);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::SatisfyRequest
//
//  Description:
//      Attempts to satisfy the given request.  If it cannot be immediately
//      satisfied, it is placed on the pending requests list.
//      NOTE:  The calling thread must have locked the critical section before
//      calling this.
//
//  Parameters:
//      request - The request to try to satsify.
//      flags   - Flags describing how to handle the request.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::SatisfyRequest(tRequest* request, tSatisfyFlags flags) {
    //------------------------------------------------------------------------
    //  If there is no request, do nothing and like it.
    //------------------------------------------------------------------------
    if (!request)
        return;
    //------------------------------------------------------------------------
    //  Determine if the request can be instantly satisfied.  The criteria:
    //  1.  If this request is low priority, there are no pending requests.
    //  2.  No other thread has exclusive access.
    //  3.  If this request is for exclusive, there are no active accesses.
    //------------------------------------------------------------------------
    bool canBeSatisfied = true;
    canBeSatisfied = canBeSatisfied && !(!(flags & kHighPriority) && pendingRequests.Count());
    canBeSatisfied = canBeSatisfied && !activeRequests.CountExclusive();
    canBeSatisfied = canBeSatisfied && !(request->CountExclusive() && activeRequests.Count());
    //------------------------------------------------------------------------
    //  If the request cannot be satisifed,  enqueue the request on the pending
    //  list.
    //------------------------------------------------------------------------
    if (!canBeSatisfied) {
        //--------------------------------------------------------------------
        //  Put it in the right spot, based on the priority.  If the priority
        //  was low, we should check the next item in the list.  It won't be
        //  this thread, and it should get high priority so it doesn't lose
        //  its place in line if it can't be satisfied.
        //--------------------------------------------------------------------
        if (flags & kHighPriority)
            pendingRequests.AddAtHead(request);
        else {
            SatisfyRequest(pendingRequests.RemoveHead(), static_cast<tSatisfyFlags>(kHighPriority | kDifferentThread));
            pendingRequests.AddAtTail(request);
        }
        //--------------------------------------------------------------------
        //  If the request was for a different thread, then that thread is
        //  already suspended, so we can just leave.  Otherwise, this thread
        //  needs to wait.
        //--------------------------------------------------------------------
        if (flags & kDifferentThread)
            return;
        criticalSection.unlock();
        request->WaitForSignal();
        criticalSection.lock();
        DebugAssertActive(request->GetTid());
        SatisfyRequest(pendingRequests.RemoveHead(), static_cast<tSatisfyFlags>(kHighPriority | kDifferentThread));
        return;
    }
    else {
        //--------------------------------------------------------------------
        //  The request can be satisfied.  If it was for the calling thread,
        //  just put the request on the active list and return.  If it was
        //  for a different thread, put the request on the active list, then
        //  wake the thread.
        //--------------------------------------------------------------------
        activeRequests.AddAtHead(request);
        if (flags & kDifferentThread) {
            request->Signal();
        }
        return;
    }
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::GetEmptyRequestList
//
//  Description:
//      Returns the empty request list.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequestList& tAccessManager::GetEmptyRequestList() {
    return emptyRequests;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::MakeNewRequest
//
//  Description:
//      Allocates a new request object.
//
//  Parameters:
//      status - Status parameter.
//
//  Return Value:
//      The new request.  NULL on failure.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequest* tAccessManager::MakeNewRequest() {
    //------------------------------------------------------------------------
    //  Allocate space for it, returning a bad status if the allocation fails.
    //------------------------------------------------------------------------
    uint8_t* memory = new uint8_t[sizeof(tRequest)];
    //------------------------------------------------------------------------
    // Initialize the sync object.  We do this at preallocation time instead
    // of in the constructor, because we want the sync object to persist across
    // usages of the node.
    //------------------------------------------------------------------------
    tAccessManager::tRequest* request = reinterpret_cast<tRequest*>(memory);
    try {
        request->InitSignal();
    }
    catch(std::exception&) {
        delete[] memory;
        request = NULL;
    }
    return request;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::DestroyRequest
//
//  Description:
//      Destroys memory associated with a request.  Note that delete should have
//      been called on the request already.
//
//  Parameters:
//      request - The request to destroy.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::DestroyRequest(tRequest* request) {
    //------------------------------------------------------------------------
    //  Get rid of the signal object created in MakeNewRequest() and free
    //  the memory.
    //------------------------------------------------------------------------
    request->TermSignal();
    delete[] reinterpret_cast<uint8_t*>(request);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:DebugGetRefCount/DebugGetActiveCount
//              DebugGetActiveSharedCount/DebugGetActiveExclusiveCount
//
//  Description:
//      Returns counters for testing
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::DebugGetRefCount() const {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    return refcount;
}
inline uint32_t tAccessManager::DebugGetActiveCount() const {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    return activeRequests.Count();
}
inline uint32_t tAccessManager::DebugGetActiveSharedCount() const {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    return activeRequests.CountShared();
}
inline uint32_t tAccessManager::DebugGetActiveExclusiveCount() const {
    std::lock_guard<std::recursive_mutex> lock(criticalSection);
    return activeRequests.CountExclusive();
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:tRequest::tRequest
//
//  Description:
//      Builds a new access manager request object.
//
//  Parameters:
//      tid  - The thread id of the associated thread.
//      type - The type of the first request.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequest::tRequest(std::thread::id _tid, tRequestType type) {
    tid       = _tid;
    shared    = exclusive = 0;
    yieldedTo = false;
    next      = NULL;
    Add(type);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest destructor
//
//  Description:
//      Destructs a request object.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequest::~tRequest() {
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::new
//
//  Description:
//      Allocates a new request object.  NOTE:  The caller must have the critical
//      section of the requesting manager.
//
//  Parameters:
//      size    - unused
//      manager - The access manager making the request.
//
//  Return Value:
//      The new object.
//
////////////////////////////////////////////////////////////////////////////////
inline void* tAccessManager::tRequest::operator new(size_t, tAccessManager* manager) {
    //------------------------------------------------------------------------
    //  Get the list.  We'll take the top of the list.
    //------------------------------------------------------------------------
    tAccessManager::tRequestList& list = manager->GetEmptyRequestList();
    tRequest* newedRequest = list.RemoveHead();
    if (!newedRequest) {
        //--------------------------------------------------------------------
        //  We ran out of objects so we need to make more at a cost of some
        //  jitter to the user. Assume MakeNewRequest() handles error cases.
        //--------------------------------------------------------------------
        newedRequest = manager->MakeNewRequest();
    }
    return newedRequest;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::delete
//
//  Description:
//      "Deletes" a request object.  NOTE:  The caller must have the critical
//      section of the requesting manager.
//
//  Parameters:
//      request - The request to delete.
//      manager - The calling manager.
//
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::operator delete(void* _request, tAccessManager* manager) {
    //------------------------------------------------------------------------
    //  To "delete" a request, just add it to the empty list.
    //------------------------------------------------------------------------
    tRequest* request = reinterpret_cast<tRequest*>(_request);
    tAccessManager::tRequestList& list = manager->GetEmptyRequestList();
    list.AddAtHead(request);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::Add
//
//  Description:
//      Adds a new request entry to the request.
//
//  Parameters:
//      type - The type of the request to add.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::Add(tRequestType type) {
    //------------------------------------------------------------------------
    //  Increment the count for the type.
    //------------------------------------------------------------------------
    switch (type) {
        case kShared:
            //----------------------------------------------------------------
            //  kShared:  Just increment the shared type and mark the nesting
            //  tracker with a 1, meaning shared.
            //----------------------------------------------------------------
            {
                assert((shared + exclusive) < 32);
                ++shared;
                nesting = (nesting << 1) | 1;
                break;
            }
        case kYieldedTo:
            //----------------------------------------------------------------
            //  Yielded to.  If there is already exclusive access, do nothing.
            //  Otherwise, fall through to exclusive.
            //----------------------------------------------------------------
            if (exclusive)
                break;
        case kExclusive:
            //----------------------------------------------------------------
            //  kExclusive.  If yielded to is set, then we can this request
            //  was handled by it, so do nothing.  Otherwise, increment
            //  the exclusive count and mark the nesting tracker with a 0,
            //  meaning exclusive.  If this fell through from above, mark
            //  yielded to
            //----------------------------------------------------------------
            if (yieldedTo) {
                yieldedTo = false;
                break;
            }
            else {
                assert((shared + exclusive) < 32);
                ++exclusive;
                nesting = (nesting << 1) | 0;
                yieldedTo = type == kYieldedTo;
                break;
            }
    }
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::RemoveLast
//
//  Description:
//      Removes the last request.
//
//  Return Value:
//      True if the request was exclusive, false if shared.
//
////////////////////////////////////////////////////////////////////////////////
inline bool tAccessManager::tRequest::RemoveLast() {
    assert(!yieldedTo);
    bool isExclusive;
    //------------------------------------------------------------------------
    //  Remove the count for the last request.  The nesting variable keeps
    //  track of such things for us.
    //------------------------------------------------------------------------
    if (nesting & 1) {
        --shared;
        isExclusive = false;
    }
    else {
        --exclusive;
        isExclusive = true;
    }
    nesting >>= 1;
    return isExclusive;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::Count
//
//  Description:
//      Returns the number of accesses this request encompasses.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::tRequest::Count() const {
    return shared + exclusive;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::CountExclusive
//
//  Description:
//      Returns the number of exclusive accesses this request encompasses.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::tRequest::CountExclusive() const {
    return exclusive;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::CountShared
//
//  Description:
//      Returns the number of shared accesses this request encompasses.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::tRequest::CountShared() const {
    return shared;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:tRequest::Next
//
//  Description:
//      Returns a reference to our next pointer.
//
//  Return Value:
//     See above.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequest*& tAccessManager::tRequest::Next() {
    return next;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:tRequest::GetTid
//
//  Description:
//      Returns the thread identifier of the request.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline std::thread::id tAccessManager::tRequest::GetTid() const {
    return tid;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::WaitForSignal
//
//  Description:
//      Creates a synchronization object and waits until it is signalled.
//      NOTE:  Only the thread whose tid matches that of the request should call
//             this function.
//      NOTE:  CreateSignal() should have been called first.
//      NOTE:  Don't call this while holding the manager's critical section.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::WaitForSignal() {
    signal->acquireWithTimeout(-1);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::Signal
//
//  Description:
//      Signals the given request.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::Signal() {
    signal->release();
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::InitSignal
//
//  Description:
//      Initializes the signal object in the request.  This is only to be called
//      by the allocator.
//
//  Parameters:
//      status - Status parameter.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::InitSignal() {
    signal = NULL;
    //------------------------------------------------------------------------
    //  Allocate a new event object that auto-resets and is not signaled initially.
    //------------------------------------------------------------------------
    signal = new tEvent(true, false);
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::TermSignal
//
//  Description:
//      Destroys the signal object in the request.  This is only to be called
//      be the deallocator.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::TermSignal() {
    delete signal;
    signal = NULL;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:Request::DebugDump
//
//  Description:
//      Dumps out information about the request to the debugger.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequest::DebugDump() {
    std::stringstream tidStr;
    tidStr << tid;
    TRACE("tAccessManager: tid = %s sh = %d  ex = %d nesting = %08X  yieldedTo = %s\n", tidStr.str().c_str(), shared, exclusive, nesting, ((yieldedTo ? "true" : "false")));
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList Constructor
//
//  Description:
//      Builds a new request list.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequestList::tRequestList() {
    shared = exclusive = 0;
    head = NULL;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList Destructor
//
//  Description:
//      Destructs a request list.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequestList::~tRequestList() {
    //------------------------------------------------------------------------
    //  Cleaning up the list is the responsibility of the manager.
    //------------------------------------------------------------------------
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList::RemoveHead
//
//  Description:
//      Removes the head of the list and returns it.  If there was no head, the
//      function returns NULL.
//
//  Return Value:
//      The former head of the list.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequest* tAccessManager::tRequestList::RemoveHead() {
    //------------------------------------------------------------------------
    //  If there is actually a head, then decrement the counts and point
    //  head to whatever used to be second in the list.
    //------------------------------------------------------------------------
    tRequest* retval = head;
    if (retval) {
        head = retval->Next();
        shared -= retval->CountShared();
        exclusive -= retval->CountExclusive();
    }
    return retval;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList::Remove
//
//  Description:
//      Removes the entry with the given tid.
//
//  Return Value:
//      The entry.  NULL if the the tid was not in the list.
//
////////////////////////////////////////////////////////////////////////////////
inline tAccessManager::tRequest* tAccessManager::tRequestList::Remove(std::thread::id tid) {
    //------------------------------------------------------------------------
    //  Find the entry that has the tid, if it is there.
    //------------------------------------------------------------------------
    tRequest** cur  = &head;
    while (*cur && (*cur)->GetTid() != tid)
        cur = &((*cur)->Next());
    if (!*cur)
        return NULL;
    //------------------------------------------------------------------------
    //  Save off the pointer to the found node.  Set that which used to point
    //  to the found node to whatever the node points to.
    //------------------------------------------------------------------------
    tRequest* retval = *cur;
    *cur = retval->Next();
    shared -= retval->CountShared();
    exclusive -= retval->CountExclusive();
    return retval;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList::AddAtHead
//
//  Description:
//      Adds the given request to the head of the list.
//
//  Parameters:
//      request - The request to add.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequestList::AddAtHead(tRequest* request) {
    //------------------------------------------------------------------------
    //  Put us at the head and add in the counts.
    //------------------------------------------------------------------------
    request->Next() = head;
    head = request;
    shared += request->CountShared();
    exclusive += request->CountExclusive();
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList::AddAtTail
//
//  Description:
//      Adds the given request to the tail of the list.
//
//  Parameters:
//      request - The request to add.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequestList::AddAtTail(tRequest* request) {
    //------------------------------------------------------------------------
    //  Find the pointer to NULL
    //------------------------------------------------------------------------
    tRequest** cur = &head;
    while (*cur)
        cur = &((*cur)->Next());
    //------------------------------------------------------------------------
    //  Make that pointer point to the new node.  Make the new node point to
    //  NULL.  Add in the new node counts.
    //------------------------------------------------------------------------
    *cur = request;
    request->Next() = NULL;
    shared += request->CountShared();
    exclusive += request->CountExclusive();
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList::Count
//
//  Description:
//      Returns the number of accesses in this list.  Note that this is NOT the
//      number of requests.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::tRequestList::Count() const {
    return shared + exclusive;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequest::CountExclusive
//
//  Description:
//      Returns the number of exclusive accesses in this list.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::tRequestList::CountExclusive() const {
    return exclusive;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager::tRequestList::CountShared
//
//  Description:
//      Returns the number of shared in this list.
//
//  Return Value:
//      See above.
//
////////////////////////////////////////////////////////////////////////////////
inline uint32_t tAccessManager::tRequestList::CountShared() const {
    return shared;
}


////////////////////////////////////////////////////////////////////////////////
//
//  tAccessManager:RequestList::DebugDump
//
//  Description:
//      Dumps out information about the list to the debugger.
//
//  Parameters:
//      data - A string to prepend our data with.
//
////////////////////////////////////////////////////////////////////////////////
inline void tAccessManager::tRequestList::DebugDump(const char* data) {
    TRACE("tAccessManager::tRequestList: %s:  %dsh %dex\n", data, shared, exclusive);
    tRequest* cur = head;
    while (cur) {
        cur->DebugDump();
        cur = cur->Next();
    }
}

