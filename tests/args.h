// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <string>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <codecvt>
#endif

//============================================================================
//  GetCommandLineArguments - returns the command-line arguments passed to the
//                            top-level executable
//============================================================================
inline std::vector<std::string> GetCommandLineArguments()
{
    std::vector<std::string> arguments;
#ifdef _WIN32
    int nArgs = 0;
    std::unique_ptr<LPWSTR, decltype(&LocalFree)> szArglist(CommandLineToArgvW(GetCommandLineW(), &nArgs), &LocalFree);
    if (szArglist) {
        for (int i = 0; i < nArgs; ++i) {
            std::wstring wide(szArglist.get()[i]);
            std::string narrow = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.to_bytes(wide);
            arguments.push_back(narrow);
        }
    }
#elif defined(__linux__)
    std::unique_ptr<FILE, int (*)(FILE*)> cmdLineFile(fopen("/proc/self/cmdline", "r"), &fclose);
    if (cmdLineFile) {
        std::vector<char> cmdLineBuffer(4096, 0);
        size_t readBytes = fread(cmdLineBuffer.data(), 1, cmdLineBuffer.size(), cmdLineFile.get());
        if (readBytes > 0) {
            cmdLineBuffer.resize(readBytes);
            for (size_t i = 0; i < cmdLineBuffer.size(); ++i) {
                if (cmdLineBuffer[i] != 0) {
                    arguments.push_back(std::move(std::string(&cmdLineBuffer[i])));
                    while (cmdLineBuffer[i] != 0 && i < cmdLineBuffer.size()) {
                        ++i;
                    }
                }
            }
        }
    }
#endif
    return arguments;
}

//============================================================================
//  IsFastTestRun - tests whether the "--fast" argument was passed on the
//                  command-line
//============================================================================
inline bool IsFastTestRun()
{
    static bool fast = false;
    static bool hasrun = false;
    if (!hasrun) {
        for (const auto& arg : GetCommandLineArguments()) {
            if (arg == "--fast") {
                fast = true;
                break;
            }
        }
        hasrun = true;
    }
    return fast;
}

//============================================================================
//  WaitOnStart - tests whether the "--wait" argument was passed on the
//                  command-line
//============================================================================
inline bool WaitOnStart()
{
    for (const auto& arg : GetCommandLineArguments()) {
        if (arg == "--wait") {
            return true;
        }
    }
    return false;
}

//============================================================================
//  IsServer - tests whether the "--server" argument was passed on the
//                  command-line
//============================================================================
inline bool IsServer()
{
    static bool server = false;
    static bool hasrun = false;
    if (!hasrun) {
        for (const auto& arg : GetCommandLineArguments()) {
            if (arg == "--server") {
                server = true;
                break;
            }
        }
        hasrun = true;
    }
    return server;
}

//============================================================================
//  DebugRTJitter - tests whether the "--debugrtjitter" argument was passed on the
//                  command-line
//============================================================================
inline bool DebugRTJitter()
{
    static bool debugRTjitter = false;
    static bool hasrun = false;
    if (!hasrun) {
        for (const auto& arg : GetCommandLineArguments()) {
            if (arg == "--debugrtjitter") {
                debugRTjitter = true;
                break;
            }
        }
        hasrun = true;
    }
    return debugRTjitter;
}
