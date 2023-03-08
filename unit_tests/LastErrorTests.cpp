// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

//============================================================================
//  Includes
//============================================================================
#include <gtest/gtest.h>
#include "api/errorhandling.h"
#include <thread>
#include "common/RdmaError.h"

namespace EasyRDMA
{

//////////////////////////////////////////////////////////////////////////////
//
//  Sanity
//
//  Description:
//     Tests basic functionality of LastError functionality
//
//////////////////////////////////////////////////////////////////////////////
TEST(LastError, Sanity)
{
    RdmaError retrievedStatus;
    GetLastRdmaError(retrievedStatus);
    EXPECT_EQ(retrievedStatus.GetCode(), 0);
    EXPECT_EQ(retrievedStatus.filename, (const char*)nullptr); // Initialize leaves it at null

    // The main thread should now have one allocated. When it is allocated is OS-dependent:
    //  -Windows allocates on thread creation (assuming DLL is loaded). Gtest may have spawned threads in some cases
    //  -Linux allocates on first use (hit above)
    int64_t startingLastErrors = DebugGetNumberOfAllocatedLastRdmaErrors();
    EXPECT_GT(startingLastErrors, 0);

    RdmaError testStatus;
    RDMA_SET_ERROR(testStatus, -500);
    PopulateLastRdmaError(testStatus);

    EXPECT_EQ(DebugGetNumberOfAllocatedLastRdmaErrors(), startingLastErrors);

    retrievedStatus.Clear();
    GetLastRdmaError(retrievedStatus);
    EXPECT_EQ(retrievedStatus.GetCode(), testStatus.GetCode());
    EXPECT_STREQ(retrievedStatus.filename, testStatus.filename);

    ClearLastRdmaError();
    retrievedStatus.Clear();
    GetLastRdmaError(retrievedStatus);
    EXPECT_EQ(retrievedStatus.GetCode(), 0);
    EXPECT_EQ(retrievedStatus.filename, (const char*)nullptr);

    EXPECT_EQ(DebugGetNumberOfAllocatedLastRdmaErrors(), startingLastErrors);
}

//////////////////////////////////////////////////////////////////////////////
//
//  Threaded
//
//  Description:
//     Tests threadlocal-specific functionality
//
//////////////////////////////////////////////////////////////////////////////
TEST(LastError, Threaded)
{
    RdmaError mainTestStatus;
    RDMA_SET_ERROR(mainTestStatus, -500);
    PopulateLastRdmaError(mainTestStatus);

    int64_t startingLastErrors = DebugGetNumberOfAllocatedLastRdmaErrors();
    EXPECT_GT(startingLastErrors, 0);

    auto asyncStatusSetter = std::thread([&] {
        RdmaError threadRetrievedStatus;
        GetLastRdmaError(threadRetrievedStatus);
        EXPECT_EQ(threadRetrievedStatus.GetCode(), 0);
        EXPECT_EQ(threadRetrievedStatus.filename, (const char*)nullptr);

        RdmaError threadTestStatus;
        RDMA_SET_ERROR(threadTestStatus, -600);
        PopulateLastRdmaError(threadTestStatus);

        GetLastRdmaError(threadRetrievedStatus);
        EXPECT_EQ(threadRetrievedStatus.GetCode(), threadTestStatus.GetCode());
        EXPECT_STREQ(threadTestStatus.filename, threadTestStatus.filename);

        EXPECT_EQ(DebugGetNumberOfAllocatedLastRdmaErrors(), startingLastErrors + 1);
    });
    EXPECT_NO_THROW(asyncStatusSetter.join());

    // Main thread should still have the old status
    RdmaError mainRetrievedStatus;
    GetLastRdmaError(mainRetrievedStatus);
    EXPECT_EQ(mainRetrievedStatus.GetCode(), mainTestStatus.GetCode());
    EXPECT_STREQ(mainRetrievedStatus.filename, mainTestStatus.filename);

    // Thread's stack error should have been destroyed
    EXPECT_EQ(DebugGetNumberOfAllocatedLastRdmaErrors(), startingLastErrors);
}

}; // namespace EasyRDMA