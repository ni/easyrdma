// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

//============================================================================
//  Includes
//============================================================================
#include "errorhandling.h"
#include <sstream>
#include <atomic>
#include "common/RdmaError.h"

namespace EasyRDMA
{

//============================================================================
//  LastRdmaError - Wraps RdmaError with some additional functionality to
//                   be suitable for our usage in a thread-local manner:
//                   -Keeps track of allocated objects so we can sanity-check
//                    they get destroyed when expected and not leak memory
//============================================================================
struct LastRdmaError : public RdmaError
{
    LastRdmaError()
    {
        ++_allocatedLastRdmaErrors;
    }
    ~LastRdmaError()
    {
        --_allocatedLastRdmaErrors;
    }
    static uint64_t GetNumberOfAllocatedLastRdmaErrors()
    {
        return _allocatedLastRdmaErrors;
    }
    static std::atomic<uint64_t> _allocatedLastRdmaErrors;
};
std::atomic<uint64_t> LastRdmaError::_allocatedLastRdmaErrors(0);

//============================================================================
//  Thread-local statics
//============================================================================
static thread_local LastRdmaError lastRdmaError;

//////////////////////////////////////////////////////////////////////////////
//
//  PopulateLastRdmaError
//
//  Description:
//      Helper to populate the lastRdmaError
//
//  Parameters:
//      status  - the incoming status code
//
//////////////////////////////////////////////////////////////////////////////
void PopulateLastRdmaError(const RdmaError& status)
{
    lastRdmaError.Clear();
    if (status.GetCode() != 0) {
        lastRdmaError.Assign(status);
    }
}

//////////////////////////////////////////////////////////////////////////////
//
//  ClearLastRdmaError
//
//  Description:
//      Helper to clear the lastRdmaError
//
//////////////////////////////////////////////////////////////////////////////
void ClearLastRdmaError()
{
    lastRdmaError.Clear();
}

//////////////////////////////////////////////////////////////////////////////
//
//  GetLastRdmaError
//
//  Description:
//      Helper to populate the lastRdmaError
//
//  Parameters:
//      status  - the incoming status code
//
//////////////////////////////////////////////////////////////////////////////
void GetLastRdmaError(RdmaError& status)
{
    status.Assign(lastRdmaError);
}

//////////////////////////////////////////////////////////////////////////////
//
//  DebugGetNumberOfAllocatedLastRdmaErrors
//
//  Description:
//      Allows unit-tests to determine how many Rdmaerrors are outstanding
//      to make sure they are allocated and free'd as expected.
//
//  Returns:
//      Number of allocated Rdma errors. Should never be more than the number
//      of running threads in the process.
//
//////////////////////////////////////////////////////////////////////////////
int64_t DebugGetNumberOfAllocatedLastRdmaErrors()
{
    return LastRdmaError::GetNumberOfAllocatedLastRdmaErrors();
}

}; // namespace EasyRDMA
