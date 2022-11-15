
// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "api/rdma_api_common.h"

#ifdef _WIN32
    #include <ndsupport.h>
    #include <ndstatus.h>
#endif

#ifdef _WIN32
extern "C" __declspec(dllexport) BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpReserved )  // reserved
{
    switch( fdwReason ) {
        case DLL_PROCESS_ATTACH:
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
#endif

namespace EasyRDMA {

// Global session manager
SessionManager sessionManager;

#ifdef _WIN32
    std::once_flag globalInitialization;

    void GlobalInitializeIfNeeded() {
        std::call_once(globalInitialization, [](){
            WSADATA wsaData;
            ::WSAStartup(MAKEWORD(2, 2), &wsaData);
            NdStartup();
        });
    }
#else
    void GlobalInitializeIfNeeded() { }
#endif //_WIN32


};