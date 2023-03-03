// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

#include "iAccessManaged.h"

//============================================================================
//  Class tAccessSuspender
//============================================================================
class tAccessSuspender
{
public:
    tAccessSuspender(iAccessManaged* _resource, bool startSuspended = true) :
        resource(_resource), suspended(false)
    {
        if (startSuspended) {
            Suspend();
        }
    }
    ~tAccessSuspender()
    {
        if (suspended) {
            resource->GetAccessManager().ResumeAccess();
        }
    }
    void Suspend()
    {
        assert(!suspended);
        resource->GetAccessManager().SuspendAccess();
        suspended = true;
    }

private:
    bool suspended;
    iAccessManaged* resource;
};