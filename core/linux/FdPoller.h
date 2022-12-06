// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

#include <fcntl.h>
#include <poll.h>
#include "common/RdmaError.h"

class FdPoller {
    public:
        FdPoller() {
            int ret = pipe(pipeFds);
            if(ret == -1) {
                RDMA_THROW(-1 * errno); \
            }
        }
        ~FdPoller() {
            if(pipeFds[0] != -1) {
                close(pipeFds[0]);
                pipeFds[0] = -1;
            }
            if(pipeFds[1] != -1) {
                close(pipeFds[1]);
                pipeFds[1] = -1;
            }
        }
        bool PollOnFd(int fd, int timeoutMs) {
            pollfd pollingFds[2];
            pollingFds[0].fd = fd;
            pollingFds[0].events = POLLIN;
            pollingFds[0].revents = 0;
            pollingFds[1].fd = pipeFds[0];
            pollingFds[1].events = POLLIN;
            pollingFds[1].revents = 0;
            int ret = poll(pollingFds, 2, timeoutMs);
            if(ret == -1) {
                RDMA_THROW(-1 * errno); \
            }
            return pollingFds[0].revents != 0;
        }
        void Cancel() {
            (void)! write(pipeFds[1], " ", 1);
        }
    private:
        int pipeFds[2];
};

