// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "ThreadUtility.h"
#include <cstddef>
#include "api/easyrdma.h"
#include "RdmaError.h"
#include <boost/filesystem.hpp>

#ifdef __linux__
#include <pthread.h>
#endif

bool IsRealtimeKernel()
{
#ifdef __linux__
    return boost::filesystem::exists("/sys/kernel/realtime");
#else
    return false;
#endif
}

tThreadAttrs GetThreadAttrs(kThreadPriority priority)
{
#ifdef __linux__
    tThreadAttrs threadAttrs;
    switch (priority) {
        case kThreadPriority::Higher:
            threadAttrs.sched_policy = SCHED_FIFO;
            threadAttrs.sched_priority = 60;
            break;
        case kThreadPriority::High:
            // Same as LV "high", just below LV timed loops on Linux RT
            threadAttrs.sched_policy = SCHED_FIFO;
            threadAttrs.sched_priority = 29;
            break;
        case kThreadPriority::Normal:
        default:
            // Normal priority that Linux uses to create a thread.
            threadAttrs.sched_policy = SCHED_OTHER;
            threadAttrs.sched_priority = 0;
            break;
    }
    return threadAttrs;
#else
    return tThreadAttrs();
#endif
}

void SetPriorityForCurrentThread(kThreadPriority priority)
{
#ifdef __linux__
    tThreadAttrs threadAttrs = GetThreadAttrs(priority);
    struct sched_param params;
    params.sched_priority = threadAttrs.sched_priority;

    pthread_t this_thread = pthread_self();
    int ret = pthread_setschedparam(this_thread, threadAttrs.sched_policy, &params);
    if (ret != 0) {
        RDMA_THROW_WITH_SUBCODE(easyrdma_Error_InternalError, ret);
    }
#endif
}

void ValidatePriorityForCurrentThread(kThreadPriority priority)
{
#ifdef __linux__
    pthread_t this_thread = pthread_self();
    const tThreadAttrs expectedThreadAttrs = GetThreadAttrs(priority);
    struct sched_param params;
    int32_t policy = 0;
    int ret = pthread_getschedparam(this_thread, &policy, &params);
    if (ret != 0) {
        RDMA_THROW_WITH_SUBCODE(easyrdma_Error_InternalError, ret);
    }
    if (expectedThreadAttrs.sched_policy != policy || expectedThreadAttrs.sched_priority != params.sched_priority) {
        RDMA_THROW_WITH_SUBCODE(easyrdma_Error_InternalError, ret);
    }
#endif
}
