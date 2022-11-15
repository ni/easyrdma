// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "RdmaEnumeration.h"
#include "RdmaCommon.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <boost/filesystem.hpp>
#include <boost/range.hpp>
#include <sstream>

struct InterfaceInfo {
    uint32_t ifIndex = -1;
    std::string ifName;
    std::string macAddress;
    std::vector<std::string> ipAddresses;
};

static std::string GetOsNetIfProperty(const std::string& ifPath, const std::string& property) {
    std::stringstream buffer;
    buffer << std::ifstream(ifPath + "/" + property).rdbuf();
    std::string output = buffer.str();
    // Remove EOL
    if (!output.empty() && output[output.length()-1] == '\n') {
        output.erase(output.length()-1);
    }
    return output;
}

std::vector<InterfaceInfo> GetInterfaces() {
    //----------------------------------------------------------------------------
    //  Pass 1 - enumerate all physical NICs and get their MAC and ifIndex
    //----------------------------------------------------------------------------
    namespace bf = boost::filesystem;
    std::vector<InterfaceInfo> interfaces;
    for(auto& interface : boost::make_iterator_range(bf::directory_iterator(bf::path("/sys/class/net")), {})) {
        //--------------------------------------------------------------------
        // Skip anything that isn't a directory
        //--------------------------------------------------------------------
        if(!bf::is_directory(interface)) {
            continue;
        }
        std::string ifPath = interface.path().string();
        std::string ifName = interface.path().filename().string();
        //--------------------------------------------------------------------
        //  Skip loopback adapters
        //--------------------------------------------------------------------
        bool isLoopback = std::stoi(GetOsNetIfProperty(ifPath, "flags"), nullptr, 16) & IFF_LOOPBACK;
        if (isLoopback) {
            continue;
        }
        //--------------------------------------------------------------------
        //  Capture relevant info and add to list
        //--------------------------------------------------------------------
        InterfaceInfo interfaceToAdd;
        interfaceToAdd.ifName = ifName;
        interfaceToAdd.macAddress = GetOsNetIfProperty(ifPath, "address");
        interfaceToAdd.ifIndex = std::stoi(GetOsNetIfProperty(ifPath, "ifindex"));
        interfaces.push_back(std::move(interfaceToAdd));
    }
    //----------------------------------------------------------------------------
    //  Pass 2 - enumerate all IP interfaces and match them to the already-created
    //      list, populating the IP addresses.
    //----------------------------------------------------------------------------
    ifaddrs *ifap = nullptr;
    HandleError(getifaddrs(&ifap));
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> ifapWrapper(ifap, &freeifaddrs);
    for (auto ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr ||
           (ifa->ifa_flags & IFF_LOOPBACK) == IFF_LOOPBACK) {
            continue;
        }
        int protocol = ifa->ifa_addr->sa_family;
        if(protocol != AF_INET && protocol != AF_INET6) {
            continue;
        }
        // Find interface to add it into
        for (auto& interface : interfaces) {
            if (interface.ifName == ifa->ifa_name) {
                interface.ipAddresses.push_back(RdmaAddress::SockAddrToIpAddrString(ifa->ifa_addr));
            }
        }
    }
    return std::move(interfaces);
}

bool IsAddressRdmaCompatible(const RdmaAddress& address) {
    bool isCompatible = false;
    // Hack: if the rdma bind succeeds, we know it's a valid rdma IP
    try {
        rdma_cm_id* cm_id = nullptr;
        if ((rdma_create_id(GetEventChannel(), &cm_id, &GetEventManager(), RDMA_PS_TCP) == 0) &&
            (rdma_bind_addr(cm_id, const_cast<RdmaAddress&>(address)) == 0)) {
                isCompatible = true;
        }
        if (cm_id) {
            rdma_destroy_id(cm_id);
        }
    }
    catch(const RdmaException&) {
        // Creating an event channel fails if there are no devices present
    }
    return isCompatible;
}

std::vector<RdmaEnumeration::RdmaInterface> RdmaEnumeration::EnumerateInterfaces(int32_t filterAddressFamily) {
    std::vector<RdmaEnumeration::RdmaInterface> interfaces;
    int32_t nativeAddressFamily = RdmaAddressFamilyToNative(filterAddressFamily);
    std::vector<InterfaceInfo> rawInterfaceList = GetInterfaces();
    for(const auto& interfaceInfo : rawInterfaceList) {
        for(const auto& ipAddress : interfaceInfo.ipAddresses) {
            RdmaAddress parsedAddress(ipAddress, 0);
            if((filterAddressFamily != AF_UNSPEC) && (parsedAddress.GetProtocol() != nativeAddressFamily)) {
                continue;
            }
            // If the address is IPv6 LLA, it needs the ifIndex set to a scope id
            // so the address is usable
            if(parsedAddress.IsIpV6LinkLocal()) {
                parsedAddress.SetScopeId(interfaceInfo.ifIndex);
            }
            if(IsAddressRdmaCompatible(parsedAddress)) {
                interfaces.push_back( { parsedAddress.GetAddrString() });
            }
        }
    }
    return std::move(interfaces);
}
