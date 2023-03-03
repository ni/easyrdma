// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

//============================================================================
//  Includes
//============================================================================
#include <gtest/gtest.h>
#include "linux/FdPoller.h"
#include <memory>
#include <thread>
#include <future>
#include "tests/utility/Utility.h"

namespace EasyRDMA
{

//////////////////////////////////////////////////////////////////////////////
//
//  Sanity
//
//  Description:
//     Tests creating a poll
//
//////////////////////////////////////////////////////////////////////////////
TEST(Poll, Sanity)
{
    std::unique_ptr<FdPoller> poller;
    RDMA_ASSERT_NO_THROW(poller.reset(new FdPoller()));
}

//////////////////////////////////////////////////////////////////////////////
//
//  Blocks
//
//  Description:
//     Tests blocking wait on poll
//
//////////////////////////////////////////////////////////////////////////////
TEST(Poll, Blocks)
{
    int pipeFds[2];
    ASSERT_NE(pipe(pipeFds), -1);

    std::unique_ptr<FdPoller> poller;
    RDMA_ASSERT_NO_THROW(poller.reset(new FdPoller()));

    auto Poll = [&](int32_t millisec) {
        return poller->PollOnFd(pipeFds[0], millisec);
    };
    auto start = std::chrono::steady_clock::now();
    auto waiter = std::async(std::launch::async, Poll, 50);
    RDMA_ASSERT_NO_THROW(EXPECT_FALSE(waiter.get()));
    ASSERT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(50));
    ASSERT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    close(pipeFds[0]);
    close(pipeFds[1]);
}

//////////////////////////////////////////////////////////////////////////////
//
//  Unblocks
//
//  Description:
//     Tests that si
//
//////////////////////////////////////////////////////////////////////////////
TEST(Poll, PollSucceeds)
{
    int pipeFds[2];
    ASSERT_NE(pipe(pipeFds), -1);

    std::unique_ptr<FdPoller> poller;
    RDMA_ASSERT_NO_THROW(poller.reset(new FdPoller()));

    auto Poll = [&](int32_t millisec) {
        return poller->PollOnFd(pipeFds[0], millisec);
    };
    auto start = std::chrono::steady_clock::now();
    auto waiter = std::async(std::launch::async, Poll, 500);
    (void)!write(pipeFds[1], " ", 1);
    RDMA_ASSERT_NO_THROW(EXPECT_TRUE(waiter.get()));
    ASSERT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    close(pipeFds[0]);
    close(pipeFds[1]);
}

//////////////////////////////////////////////////////////////////////////////
//
//  Cancel
//
//  Description:
//     Tests cancelling a poll
//
//////////////////////////////////////////////////////////////////////////////
TEST(Poll, Cancel)
{
    int pipeFds[2];
    ASSERT_NE(pipe(pipeFds), -1);

    std::unique_ptr<FdPoller> poller;
    RDMA_ASSERT_NO_THROW(poller.reset(new FdPoller()));

    auto Poll = [&](int32_t millisec) {
        return poller->PollOnFd(pipeFds[0], millisec);
    };
    auto start = std::chrono::steady_clock::now();
    auto waiter = std::async(std::launch::async, Poll, 500);
    RDMA_ASSERT_NO_THROW(poller->Cancel());
    RDMA_ASSERT_NO_THROW(EXPECT_FALSE(waiter.get()));
    ASSERT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    close(pipeFds[0]);
    close(pipeFds[1]);
}

}; // namespace EasyRDMA
