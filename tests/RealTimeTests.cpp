// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "args.h"
#include <chrono>
#include <memory>
#include <future>
#include <regex>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <vector>
#include "core/common/RdmaAddress.h"
#include "utility/RdmaTestBase.h"
#include "common/ThreadUtility.h"
#include <fstream>
#include <thread>

#ifdef __linux__
    #include <sys/mman.h>
    #include <sys/syscall.h>
#endif

#include "tests/session/Session.h"
#include "tests/utility/Enumeration.h"
#include "tests/utility/TestEndpoints.h"
#include "tests/utility/Utility.h"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>


namespace EasyRDMA {

using namespace boost::accumulators;
typedef accumulator_set<double, features<tag::mean, tag::min, tag::max, tag::count, tag::variance>> SimpleStatistics;

class RdmaRealTimeTest : public RdmaTestBase<::testing::TestWithParam<TestEndpoints>>  {
    public:
    RdmaRealTimeTest() : stop(false) {
        // If RT:
        // -use mlockall to pagelock all memory used by this process (like lvrt does)
        // -Configure this thread to run at higher priority.
        #ifdef __linux__
            if(IsRealtimeKernel()) {
                EXPECT_EQ(0, mlockall(MCL_CURRENT|MCL_FUTURE));
                SetPriorityForCurrentThread(kThreadPriority::Higher);
            }
        #endif

        // Start dummy threads to simulate OS work in the background.
        uint32_t kNumDummyBackgroundThreads = 5;
        for (size_t i = 0; i < kNumDummyBackgroundThreads; i++) {
            _osDummyWork.push_back(boost::move(CreatePriorityThread(boost::bind(&RdmaRealTimeTest::SimulateOsWork, this, boost::ref(stop)), kThreadPriority::Normal, "Interference")));
        }
    }
    ~RdmaRealTimeTest() {
        stop = true;
        for(auto& t : _osDummyWork) {
            t.join();
        }

        // Restore stuff we changed
        #ifdef __linux__
            if(IsRealtimeKernel()) {
                SetPriorityForCurrentThread(kThreadPriority::Normal);
                munlockall();
            }
        #endif
    }
    protected:
        void SysFsWrite(const char* file, std::string value) {
            std::ofstream f(file, std::fstream::out);
            ASSERT_TRUE(f.is_open()) << "File: " << file;
            f << value << std::endl;
            f.close();
        }

        void SysFsWrite(const char* file, int value) {
            SysFsWrite(file, std::to_string(value));
        }
        void RunJitterTest(bool usePolling);

