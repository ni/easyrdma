// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

// Apparently the kernel headers included with gcc 6.2 don't have this defined
#ifndef __aligned_u64
	#define __aligned_u64 __u64 __attribute__((aligned(8)))
#endif

#include <rdma/rdma_cma.h>
#include <infiniband/ib.h>

#include "RdmaError.h"
#include "api/easyrdma.h"
#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <memory>
#include "RdmaErrorTranslation.h"
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <assert.h>
#include <iostream>

class EventManager;

#define THROW_OS_ERROR(osError) \
    { \
        RdmaError s; \
        RDMA_SET_ERROR_WITH_SUBCODE(s,  RdmaErrorTranslation::OSErrorToRdmaError(osError), osError); \
        throw RdmaException(s); \
    }

#define HandleErrorFromPointer(expr) \
    { \
        void* result = (expr); \
        if(result == nullptr) { \
            THROW_OS_ERROR(errno); \
        } \
    }

#define HandleError(expr) \
    { \
        int result = (expr); \
        if(result == -1) { \
            THROW_OS_ERROR(errno); \
        } \
    }


#ifdef _DEBUG
    #define ASSERT_ALWAYS_INTERNAL(statement, statement_str) assert(statement)
#else
    #define ASSERT_ALWAYS_INTERNAL(statement, statement_str) \
        { \
            if(!(statement)) {  \
                std::cerr << "Fatal error:" << statement_str << " failed in " << __FILE__ << " at line " << __LINE__ << std::endl; \
                RDMA_THROW(easyrdma_Error_InternalError); \
            } \
        }
#endif

#define ASSERT_ALWAYS(statement) ASSERT_ALWAYS_INTERNAL(statement, #statement)


inline void* AllocateAlignedMemory(size_t size, size_t alignment) {
    void* allocatedBuffer = nullptr;
    if(0 != posix_memalign(&allocatedBuffer, alignment, size)) {
        RDMA_THROW(easyrdma_Error_OutOfMemory);
    }
    return allocatedBuffer;
}

inline void FreeAlignedMemory(void* ptr) {
    free(ptr);
}

rdma_event_channel* GetEventChannel();
EventManager& GetEventManager();
bool IsValgrindRunning();