// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaCommon.h"
#include "RdmaEnumeration.h"
#include "RdmaAddress.h"
#include <ndsupport.h>
#include <ws2spi.h>

std::vector<RdmaEnumeration::RdmaInterface> RdmaEnumeration::EnumerateInterfaces(int32_t filterAddressFamily) {
    int32_t nativeAddressFamily = RdmaAddressFamilyToNative(filterAddressFamily);
    std::vector<RdmaInterface> interfaces;
    std::vector<uint8_t> addrListRawBuffer(65536);
    size_t addrListRawBufferSize = addrListRawBuffer.size();
    auto addrList = reinterpret_cast<SOCKET_ADDRESS_LIST*>(addrListRawBuffer.data());
    HandleHR(NdQueryAddressList(ND_QUERY_EXCLUDE_NDv1_ADDRESSES, addrList, &addrListRawBufferSize));
    for(size_t i = 0; i < addrList->iAddressCount; ++i) {
        if((nativeAddressFamily != AF_UNSPEC) && (nativeAddressFamily != addrList->Address[i].lpSockaddr->sa_family)) {
            continue;
        }
        std::string addrString = RdmaAddress::SockAddrToIpAddrString(addrList->Address[i].lpSockaddr);
        interfaces.push_back(RdmaEnumeration::RdmaInterface( { { addrString } }));
    }
    return std::move(interfaces);
}
