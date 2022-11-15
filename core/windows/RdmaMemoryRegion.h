// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "RdmaCommon.h"

class RdmaMemoryRegion {
    public:
        RdmaMemoryRegion(AutoRef<IND2MemoryRegion>&& _memoryRegion, void* _buffer, size_t size) : memoryRegion(std::move(_memoryRegion)) {
            mrLocalToken = memoryRegion->GetLocalToken();
        }
        ~RdmaMemoryRegion() {
            try {
                OverlappedWrapper overlapped;
                HandleHROverlapped(memoryRegion->Deregister(overlapped), memoryRegion, overlapped);
                memoryRegion.reset();
            }
            catch(RdmaException&) {
                assert(0);
            }
        }

        UINT32 GetMRLocalToken() {
            return memoryRegion->GetLocalToken();
        }

    protected:
        AutoRef<IND2MemoryRegion> memoryRegion;
        UINT32 mrLocalToken = 0;
};
