// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>

class TestLogger {
public:
    typedef struct {
        std::string logs;
    } tLogData;

    enum Verbosity : int16_t {
        Trace = -3,
        Debug = -2,
        Info  = -1,
        Warn  =  0,
        Error =  1,
        Fatal =  2,
        Off   =  3
    };

    explicit TestLogger(Verbosity level = Verbosity::Info);
    virtual ~TestLogger();

    void setVerbosity(Verbosity level);

    // Called by the test fixture in SetUp to redirect logs to a test-specific destination.
    void beginTest();
    // Called by the test fixture in TearDown to get all test-specific logs and redirect
    // future logs back to the global destination.
    tLogData endTest();
    // Called by the test environment in TearDown to get all global logs and store them
    // in the test results.
    tLogData getGlobalLogs();

    void logMessage(TestLogger::Verbosity level, const std::string& message);

    class tStreamWrapper : public std::stringstream {
    public:
        tStreamWrapper(TestLogger& parent, TestLogger::Verbosity verbosity)
            : _parent(parent), _verbosity(verbosity) {}
        tStreamWrapper(const tStreamWrapper& other)
            : _parent(other._parent), _verbosity(other._verbosity) {}
        ~tStreamWrapper() {
            _parent.logMessage(_verbosity, std::stringstream::str());
        }
    private:
        TestLogger& _parent;
        TestLogger::Verbosity _verbosity;
    };

    tStreamWrapper log(TestLogger::Verbosity level) {
        return tStreamWrapper(*this, level);
    }
    inline tStreamWrapper trace() { return log(TestLogger::Verbosity::Trace); }
    inline tStreamWrapper debug() { return log(TestLogger::Verbosity::Debug); }
    inline tStreamWrapper info() { return log(TestLogger::Verbosity::Info); }
    inline tStreamWrapper warn() { return log(TestLogger::Verbosity::Warn); }
    inline tStreamWrapper error() { return log(TestLogger::Verbosity::Error); }
    inline tStreamWrapper fatal() { return log(TestLogger::Verbosity::Fatal); }

private:
    enum LogDestination : uint32_t {
        Global,
        Test
    };

    std::mutex _logMutex;

    std::chrono::time_point<std::chrono::system_clock> _startTime;

    LogDestination _destination;
    Verbosity _verbosity;
    std::stringstream _globalLogDestination;
    std::stringstream _testLogDestination;
};