    private:
        void SimulateOsWork(std::atomic<bool>& stop) {
            ValidatePriorityForCurrentThread(kThreadPriority::Normal);
            while(!stop) {
                std::string tempFilename = GetTemporaryFilename();
                ASSERT_TRUE(tempFilename.size() > 0);
                std::ofstream tempFile(tempFilename.c_str());
                ASSERT_TRUE(tempFile.is_open());
                // Write 1 MB of data to a file.
                std::vector<uint8_t> data(1000000, 'A');
                for (auto byte : data) {
                    tempFile << byte;
                }
                tempFile.close();
                ::remove(tempFilename.c_str());
                boost::this_thread::sleep_for(boost::chrono::microseconds(1));
            }
        }
        std::vector<boost::thread> _osDummyWork;
        std::atomic<bool> stop;
};

INSTANTIATE_TEST_SUITE_P(Devices, RdmaRealTimeTest, ::testing::ValuesIn(GetTestEndpointsBasic()), PrintTestParamName);


void RdmaRealTimeTest::RunJitterTest(bool usePolling) {
    // It is nice to still run these tests on Windows/desktop linux just to make sure the test isn't broken, even
    // though they don't give any useful performance metrics. We'll just make them run really fast there. Also allow
    // them to be fast with --fast on RT.
    auto kTestDuration = (IsRealtimeKernel() && !IsFastTestRun()) ? std::chrono::milliseconds(10000) : std::chrono::milliseconds(750);
    if(DebugRTJitter()) {
        kTestDuration = std::chrono::milliseconds(60000);
    }
    const auto kNumWarmupTime = std::chrono::milliseconds(500);

    // NOTE: Might need to adjust this to trigger!
    const double kLatencySpikeMicrosecToTrigger = 8.5;

    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, usePolling));
    const size_t kTransferSize = 128;
    std::vector<double> durations;
    durations.reserve(1000000);
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kTransferSize, 2));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kTransferSize, 1));

    if(IsRealtimeKernel()) {
        // Pin timed-loop prio stuff to CPU 7 and everything else to the remaining CPUs.
        // NOTE: Assumes we have an 8-core CPU, which all our ATS and dev test systems do.
        SysFsWrite("/dev/cgroup/cpuset/system_set/cpus", "0-6");
        SysFsWrite("/dev/cgroup/cpuset/LabVIEW_tl_set/cpus", "7");

        if(DebugRTJitter()) {
            info() << "-- Debugging RT jitter in the kernel --";
            info() << "Starting kernel event tracing. Test will end on latency spike and disable tracing.";
            SysFsWrite("/sys/kernel/debug/tracing/events/sched/enable", 1);
            SysFsWrite("/sys/kernel/debug/tracing/events/irq/enable", 1);
            SysFsWrite("/sys/kernel/debug/tracing/events/exceptions/enable", 1);
            SysFsWrite("/sys/kernel/debug/tracing/events/syscalls/enable", 1);
            SysFsWrite("/sys/kernel/debug/tracing/tracing_on", 1);
        }
    }

    SimpleStatistics latencyStats;
    bool sawLatencySpike = false;
    auto RealTimeBody = [&]() {
        // Add this thread to the TL task group
        #ifdef __linux__
            if(IsRealtimeKernel()) {
                SysFsWrite("/dev/cgroup/cpuset/LabVIEW_tl_set/tasks", syscall(SYS_gettid));
            }
        #endif

        auto testStart = std::chrono::high_resolution_clock::now();

        do {
            auto bufferRegion = connections.sender.GetSendRegion();

            #ifdef __linux__
                if(DebugRTJitter()) {
                    SysFsWrite("/sys/kernel/debug/tracing/trace_marker", 1);
                }
            #endif

            // Timestamp before we queue send to after we recv. We are intentionally not timing copying the data out of the buffer
            auto start = std::chrono::steady_clock::now();
            RDMA_ASSERT_NO_THROW(connections.sender.QueueRegion(bufferRegion));
            RDMA_ASSERT_NO_THROW(connections.receiver.ReceiveBlankData());

            double latencyUs = (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()) / 1000.0f;

            if(std::chrono::high_resolution_clock::now() - testStart > kNumWarmupTime) {
                durations.push_back(latencyUs);

                if(DebugRTJitter() && latencyUs > kLatencySpikeMicrosecToTrigger) {
                    sawLatencySpike = true;
                    break;
                }
            }

            // Allow credits to be received
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

        } while(std::chrono::high_resolution_clock::now() - testStart < kTestDuration);
    };
    auto RealTimeThread = CreatePriorityThread(RealTimeBody, IsRealtimeKernel() ? kThreadPriority::Higher : kThreadPriority::Normal, "Test");
    RDMA_ASSERT_NO_THROW(RealTimeThread.join());

    #ifdef __linux__
        if(DebugRTJitter()) {
            SysFsWrite("/sys/kernel/debug/tracing/tracing_on", 0);
            if(sawLatencySpike) {
                info() << "*** Saw latency spike. Stopped early. ***";
            }
        }

        if(IsRealtimeKernel()) {
            // Retore default pinning
            SysFsWrite("/dev/cgroup/cpuset/system_set/cpus", "0-7");
            SysFsWrite("/dev/cgroup/cpuset/LabVIEW_tl_set/cpus", "0-7");
        }
    #endif

    // Process raw latency samples.
    for(auto n : durations) {
        latencyStats(n );
    }

    info() << "One-way latency:";
    info() << "  -- min     : " << boost::accumulators::min(latencyStats) << " us";
    info() << "  -- mean    : " << boost::accumulators::mean(latencyStats) << " us";
    info() << "  -- max     : " << boost::accumulators::max(latencyStats) << " us";
    info() << "  -- std. dev: " << sqrt(boost::accumulators::variance(latencyStats));
    info() << "  -- count   : " << boost::accumulators::count(latencyStats);
}

#ifdef __linux__
TEST_P(RdmaRealTimeTest, Latency_UnderLoad_Polling) {
    RunJitterTest(true);
}
#endif

TEST_P(RdmaRealTimeTest, Latency_UnderLoad_NoPolling) {
    RunJitterTest(false);
}

}; //EasyRDMA