// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <memory>
#include <functional>
#include <string>
#include <iostream>
#include <regex>
#include "api/easyrdma.h"
#include "common/RdmaError.h"

// RdmaAddress.h is shared with the gtests currently, so do not include
// RdmaCommon.h but instead get all whatever OS headers we need explicitly. This
// is because RdmaCommon adds other dependencies we don't want to bring into the tests
#ifdef _WIN32
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#endif

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#endif

class RdmaAddress
{
public:
    RdmaAddress() :
        address({0}){};
    RdmaAddress(const std::string& _address, uint16_t port) :
        address({0})
    {
        // Capture any scope id
        std::smatch matchResult;
        std::regex scopeIdMatch("(%[0-9]+)$");
        std::string scopeId;
        if (std::regex_match(_address, matchResult, scopeIdMatch)) {
            scopeId = matchResult[0];
        }

        addrinfo hints = {(AI_ALL | AI_NUMERICHOST | AI_NUMERICSERV), AF_UNSPEC, 0, 0, 0, nullptr, nullptr, nullptr};
        addrinfo* result = nullptr;
        int ret = getaddrinfo(_address.c_str(), std::to_string(port).c_str(), &hints, &result);
        if (ret != 0) {
            RDMA_THROW(easyrdma_Error_InvalidAddress);
        }
        memcpy(&address, result->ai_addr, std::min(static_cast<size_t>(result->ai_addrlen), sizeof(address)));

        // Store scope id if passed. getaddrinfo() drops it.
        if (scopeId.length()) {
            if (GetProtocol() == AF_INET6) {
                auto in6 = reinterpret_cast<sockaddr_in6*>(&address);
                in6->sin6_scope_id = stoi(scopeId);
            } else {
                RDMA_THROW(easyrdma_Error_InvalidAddress);
            }
        }
        freeaddrinfo(result);
    }
    RdmaAddress(const sockaddr* _address) :
        address({0})
    {
        memcpy(&address, _address, GetAddrSize(_address));
    }
    RdmaAddress(const sockaddr_in& _address) :
        address({0})
    {
        memcpy(&address, &_address, sizeof(_address));
    }

    int GetProtocol() const
    {
        return address.ss_family;
    }

    size_t GetSize() const
    {
        return GetAddrSize(reinterpret_cast<const sockaddr*>(&address));
    }

    bool IsIpV6LinkLocal() const
    {
        switch (address.ss_family) {
            case AF_INET6:
                return IN6_IS_ADDR_LINKLOCAL(&reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr);
            default:
                return false;
        }
    }

    operator sockaddr*()
    {
        return reinterpret_cast<sockaddr*>(&address);
    }

    operator const sockaddr*() const
    {
        return reinterpret_cast<const sockaddr*>(&address);
    }

    static std::string SockAddrToIpAddrString(const sockaddr* sockaddr)
    {
        std::string output;
        output.resize(INET6_ADDRSTRLEN);
        switch (sockaddr->sa_family) {
            case AF_INET: {
                inet_ntop(sockaddr->sa_family, &reinterpret_cast<const sockaddr_in*>(sockaddr)->sin_addr, &output[0], output.size());
                output.resize(strlen(output.c_str()));
                break;
            }
            case AF_INET6: {
                auto in6 = reinterpret_cast<const sockaddr_in6*>(sockaddr);
                inet_ntop(sockaddr->sa_family, &in6->sin6_addr, &output[0], output.size());
                output.resize(strlen(output.c_str()));
#ifdef __linux__
                // Add in scope id on Linux if LLA. It's unneeded on Windows since
                // you can always bind to a specific source interface.
                // Scope ids of 0 are assumed to be unset
                if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr) && in6->sin6_scope_id != 0) {
                    output += "%" + std::to_string(in6->sin6_scope_id);
                }
#endif
                break;
            }
            case AF_UNSPEC:
                output = "*";
                break;
            default:
                RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        return std::move(output);
    }

    std::string GetAddrString() const
    {
        return SockAddrToIpAddrString(reinterpret_cast<const sockaddr*>(&address));
    }

    uint16_t GetPort() const
    {
        uint16_t port = 0;
        switch (address.ss_family) {
            case AF_INET: {
                auto ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
                port = ntohs(ipv4->sin_port);
                break;
            }
            case AF_INET6: {
                auto ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
                port = ntohs(ipv6->sin6_port);
                break;
            }
            case AF_UNSPEC:
                break;
            default:
                RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
        return port;
    }

    void SetPort(uint16_t port)
    {
        switch (address.ss_family) {
            case AF_INET:
                reinterpret_cast<sockaddr_in*>(&address)->sin_port = htons(port);
                break;
            case AF_INET6:
                reinterpret_cast<sockaddr_in6*>(&address)->sin6_port = htons(port);
                break;
            default:
                RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
    }

    void SetScopeId(uint32_t scopeId)
    {
        switch (address.ss_family) {
            case AF_INET6:
                reinterpret_cast<sockaddr_in6*>(&address)->sin6_scope_id = scopeId;
                break;
            default:
                RDMA_THROW(easyrdma_Error_InvalidArgument);
        }
    }

    std::string ToString() const
    {
        std::string output = GetAddrString();
        output += ":";
        output += std::to_string(GetPort());
        return std::move(output);
    }

    sockaddr_storage address;

private:
    static size_t GetAddrSize(const sockaddr* addr)
    {
        switch (addr->sa_family) {
            case AF_INET:
                return sizeof(sockaddr_in);
            case AF_INET6:
                return sizeof(sockaddr_in6);
        }
        return 0;
    }
};

inline std::ostream& operator<<(std::ostream& os, const RdmaAddress& addr)
{
    return os << addr.ToString();
}

inline bool operator==(const RdmaAddress& lhs, const RdmaAddress& rhs)
{
    return memcmp(&lhs.address, &rhs.address, sizeof(lhs.address)) == 0;
}

inline int32_t RdmaAddressFamilyToNative(int32_t rdmaAddressFamily)
{
    // Convert from our own define for address types to the native OS header definition
    int32_t nativeAddressFamily = 0;
    switch (rdmaAddressFamily) {
        case easyrdma_AddressFamily_AF_UNSPEC:
            return AF_UNSPEC;
        case easyrdma_AddressFamily_AF_INET:
            return AF_INET;
        case easyrdma_AddressFamily_AF_INET6:
            return AF_INET6;
        default:
            RDMA_THROW(easyrdma_Error_InvalidArgument);
    }
}
