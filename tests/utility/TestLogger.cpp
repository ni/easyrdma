// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "TestLogger.h"
#include <fstream>

TestLogger::TestLogger(Verbosity level) :
    _logMutex(),
    _startTime(std::chrono::system_clock::now()),
    _destination(LogDestination::Global),
    _verbosity(level),
    _globalLogDestination(),
    _testLogDestination()
{}

TestLogger::~TestLogger() {}

void TestLogger::setVerbosity(Verbosity level)
{
    _verbosity = level;
}

void TestLogger::beginTest()
{
    std::lock_guard<std::mutex> lock(_logMutex);
    _destination = LogDestination::Test;
}

TestLogger::tLogData TestLogger::endTest()
{
    std::lock_guard<std::mutex> lock(_logMutex);
    std::string logs = _testLogDestination.str();

    _testLogDestination.str("");
    _destination = LogDestination::Global;

    return {std::move(logs)};
}

TestLogger::tLogData TestLogger::getGlobalLogs()
{
    std::lock_guard<std::mutex> lock(_logMutex);
    std::string logs = _globalLogDestination.str();
    _globalLogDestination.str("");

    return {std::move(logs)};
}

void TestLogger::logMessage(Verbosity level, const std::string& message)
{
    if ((int32_t)_verbosity > (int32_t)level)
        return;

    std::lock_guard<std::mutex> lock(_logMutex);

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _startTime);

    std::stringstream full_message;
    full_message << "[" << std::setfill(' ') << std::setw(10) << elapsed_ms.count() << "] " << std::setfill('\0');

    switch (level) {
        case Verbosity::Trace:
            full_message << "[TRACE] ";
            break;
        case Verbosity::Debug:
            full_message << "[DEBUG] ";
            break;
        case Verbosity::Info:
            full_message << "[ INFO] ";
            break;
        case Verbosity::Warn:
            full_message << "[ WARN] ";
            break;
        case Verbosity::Error:
            full_message << "[ERROR] ";
            break;
        case Verbosity::Fatal:
            full_message << "[FATAL] ";
            break;
        default:
            full_message << "[UNKWN] ";
    }

    full_message << message << std::endl;

    std::cout << full_message.str();

    switch (_destination) {
        case LogDestination::Test:
            _testLogDestination << full_message.str();
            break;
        case LogDestination::Global:
        default:
            _globalLogDestination << full_message.str();
            break;
    }
}
