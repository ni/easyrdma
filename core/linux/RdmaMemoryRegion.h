// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

#include "RdmaCommon.h"
#include "rdma/rdma_verbs.h"

class RdmaMemoryRegion
{
public:
    RdmaMemoryRegion(rdma_cm_id* _cm_id, void* buffer, size_t length) :
        cm_id(_cm_id)
    {
        mr = rdma_reg_msgs(cm_id, buffer, length);
        HandleErrorFromPointer(mr);
    }
    ~RdmaMemoryRegion()
    {
        rdma_dereg_mr(mr);
    }
    ibv_mr* GetMR()
    {
        return mr;
    };

protected:
    rdma_cm_id* cm_id;
    ibv_mr* mr;
};
