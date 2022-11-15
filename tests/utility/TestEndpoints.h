// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <vector>
#include <string>
#include <ostream>

#include <gtest/gtest.h>
#include "Enumeration.h"

#if _WIN32
    #include <WinSock2.h>
#else
    #include <netinet/ip.h>
#endif

struct TestEndpoints {
    std::string endpointA;
    std::string endpointB;
    std::string name;
};

// This is used for printing parameter info during test failures
inline std::ostream& operator<<(std::ostream& os, const TestEndpoints& endpoint) {
  return os << endpoint.name << ": " << endpoint.endpointA << "<->" << endpoint.endpointB;
}

// This is used for the parameterized test name
inline std::string PrintTestParamName(const ::testing::TestParamInfo<TestEndpoints>& info) {
    return info.param.name;
}

// Test parameterization function that tries to get all possible permutations.
// This will generate test permutations of the following types:
// -One IPv4 loopback
// -One IPv6 loopback
// -One IPv4 connection between two different interfaces (if >=2 ports found)
// -One IPv6 connection between two different interfaces (if >=2 ports found)
// It doesn't currently add any extra if you have more than 2 ports since we don't
// expect the ATS to run in this manner and we'd have to add logic to the naming
// so they don't collide
inline std::vector<TestEndpoints> GetTestEndpointsAllPermutations() {
    std::vector<TestEndpoints> testEndpointList;
    auto interfaces = Enumeration::EnumerateInterfaces();
    std::vector<std::string> ipv4Interfaces;
    std::vector<std::string> ipv6Interfaces;
    for(const auto& iface : interfaces) {
        RdmaAddress address(iface, 0);
        switch(address.GetProtocol()) {
            case AF_INET:
                ipv4Interfaces.push_back(iface);
                break;
            case AF_INET6:
                ipv6Interfaces.push_back(iface);
                break;
        }
    }
    if(ipv4Interfaces.size()) {
        TestEndpoints ipv4Loopback;
        ipv4Loopback.endpointA = ipv4Interfaces[0];
        ipv4Loopback.endpointB = ipv4Interfaces[0];
        ipv4Loopback.name = "IPv4_Loopback";
        testEndpointList.push_back(ipv4Loopback);
    }
    if(ipv6Interfaces.size()) {
        TestEndpoints ipv6Loopback;
        ipv6Loopback.endpointA = ipv6Interfaces[0];
        ipv6Loopback.endpointB = ipv6Interfaces[0];
        ipv6Loopback.name = "IPv6_Loopback";
        testEndpointList.push_back(ipv6Loopback);
    }
    if(ipv4Interfaces.size() >= 2) {
        TestEndpoints ipv4;
        ipv4.endpointA = ipv4Interfaces[0];
        ipv4.endpointB = ipv4Interfaces[1];
        ipv4.name = "IPv4";
        testEndpointList.push_back(ipv4);
    }
    if(ipv6Interfaces.size() >= 2) {
        TestEndpoints ipv6;
        ipv6.endpointA = ipv6Interfaces[0];
        ipv6.endpointB = ipv6Interfaces[1];
        ipv6.name = "IPv6";
        testEndpointList.push_back(ipv6);
    }
    return std::move(testEndpointList);
}

// Test parameterization function for the majority of tests.
// It will return just one set of endpoints testing IPv4 connections.
// If there is a single port, it will use internal loopback; otherwise will
// use two ports assumed physically connected
inline std::vector<TestEndpoints> GetTestEndpointsBasic() {
    std::vector<TestEndpoints> testEndpointList;
    auto interfaces = Enumeration::EnumerateInterfaces();
    std::vector<std::string> ipv4Interfaces;
    std::vector<std::string> ipv6Interfaces;
    for(const auto& iface : interfaces) {
        RdmaAddress address(iface, 0);
        switch(address.GetProtocol()) {
            case AF_INET:
                ipv4Interfaces.push_back(iface);
                break;
            case AF_INET6:
                ipv6Interfaces.push_back(iface);
                break;
        }
    }
    if(ipv4Interfaces.size() == 1) {
        TestEndpoints ipv4Loopback;
        ipv4Loopback.endpointA = ipv4Interfaces[0];
        ipv4Loopback.endpointB = ipv4Interfaces[0];
        ipv4Loopback.name = "IPv4_Loopback";
        testEndpointList.push_back(ipv4Loopback);
    }
    else if(ipv4Interfaces.size() >= 2) {
        TestEndpoints ipv4;
        ipv4.endpointA = ipv4Interfaces[0];
        ipv4.endpointB = ipv4Interfaces[1];
        ipv4.name = "IPv4";
        testEndpointList.push_back(ipv4);
    }
    return std::move(testEndpointList);
}