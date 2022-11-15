// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

//============================================================================
//  The code to be tested
//============================================================================
#include "core/api/tAccessManager.h"
#include "core/api/tAccessManagedRef.h"
#include "tests/utility/Utility.h"


//============================================================================
//  Includes
//============================================================================
#include <gtest/gtest.h>
#include "common/RdmaError.h"
#include <boost/thread/shared_mutex.hpp>
#include <vector>
#include <random>
#include <chrono>
#include <thread>


namespace EasyRDMA {

//////////////////////////////////////////////////////////////////////////////
//
//  Sanity Test
//
//  Description:
//      Checks basic usage in single-threaded fashion
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, Sanity) {
    tAccessManager accessManager;

    // Exclusive access
    accessManager.Acquire(true);
    EXPECT_EQ(1UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_TRUE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());

    // Shared access
    accessManager.Acquire(false);
    EXPECT_EQ(1UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_TRUE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());

    // Recursive shared
    accessManager.Acquire(false);
    accessManager.Acquire(false);
    EXPECT_EQ(2UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(2UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(2UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_TRUE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_TRUE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());

    // Recursive exclusive (works since it is the same thread)
    accessManager.Acquire(true);
    accessManager.Acquire(true);
    EXPECT_EQ(2UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(2UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(2UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_TRUE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_TRUE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());

    // Shared then promoting to exclusive
    accessManager.Acquire(false);
    EXPECT_EQ(1UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_TRUE(accessManager.HasSharedAccess());
    accessManager.Acquire(true);
    EXPECT_EQ(2UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(2UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_TRUE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_EQ(1UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(1UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_TRUE(accessManager.HasSharedAccess());
    accessManager.Release();
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(accessManager.HasExclusiveAccess());
    EXPECT_FALSE(accessManager.HasSharedAccess());
}


//////////////////////////////////////////////////////////////////////////////
//
//  AccessManagerExclusiveThreadTester
//
//  Description:
//     Thread for BasicThreading test
//
//////////////////////////////////////////////////////////////////////////////
void AccessManagerExclusiveThreadTester(tAccessManager* accessManager, int numLoops, int maxTimeMs, std::atomic<uint32_t>* contendedData, bool mixSharedAccess) {

    // Init random generator
    std::random_device randomdev;
    std::mt19937 gen(randomdev());

    for(int i=0; i < numLoops; ++i) {
        // Choose a random time interval from 1-maxTimeMs
        std::uniform_int_distribution<int> uniform_dist(1, maxTimeMs);
        int millisec = uniform_dist(gen);

        std::this_thread::sleep_for(std::chrono::milliseconds(millisec));

        // Figure out if we are acquiring shared access
        bool shared = mixSharedAccess ? (i % 2 == 0) : false;

        // Get access
        accessManager->Acquire(!shared);
        auto start = std::chrono::system_clock::now();
        while(1) {
            (*contendedData)++;
            if(shared) {
                // Don't know how many total haved shared access, just
                // that it must be >0. Nobody can have exclusive
                EXPECT_LT(0UL, accessManager->DebugGetActiveCount());
                EXPECT_EQ(0UL, accessManager->DebugGetActiveExclusiveCount());
                EXPECT_LT(0UL, accessManager->DebugGetActiveSharedCount());
                EXPECT_LT(0UL, *contendedData);
                EXPECT_FALSE(accessManager->HasExclusiveAccess());
                EXPECT_TRUE(accessManager->HasSharedAccess());
            }
            else {
                // Check that we are the only one with exclusive access
                EXPECT_EQ(1UL, accessManager->DebugGetActiveCount());
                EXPECT_EQ(1UL, accessManager->DebugGetActiveExclusiveCount());
                EXPECT_EQ(0UL, accessManager->DebugGetActiveSharedCount());
                EXPECT_EQ(1UL, *contendedData);
                EXPECT_TRUE(accessManager->HasExclusiveAccess());
                EXPECT_FALSE(accessManager->HasSharedAccess());
            }
            (*contendedData)--;
            auto millisecPassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start);
            // Keep polling for remaining time up to maxTimeMs
            if(millisecPassed.count() > maxTimeMs-millisec) {
                break;
            }
        }
        // Release access
        accessManager->Release();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
//  BasicThreading test
//
//  Description:
//     Spawns threads that each get exclusive access at random times and
//     validates exclusive sync as well as access manager counters
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, BasicThreading) {
    tAccessManager accessManager;

    const size_t numThreads = 8;
    const int numLoops = 5;
    const int maxTimeMs = 10;

    std::thread threads[numThreads];
    std::atomic<uint32_t> contendedData(0);
    for(size_t i=0; i < numThreads; ++i) {
        threads[i] = std::thread(AccessManagerExclusiveThreadTester, &accessManager, numLoops, maxTimeMs, &contendedData, false /* mixSharedAccess */);
    }
    for(size_t i=0; i < numThreads; ++i) {
        threads[i].join();
    }
    EXPECT_EQ(0UL, contendedData);
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
}

//////////////////////////////////////////////////////////////////////////////
//
//  AllRefsReleasedThread
//
//  Description:
//     Helper thread for AllRefsReleased test
//
//////////////////////////////////////////////////////////////////////////////
void AllRefsReleasedThread(tAccessManager* accessManager, uint32_t timeout, bool* sawEvent) {
    *sawEvent = false;
    EXPECT_NO_THROW(accessManager->WaitForAllReferencesToBeReleasedWithTimeout(timeout));
    *sawEvent = true;
}

//////////////////////////////////////////////////////////////////////////////
//
//  AllRefsReleased
//
//  Description:
//     Tests the AllRefsReleased event mechanism
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, AllRefsReleased) {
    tAccessManager accessManager;

    uint32_t timeout = 10;

    EXPECT_NO_THROW(accessManager.WaitForAllReferencesToBeReleasedWithTimeout(timeout));
    accessManager.Acquire(true);
    RDMA_EXPECT_THROW_WITHCODE(accessManager.WaitForAllReferencesToBeReleasedWithTimeout(timeout), easyrdma_Error_Timeout);
    accessManager.Release();
    EXPECT_NO_THROW(accessManager.WaitForAllReferencesToBeReleasedWithTimeout(timeout));
    accessManager.Acquire(false);
    accessManager.Acquire(true);
    RDMA_EXPECT_THROW_WITHCODE(accessManager.WaitForAllReferencesToBeReleasedWithTimeout(timeout), easyrdma_Error_Timeout);
    accessManager.Release();
    RDMA_EXPECT_THROW_WITHCODE(accessManager.WaitForAllReferencesToBeReleasedWithTimeout(timeout), easyrdma_Error_Timeout);
    accessManager.Release();
    EXPECT_NO_THROW(accessManager.WaitForAllReferencesToBeReleasedWithTimeout(timeout));

    // Try blocking/waking a thread
    accessManager.Acquire(true);
    bool sawEvent = false;
    std::thread thread(AllRefsReleasedThread, &accessManager, 1000, &sawEvent);
    EXPECT_EQ(sawEvent, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
    EXPECT_EQ(sawEvent, false);
    accessManager.Release();
    thread.join();
    EXPECT_EQ(sawEvent, true);
}

//////////////////////////////////////////////////////////////////////////////
//
//  SharedExclusiveThreading test
//
//  Description:
//     Spawns threads that each get exclusive access at random times and
//     validates exclusive sync as well as access manager counters
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, SharedExclusiveThreading) {
    tAccessManager accessManager;

    const size_t numThreads = 8;
    const int numLoops = 5;
    const int maxTimeMs = 10;

    std::thread threads[numThreads];
    std::atomic<uint32_t> contendedData(0);
    for(size_t i=0; i < numThreads; ++i) {
        threads[i] = std::thread(AccessManagerExclusiveThreadTester, &accessManager, numLoops, maxTimeMs, &contendedData, true /* mixSharedAccess */);
    }
    for(size_t i=0; i < numThreads; ++i) {
        threads[i].join();
    }
    EXPECT_EQ(0UL, contendedData);
    EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
}



//////////////////////////////////////////////////////////////////////////////
//
//  AutoRef test
//
//  Description:
//     Checks that the autoref class works as expected
//
//////////////////////////////////////////////////////////////////////////////
class AccessManagerOwner : public iAccessManaged {
public:
    AccessManagerOwner() { };
    virtual tAccessManager& GetAccessManager() { return accessManager; };
    tAccessManager accessManager;
};

TEST(AccessManager, AutoRef) {
    auto resource = std::make_shared<AccessManagerOwner>();

    // Shared
    {
        tAccessManagedRef<AccessManagerOwner> ref(resource, kAccess_Shared);
        EXPECT_EQ(1UL, resource->accessManager.DebugGetRefCount());
        EXPECT_EQ(1UL, resource->accessManager.DebugGetActiveCount());
        EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveExclusiveCount());
        EXPECT_EQ(1UL, resource->accessManager.DebugGetActiveSharedCount());
        EXPECT_FALSE(resource->accessManager.HasExclusiveAccess());
        EXPECT_TRUE(resource->accessManager.HasSharedAccess());
    }
    EXPECT_EQ(0UL, resource->accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(resource->accessManager.HasExclusiveAccess());
    EXPECT_FALSE(resource->accessManager.HasSharedAccess());

    // Exclusive
    {
        tAccessManagedRef<AccessManagerOwner> ref(resource, kAccess_Exclusive);
        EXPECT_EQ(1UL, resource->accessManager.DebugGetRefCount());
        EXPECT_EQ(1UL, resource->accessManager.DebugGetActiveCount());
        EXPECT_EQ(1UL, resource->accessManager.DebugGetActiveExclusiveCount());
        EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveSharedCount());
        EXPECT_TRUE(resource->accessManager.HasExclusiveAccess());
        EXPECT_FALSE(resource->accessManager.HasSharedAccess());
    }
    EXPECT_EQ(0UL, resource->accessManager.DebugGetRefCount());
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveCount());
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveExclusiveCount());
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(resource->accessManager.HasExclusiveAccess());
    EXPECT_FALSE(resource->accessManager.HasSharedAccess());

    // Copy CTOR
    {
        tAccessManagedRef<AccessManagerOwner> ref(resource, kAccess_Shared);
        EXPECT_EQ(1UL, resource->accessManager.DebugGetActiveSharedCount());
        EXPECT_FALSE(resource->accessManager.HasExclusiveAccess());
        EXPECT_TRUE(resource->accessManager.HasSharedAccess());
        tAccessManagedRef<AccessManagerOwner> ref2 = ref;
        EXPECT_EQ(2UL, resource->accessManager.DebugGetActiveSharedCount());
        EXPECT_FALSE(resource->accessManager.HasExclusiveAccess());
        EXPECT_TRUE(resource->accessManager.HasSharedAccess());
    }
    EXPECT_EQ(0UL, resource->accessManager.DebugGetActiveSharedCount());
    EXPECT_FALSE(resource->accessManager.HasExclusiveAccess());
    EXPECT_FALSE(resource->accessManager.HasSharedAccess());
}

//////////////////////////////////////////////////////////////////////////////
//
//  ReleaseReturn test
//
//  Description:
//     Checks if Release is returning the access in the correct order
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, ReleaseReturn) {
    tAccessManager accessManager;

     const std::vector<std::vector<bool>> releaseTestCases{
        {true},
        {false},
        {true, true, false, true},
        {true, true, false, true, false, false}
    };

    for(auto testCase : releaseTestCases) {
        // Acquire the proper access
        for(auto exclusive : testCase) {
            accessManager.Acquire(exclusive);
        }
        EXPECT_EQ(testCase.size(), accessManager.DebugGetRefCount());
        for(size_t i = testCase.size(); i > 0; --i) {
            EXPECT_EQ(testCase[i-1], accessManager.Release());
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
//
//  AccessAcquireAll test
//
//  Description:
//     Tests AcquireAll method without using RequestAll
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, AccessAcquireAll) {
    tAccessManager accessManager;

    const std::vector<std::vector<bool>> acquireTestCases{
        {true},
        {false},
        {true, false},
        {false, true},
        {true, true, false, true}
    };

    for(auto testCase : acquireTestCases) {
        tAccessManager::tAccessStack accessStack;
        accessStack.size = 0;
        std::fill(accessStack.val, (accessStack.val + sizeof(accessStack.val)), false);

        for(auto exclusive : testCase) {
            accessStack.val[accessStack.size++] = exclusive;
        }
        accessManager.AcquireAll(accessStack);
        EXPECT_EQ(testCase.size(), accessManager.DebugGetRefCount());
        for(size_t i = 0; i < testCase.size(); ++i) {
            EXPECT_EQ(testCase[i], accessManager.Release());
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
//
//  AccessReleaseAll test
//
//  Description:
//     Tests ReleaseAll method without using AcquireAll
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, AccessReleaseAll) {
    tAccessManager accessManager;

    const std::vector<std::vector<bool>> releaseTestCases{
        {},
        {true},
        {false},
        {true, false},
        {false, true},
        {true, true, false, true}
    };

    for(auto testCase : releaseTestCases) {
        tAccessManager::tAccessStack accessStack;

        // Acquire access according to test case.
        for(auto exclusive : testCase) {
            accessManager.Acquire(exclusive);
        }
        accessStack = accessManager.ReleaseAll();
        EXPECT_EQ(testCase.size(), accessStack.size);
        // The stack should be flipped, check if they are equal
        for(size_t i = 0; i < accessStack.size; ++i) {
            EXPECT_EQ(testCase[testCase.size()-i-1], accessStack.val[i]);
        }
        EXPECT_EQ(0UL, accessManager.DebugGetRefCount());
        EXPECT_EQ(0UL, accessManager.DebugGetActiveCount());
        EXPECT_EQ(0UL, accessManager.DebugGetActiveExclusiveCount());
        EXPECT_EQ(0UL, accessManager.DebugGetActiveSharedCount());
        EXPECT_FALSE(accessManager.HasExclusiveAccess());
        EXPECT_FALSE(accessManager.HasSharedAccess());
    }
}

//////////////////////////////////////////////////////////////////////////////
//
//  ReleaseAcquireAll test
//
//  Description:
//     Tests using Release and Acquire all together
//
//////////////////////////////////////////////////////////////////////////////
TEST(AccessManager, ReleaseAcquireAll) {
    tAccessManager accessManager;

    const std::vector<std::vector<bool>> accessTestCases{
        {true},
        {false},
        {true, false},
        {false, true},
        {true, true, false, true}
    };

    for(auto testCase : accessTestCases) {
        tAccessManager::tAccessStack accessStack;

        // Acquire access according to test case.
        for(auto exclusive : testCase) {
            accessManager.Acquire(exclusive);
        }
        accessStack = accessManager.ReleaseAll();
        EXPECT_EQ(testCase.size(), accessStack.size);
        accessManager.AcquireAll(accessStack);
        // The stack should be flipped, check if they are equal
        for(size_t i = testCase.size(); i > 0; --i) {
            EXPECT_EQ(testCase[i-1], accessManager.Release());
        }
    }
}

} // namespace EasyRDMA
