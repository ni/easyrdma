// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>
#include <boost/thread.hpp>
#include "RdmaError.h"
#include "api/easyrdma.h"

enum kThreadPriority {
    Higher,
    High,
    Normal
};

/////////////////////////////////////////////////////////////////////////////
//
//  tThreadAttrs
//
//  Description:
//      Represent the thread's scheduler policy and priority.
//
/////////////////////////////////////////////////////////////////////////////
struct tThreadAttrs {
    int32_t sched_policy;
    int32_t sched_priority;
};

bool IsRealtimeKernel();
tThreadAttrs GetThreadAttrs(kThreadPriority priority);
void SetPriorityForCurrentThread(kThreadPriority priority);
void ValidatePriorityForCurrentThread(kThreadPriority priority);

/////////////////////////////////////////////////////////////////////////////
//
//  CreatePriorityThread
//
//  Description:
//      On Linux:
//       - Creates a thread that runs at a custom priority.
//      Other OS:
//       - Creates a simple thread.
//  Params:
//      func     - A function to be passed to the thread. (Use boost::bind to
//                 pass a function with arguments).
//      priority - The priority the thread will run at.
//  Return value:
//      A boost::thread object.
//
/////////////////////////////////////////////////////////////////////////////
template<typename Callable>
boost::thread CreatePriorityThread(Callable func, kThreadPriority priority = kThreadPriority::High, const char* label = nullptr) {
    #ifdef __linux__
        tThreadAttrs threadAttrs = GetThreadAttrs(priority);

        boost::thread::attributes attrs;
        pthread_attr_setschedpolicy(attrs.native_handle(), threadAttrs.sched_policy);
        struct sched_param params;
        params.sched_priority = threadAttrs.sched_priority;
        pthread_attr_setschedparam(attrs.native_handle(), &params);

        // Force the thread to take the scheduling attributes specified in the
        // attributes argument. This will prevent that the thread inherits
        // the attributes from the caller thread.
        pthread_attr_setinheritsched(attrs.native_handle(), PTHREAD_EXPLICIT_SCHED);

        boost::thread t = boost::thread(attrs, func);
        if(label) {
            int rc = pthread_setname_np(t.native_handle(), label);
            if(rc != 0) {
                RDMA_THROW_WITH_SUBCODE(easyrdma_Error_InternalError, rc);
            }
        }
        return std::move(t);
    #else
        boost::thread t = boost::thread(func);
        if(label) {
            // FFV: Set thread description on Windows. Note that this is more complicated
            //       because we need to dynamically link to the function for compatibility and
            //       it also uses a wide string input
            //std::wstring wideLabel;
            //SetThreadDescription(t.native_handle(), wideLabel.c_str());
        }
        return std::move(t);
    #endif
}
