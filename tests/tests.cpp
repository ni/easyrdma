// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "args.h"
#include <chrono>
#include <memory>
#include <future>
#include <regex>
#include "core/common/RdmaAddress.h"
#include "core/common/RdmaConnectionData.h"
#include "utility/RdmaTestBase.h"

#include "session/Session.h"
#include "utility/Enumeration.h"
#include "utility/TestEndpoints.h"
#include "utility/Utility.h"

namespace EasyRDMA {


std::string StripPossibleIpv6ScopeId(const std::string& input) {
    std::regex scopeIdMatch("(%[0-9]+)$");
    std::string strippedAddress = input;
    strippedAddress = std::regex_replace(strippedAddress, scopeIdMatch, "");
    return strippedAddress;
}


class RdmaTest : public RdmaTestBase<::testing::TestWithParam<TestEndpoints>>  {
public:
    RdmaTest() {
    }
    ~RdmaTest() {
    }
};

class RdmaTestPermutateConnectionTypes : public RdmaTest {
public:
    RdmaTestPermutateConnectionTypes() {
    }
    ~RdmaTestPermutateConnectionTypes() {
    }
};

INSTANTIATE_TEST_SUITE_P(Devices, RdmaTestPermutateConnectionTypes, ::testing::ValuesIn(GetTestEndpointsAllPermutations()), PrintTestParamName);
INSTANTIATE_TEST_SUITE_P(Devices, RdmaTest, ::testing::ValuesIn(GetTestEndpointsBasic()), PrintTestParamName);

class RdmaEnumerationTest : public RdmaTestBase<::testing::Test>  {
};

TEST_F(RdmaEnumerationTest, Enumerate) {
    auto interfaces = Enumeration::EnumerateInterfaces();
    for(const auto& iface : interfaces) {
        info() << " -- " << iface;
    }
}

TEST_F(RdmaEnumerationTest, CorrectNumberOfInterfacesForTest) {
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
    EXPECT_EQ(ipv4Interfaces.size(), 2U) << "Expected exactly 2 IPv4 RDMA ports; Remaining tests may not give expected results";
    EXPECT_EQ(ipv6Interfaces.size(), 2U) << "Expected exactly 2 IPv6 RDMA ports; Remaining tests may not give expected results";
};

TEST_F(RdmaEnumerationTest, Enumerate_Filter) {
    auto interfacesAll = Enumeration::EnumerateInterfaces();
    std::vector<std::string> ipv4Interfaces;
    std::vector<std::string> ipv6Interfaces;
    for(const auto& iface : interfacesAll) {
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
    std::sort(ipv4Interfaces.begin(), ipv4Interfaces.end());
    std::sort(ipv6Interfaces.begin(), ipv6Interfaces.end());

    auto interfacesFilteredIPv4 = Enumeration::EnumerateInterfaces(easyrdma_AddressFamily_AF_INET);
    std::sort(interfacesFilteredIPv4.begin(), interfacesFilteredIPv4.end());
    EXPECT_EQ(interfacesFilteredIPv4, ipv4Interfaces);

    auto interfacesFilteredIPv6 = Enumeration::EnumerateInterfaces(easyrdma_AddressFamily_AF_INET6);
    std::sort(interfacesFilteredIPv6.begin(), interfacesFilteredIPv6.end());
    EXPECT_EQ(interfacesFilteredIPv6, ipv6Interfaces);
}


TEST_P(RdmaTestPermutateConnectionTypes, CreateConnector) {
    Session session;
    auto endpoint = GetEndpointAddresses().first;
    RDMA_ASSERT_NO_THROW(session = Session::CreateConnector(endpoint.GetAddrString(), endpoint.GetPort()));

    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetLocalAddress(), endpoint.GetAddrString()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetLocalPort(), endpoint.GetPort()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetRemoteAddress(), "*"));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetRemotePort(), 0));

    RDMA_ASSERT_NO_THROW(session.Close());
}

TEST_P(RdmaTestPermutateConnectionTypes, CreateListener) {
    Session session;
    auto endpoint = GetEndpointAddresses().first;
    RDMA_ASSERT_NO_THROW(session = Session::CreateListener(endpoint.GetAddrString(), endpoint.GetPort()));

    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetLocalAddress(), endpoint.GetAddrString()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetLocalPort(), endpoint.GetPort()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetRemoteAddress(), "*"));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(session.GetRemotePort(), 0));

    RDMA_ASSERT_NO_THROW(session.Close());
}

TEST_P(RdmaTestPermutateConnectionTypes, CreateListener_ReusePort) {
    Session session, session2;
    auto endpoint = GetEndpointAddresses().first;
    RDMA_ASSERT_NO_THROW(session = Session::CreateListener(endpoint.GetAddrString(), 10000));
    RDMA_ASSERT_THROW_WITHCODE(session2 = Session::CreateListener(endpoint.GetAddrString(), 10000), easyrdma_Error_AddressInUse);
}

TEST_P(RdmaTestPermutateConnectionTypes, CreateConnector_ReusePort) {
    Session session, session2;
    auto endpoint = GetEndpointAddresses().first;
    RDMA_ASSERT_NO_THROW(session = Session::CreateConnector(endpoint.GetAddrString(), 10000));
    #ifdef _WIN32
        // Oddly, the Mellanox provider seems to not care if we create two connectors sharing the
        // same address/port
        RDMA_ASSERT_NO_THROW(session2 = Session::CreateConnector(endpoint.GetAddrString(), 10000));
    #else
        RDMA_ASSERT_THROW_WITHCODE(session2 = Session::CreateConnector(endpoint.GetAddrString(), 10000), easyrdma_Error_AddressInUse);
    #endif
}


TEST_P(RdmaTestPermutateConnectionTypes, Connect) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnector, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive);
    } );
    RDMA_ASSERT_NO_THROW(sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    Session sessionAccepted;
    RDMA_ASSERT_NO_THROW(sessionAccepted = std::move(accept.get()));

    // Strip any scope Ids because they can be inconsistently present depending on how we are getting them
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(StripPossibleIpv6ScopeId(sessionConnector.GetLocalAddress()), StripPossibleIpv6ScopeId(localAddressConnector.GetAddrString())));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(sessionConnector.GetLocalPort(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(StripPossibleIpv6ScopeId(sessionConnector.GetRemoteAddress()), StripPossibleIpv6ScopeId(localAddressListener.GetAddrString())));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(sessionConnector.GetRemotePort(), localAddressListener.GetPort()));

    RDMA_ASSERT_NO_THROW( EXPECT_EQ(StripPossibleIpv6ScopeId(sessionAccepted.GetLocalAddress()), StripPossibleIpv6ScopeId(localAddressListener.GetAddrString())));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(sessionAccepted.GetLocalPort(), localAddressListener.GetPort()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(StripPossibleIpv6ScopeId(sessionAccepted.GetRemoteAddress()), StripPossibleIpv6ScopeId(localAddressConnector.GetAddrString())));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ(sessionAccepted.GetRemotePort(), localAddressConnector.GetPort()));

    RDMA_ASSERT_NO_THROW(sessionConnector.Close());
    RDMA_ASSERT_NO_THROW(sessionListener.Close());
    RDMA_ASSERT_NO_THROW(sessionAccepted.Close());
}

TEST_P(RdmaTestPermutateConnectionTypes, ConnectDisconnect) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(EXPECT_EQ(true, connections.sender.GetPropertyBool(easyrdma_Property_Connected)));
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(true, connections.receiver.GetPropertyBool(easyrdma_Property_Connected)));

    RDMA_ASSERT_NO_THROW(connections.sender.Close());
    bool connected = true;
    auto started = std::chrono::steady_clock::now();
    while(connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        RDMA_ASSERT_NO_THROW(connected = connections.receiver.GetPropertyBool(easyrdma_Property_Connected));
        ASSERT_LE(std::chrono::steady_clock::now()-started, std::chrono::milliseconds(500));
    }
}

TEST_P(RdmaTest, GetPropertyErrorWriteOnly) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    size_t bufferSize = 100;
    std::vector<uint8_t> buffer(bufferSize);
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.GetProperty(easyrdma_Property_ConnectionData, buffer.data(), &bufferSize), easyrdma_Error_WriteOnlyProperty);
}

TEST_P(RdmaTest, SetPropertyErrorReadOnly) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    uint64_t queuedBuffers = 1;
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.SetProperty(easyrdma_Property_QueuedBuffers, &queuedBuffers, sizeof(queuedBuffers)), easyrdma_Error_ReadOnlyProperty);
}

TEST_P(RdmaTest, ConnectLoop) {
    for(int iteration = 0; iteration < 50; ++iteration) {
        ConnectionPair connections;
        RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    }
}

TEST_P(RdmaTest, ConnectMultipleToSingleListener) {
    RdmaAddress localAddressListener = GetEndpointAddresses().first;
    RdmaAddress localAddressConnector = GetEndpointAddresses().second;

    Session sessionConnectorA, sessionConnectorB, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionConnectorA = Session::CreateConnector(localAddressConnector.GetAddrString(), 0));
    RDMA_ASSERT_NO_THROW(sessionConnectorB = Session::CreateConnector(localAddressConnector.GetAddrString(), 0));

    auto receiverA = std::async(std::launch::async, [&]() { return sessionListener.Accept(easyrdma_Direction_Receive); } );
    RDMA_ASSERT_NO_THROW(sessionConnectorA.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    RDMA_ASSERT_NO_THROW(receiverA.get());
    auto receiverB = std::async(std::launch::async, [&]() { return sessionListener.Accept(easyrdma_Direction_Receive); } );
    RDMA_ASSERT_NO_THROW(sessionConnectorB.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    RDMA_ASSERT_NO_THROW(receiverB.get());
}


TEST_P(RdmaTest, Connect_Cancel_WithClose) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    Session sessionConnector, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto connect = std::async(std::launch::async, [&]() {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort(), 5000);
    });
    // Give time for async Connect thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Destroy connector to cancel
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(sessionConnector.Close());
    RDMA_ASSERT_THROW_WITHCODE(connect.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(200));

    RDMA_ASSERT_NO_THROW(sessionListener.Close());
}

TEST_P(RdmaTest, Connect_Cancel_WithAbort) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    Session sessionConnector, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto connect = std::async(std::launch::async, [&]() {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort(), 5000);
    });
    // Give time for async Connect thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Abort connector
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(sessionConnector.Abort());

    // Abort again and make sure it is harmless
    RDMA_ASSERT_NO_THROW(sessionConnector.Abort());

    RDMA_ASSERT_THROW_WITHCODE(connect.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(200));

    RDMA_ASSERT_NO_THROW(sessionListener.Close());
    RDMA_ASSERT_NO_THROW(sessionConnector.Close());
}

TEST_P(RdmaTest, Accept_Cancel_WithClose) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;

    Session sessionListener;
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive, 5000);
    } );
    // Give time for async Accept thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Destroy listener to cancel
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(sessionListener.Close());
    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(200));
}

TEST_P(RdmaTest, Accept_Cancel_WithAbort) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;

    Session sessionListener;
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive, 5000);
    } );
    // Give time for async Accept thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Abort listener to cancel
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(sessionListener.Abort());

    // Abort again and make sure it is harmless
    RDMA_ASSERT_NO_THROW(sessionListener.Abort());

    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(200));

    RDMA_ASSERT_NO_THROW(sessionListener.Close());
}

TEST_P(RdmaTest, Accept_AgainAfterTimeout) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    Session sessionListener, sessionConnector;
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));

    RDMA_ASSERT_THROW_WITHCODE(sessionListener.Accept(easyrdma_Direction_Receive, 10), easyrdma_Error_Timeout);
    auto connect = std::async(std::launch::async, [&]() {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort());
    });
    RDMA_ASSERT_NO_THROW(sessionListener.Accept(easyrdma_Direction_Receive, 500));
    RDMA_ASSERT_NO_THROW(connect.get());
}


TEST_P(RdmaTest, Connect_Timeout) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    Session sessionConnector, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto connect = std::async(std::launch::async, [&]() {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort(), 50);
    });

    EXPECT_EQ(connect.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);
    sessionConnector.Close();
    RDMA_ASSERT_THROW_WITHCODE(connect.get(), easyrdma_Error_Timeout);
}

TEST_P(RdmaTest, Connect_ErrorsWhenCalledAgainAfterTimeout) {

    // This test checks the behavior when we call connect on a same session again after
    // we failed due to a timeout. Ideally this seems like it should work, but on the Mellanox
    // drivers on both Windows and Linux, once you have attempted a connect attempt, using the same
    // session to try connecting will fail. We could try to re-create the underlying connector, but
    // this wouldn't be completely transparent due to the need to re-bind (potentially to a different
    // port) and this doesn't seem like it would be a common scenario anyway. We'll simply validate that
    // this code path doesn't behave unexpectedly, but not expect it to actually connect.

    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    Session sessionConnector, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto connect = std::async(std::launch::async, [&]() {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort(), 50);
    });
    RDMA_ASSERT_THROW_WITHCODE(connect.get(), easyrdma_Error_Timeout);

    // Re-create listener
    RDMA_ASSERT_NO_THROW(sessionListener.Close());
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive, 100);
    });
    connect = std::async(std::launch::async, [&]() {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort(), 5000);
    });

    // Currently Windows and Linux fail differently when in the edge case of calling connect again. For now we'll just
    // expect different errors. We'd need some special code in Linux to determine if the invalid argument is due to
    // calling resolve_addr a second time vs some other error in that path.
    #ifdef _WIN32
        int errorCode = easyrdma_Error_AlreadyConnected;
    #else
        int errorCode = easyrdma_Error_InvalidArgument;
    #endif
    RDMA_EXPECT_THROW_WITHCODE(connect.get(), errorCode);
    RDMA_EXPECT_THROW_WITHCODE(accept.get(), easyrdma_Error_Timeout);
}

TEST_P(RdmaTest, Connect_ErrorsWhenCalledAgainAfterConnected) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnector, sessionListener;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive);
    } );
    RDMA_ASSERT_NO_THROW(sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    Session sessionAccepted;
    RDMA_ASSERT_NO_THROW(sessionAccepted = std::move(accept.get()));

    // Connect on already connected session
    RDMA_EXPECT_THROW_WITHCODE(sessionConnector.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), localAddressListener.GetPort()), easyrdma_Error_AlreadyConnected);
}

TEST_P(RdmaTest, Accept_Timeout) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;

    Session sessionListener;
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));

    auto accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive, 50);
    } );

    EXPECT_EQ(accept.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
    sessionListener.Close();
    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_Timeout);
}


TEST_P(RdmaTest, ConfigureBuffers) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(1024*1024, 20));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(4096*1024, 50));
}

TEST_P(RdmaTest, ConfigureBuffers_SenderFirst) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(1024*1024, 20));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(4096*1024, 50));
}

TEST_P(RdmaTest, ConfigureBuffers_ReceiverFirst) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(4096*1024, 50));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(1024*1024, 20));
}

TEST_P(RdmaTest, ConfigureExternalBuffer_Sender) {
    ConnectionPair connections;
    std::vector<uint8_t> sendBuffer(4096*1024);
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), 20));
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffers
}

TEST_P(RdmaTest, ConfigureExternalBuffer_Receiver) {
    ConnectionPair connections;
    std::vector<uint8_t> receiveBuffer(4096*1024);
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(receiveBuffer.data(), receiveBuffer.size(), 50));
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffers
}

TEST_P(RdmaTest, ConfigureBuffers_Twice) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(1024*1024, 20));
    RDMA_EXPECT_THROW_WITHCODE(connections.sender.ConfigureBuffers(1024*1024, 20), easyrdma_Error_AlreadyConfigured);

    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(4096*1024, 50));
    RDMA_EXPECT_THROW_WITHCODE(connections.receiver.ConfigureBuffers(4096*1024, 50), easyrdma_Error_AlreadyConfigured);
}

TEST_P(RdmaTest, ConfigureExternalBuffer_Twice) {
    ConnectionPair connections;
    std::vector<uint8_t> sendBuffer(1024*1024);
    std::vector<uint8_t> receiveBuffer(4096*1024);
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), 20));
    RDMA_EXPECT_THROW_WITHCODE(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), 20), easyrdma_Error_AlreadyConfigured);
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(receiveBuffer.data(), receiveBuffer.size(), 50));
    RDMA_EXPECT_THROW_WITHCODE(connections.receiver.ConfigureExternalBuffer(receiveBuffer.data(), receiveBuffer.size(), 50), easyrdma_Error_AlreadyConfigured);
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffers
}


TEST_P(RdmaTest, Configure_ConnectorErrors) {
    Session sessionConnector;
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressConnector = endpoints.first;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    RDMA_ASSERT_THROW_WITHCODE(sessionConnector.ConfigureBuffers(1024, 10), easyrdma_Error_NotConnected);
}

TEST_P(RdmaTest, Configure_ListenerErrors) {
    Session sessionListener;
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RDMA_ASSERT_NO_THROW(sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), localAddressListener.GetPort()));
    RDMA_ASSERT_THROW_WITHCODE(sessionListener.ConfigureBuffers(1024, 10), easyrdma_Error_InvalidOperation);
}

TEST_P(RdmaTest, SendReceive) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    std::vector<uint8_t> sendBuffer(bufferSize);
    for(size_t i=0; i < bufferSize; ++i) {
        sendBuffer[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> receiveBuffer;
    RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    RDMA_ASSERT_NO_THROW(receiveBuffer = connections.receiver.Receive());
    ASSERT_EQ(sendBuffer.size(), receiveBuffer.size());
    EXPECT_EQ(sendBuffer, receiveBuffer);
}


TEST_P(RdmaTest, SendWithCallback) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 1024;
    const size_t transferCount = 32;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, transferCount));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, transferCount));

    BufferCompletion completedTransfers[transferCount];

    for(size_t i = 0; i < transferCount; ++i) {
        std::vector<uint8_t> sendBuffer(bufferSize, static_cast<uint8_t>(i));
        RDMA_ASSERT_NO_THROW(connections.sender.SendWithCallback(sendBuffer, &completedTransfers[i], reinterpret_cast<void*>(static_cast<uintptr_t>(i))));
    }

    for(size_t i = 0; i < transferCount; ++i) {
        RDMA_ASSERT_NO_THROW(completedTransfers[i].WaitForCompletion(500)) << "Transfer: " << i;
        EXPECT_EQ(reinterpret_cast<uintptr_t>(completedTransfers[i].GetContext()), i);
    }
}


TEST_P(RdmaTest, SendReceive_Partial) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 100;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 5));

    // Go through the buffer sizes twice so that we ensure we re-use buffers with smaller sizes
    // than they previously held
    for(size_t i = 0; i < bufferSize*2; ++i) {
        size_t partialSize = i % bufferSize;
        std::vector<uint8_t> sendBuffer(partialSize);
        for(auto& byte : sendBuffer) {
            byte = static_cast<uint8_t>(rand());
        }
        std::vector<uint8_t> receiveBuffer;
        BufferCompletion completion;
        RDMA_ASSERT_NO_THROW(connections.sender.SendWithCallback(sendBuffer, &completion));
        RDMA_ASSERT_NO_THROW(completion.WaitForCompletion(500));
        ASSERT_EQ(completion.GetCompletedBytes(), partialSize);
        RDMA_ASSERT_NO_THROW(receiveBuffer = connections.receiver.Receive());
        ASSERT_EQ(sendBuffer.size(), receiveBuffer.size());
        EXPECT_EQ(sendBuffer, receiveBuffer);
    }
}

TEST_P(RdmaTest, Send_Partial_TooLarge) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 100;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    BufferRegion sendRegion;
    RDMA_ASSERT_NO_THROW(sendRegion = connections.sender.GetSendRegion());
    ASSERT_EQ(sendRegion.bufferSize, bufferSize);
    sendRegion.usedSize = bufferSize+1;
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.QueueRegion(sendRegion), easyrdma_Error_InvalidSize);
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.Receive(10), easyrdma_Error_Timeout);
}

TEST_P(RdmaTest, FlowControl_Send_TooLarge) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t SendbufferSize = 100;
    const size_t RevbufferSize = 50;
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(RevbufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(SendbufferSize, 1));

    // Make sure the background credit mechanism has had time to run and give the receiver credit
    // so that we get the error on queueing and not asynchronously later
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    BufferRegion sendRegion;
    RDMA_ASSERT_NO_THROW(sendRegion = connections.sender.GetSendRegion());

    RDMA_ASSERT_THROW_WITHCODE(connections.sender.QueueRegion(sendRegion), easyrdma_Error_SendTooLargeForRecvBuffer);
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.Receive(10), easyrdma_Error_Timeout);
}

TEST_P(RdmaTest, ExternalMemorySend) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    const int bufferCount = 10;
    const int transferCount = 5;
    const size_t eachBufferLen = 1024*1024;
    std::vector<uint8_t> largeSendBuffer(bufferCount*eachBufferLen);
    for(auto& b : largeSendBuffer) {
        b = static_cast<uint8_t>(rand());
    }

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(largeSendBuffer.data(), largeSendBuffer.size(), bufferCount));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(eachBufferLen, bufferCount));

    auto sender = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            RDMA_ASSERT_NO_THROW(connections.sender.QueueExternalBuffer(largeSendBuffer.data() + (i % bufferCount)*eachBufferLen, eachBufferLen));
        }
    });
    auto receiver = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            std::vector<uint8_t> receiveBuffer;
            RDMA_ASSERT_NO_THROW(receiveBuffer = connections.receiver.Receive());
            ASSERT_EQ(receiveBuffer.size(), eachBufferLen);
            EXPECT_EQ(memcmp(receiveBuffer.data(), largeSendBuffer.data() + (i % bufferCount)*eachBufferLen, eachBufferLen), 0);
        }
    });
    RDMA_ASSERT_NO_THROW(sender.get());
    RDMA_ASSERT_NO_THROW(receiver.get());
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffer
}

TEST_P(RdmaTest, Send_Partial_ExternalMemory) {
    const size_t maxTransferSize = 100;
    std::vector<uint8_t> sendBuffer(maxTransferSize);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(maxTransferSize, 5));

    // Go through the buffer sizes twice so that we ensure we re-use buffers with smaller sizes
    // than they previously held
    for(size_t i = 0; i < maxTransferSize*2; ++i) {
        size_t partialSize = i % maxTransferSize;
        for(size_t j = 0; j < partialSize; ++j) {
            sendBuffer[j] = static_cast<uint8_t>(rand());
        }
        std::vector<uint8_t> receiveBuffer;
        BufferCompletion completion;
        RDMA_ASSERT_NO_THROW(connections.sender.QueueExternalBufferWithCallback(sendBuffer.data(), partialSize, &completion));
        RDMA_ASSERT_NO_THROW(completion.WaitForCompletion(100));
        ASSERT_EQ(partialSize, completion.GetCompletedBytes());
        RDMA_ASSERT_NO_THROW(receiveBuffer = connections.receiver.Receive());
        ASSERT_EQ(partialSize, receiveBuffer.size());
        EXPECT_EQ(std::vector<uint8_t>(sendBuffer.begin(), sendBuffer.begin()+partialSize), receiveBuffer);
    }
}

TEST_P(RdmaTest, Recv_Partial_ExternalMemory) {
    const size_t maxTransferSize = 100;
    std::vector<uint8_t> recvBuffer(maxTransferSize);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(maxTransferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(recvBuffer.data(), recvBuffer.size(), 5));

    // Go through the buffer sizes twice so that we ensure we re-use buffers with smaller sizes
    // than they previously held
    for(size_t i = 0; i < maxTransferSize*2; ++i) {
        size_t partialSize = i % maxTransferSize;

        std::vector<uint8_t> sendBuffer(partialSize);
        for(auto& byte : sendBuffer) {
            byte = static_cast<uint8_t>(rand());
        }
        //Fill in whole recv buffer with 0's
        std::fill(recvBuffer.begin(), recvBuffer.end(), 0);

        BufferCompletion completion;
        RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(recvBuffer.data(), partialSize, &completion));
        RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
        RDMA_ASSERT_NO_THROW(completion.WaitForCompletion(100));
        ASSERT_EQ(partialSize, completion.GetCompletedBytes());

        // Transferred portion should match what we sent
        EXPECT_EQ(std::vector<uint8_t>(recvBuffer.begin(), recvBuffer.begin()+partialSize), sendBuffer);
        // Remaining portion should be 0's
        EXPECT_EQ(std::vector<uint8_t>(recvBuffer.begin()+partialSize, recvBuffer.end()), std::vector<uint8_t>(recvBuffer.size()-partialSize, 0));
    }
}

TEST_P(RdmaTest, Recv_Partial_ExternalMemory_AfterSend) {
    const size_t maxTransferSize = 100;
    std::vector<uint8_t> recvBuffer(maxTransferSize);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(maxTransferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(recvBuffer.data(), recvBuffer.size(), 5));

    // Go through the buffer sizes twice so that we ensure we re-use buffers with smaller sizes
    // than they previously held
    for(size_t i = 0; i < maxTransferSize*2; ++i) {
        size_t partialSize = i % maxTransferSize;

        std::vector<uint8_t> sendBuffer(partialSize);
        for(auto& byte : sendBuffer) {
            byte = static_cast<uint8_t>(rand());
        }
        //Fill in whole recv buffer with 0's
        std::fill(recvBuffer.begin(), recvBuffer.end(), 0);

        // Send first, without buffers queued on receive side. This tests
        // the case where the sender queues waiting for credits.
        RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));

        BufferCompletion completion;
        RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(recvBuffer.data(), partialSize, &completion));
        RDMA_ASSERT_NO_THROW(completion.WaitForCompletion(100));
        ASSERT_EQ(partialSize, completion.GetCompletedBytes());

        // Transferred portion should match what we sent
        EXPECT_EQ(std::vector<uint8_t>(recvBuffer.begin(), recvBuffer.begin()+partialSize), sendBuffer);
        // Remaining portion should be 0's
        EXPECT_EQ(std::vector<uint8_t>(recvBuffer.begin()+partialSize, recvBuffer.end()), std::vector<uint8_t>(recvBuffer.size()-partialSize, 0));
    }
}


TEST_P(RdmaTest, ExternalMemoryRecv) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    const int bufferCount = 10;
    const int transferCount = 20;
    const size_t eachBufferLen = 1024*1024;
    std::vector<uint8_t> largeRecvBuffer(bufferCount*eachBufferLen);
    std::vector<std::vector<uint8_t>> sendBuffers(transferCount, std::vector<uint8_t>(eachBufferLen));
    for(auto& sendbuf : sendBuffers) {
        for(auto& b : sendbuf) {
            b = static_cast<uint8_t>(rand());
        }
    }

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(eachBufferLen, bufferCount));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(largeRecvBuffer.data(), largeRecvBuffer.size(), bufferCount));

    // Queue all rx buffers at start
    BufferCompletion completedTransfers[transferCount];
    size_t queuedSendBuffers = 0;
    for(int i = 0; i < bufferCount; ++i) {
        RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(largeRecvBuffer.data() + (i % bufferCount)*eachBufferLen,
                                                                                eachBufferLen, &completedTransfers[queuedSendBuffers++], nullptr, 1000));
    }
    auto sender = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            connections.sender.Send(sendBuffers[i]);
        }
    });
    auto receiver = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            RDMA_ASSERT_NO_THROW(completedTransfers[i].WaitForCompletion(1000)) << "Buffer index: " << i;
            EXPECT_EQ(memcmp(sendBuffers[i].data(), largeRecvBuffer.data() + (i % bufferCount)*eachBufferLen, eachBufferLen), 0);

            // Done with buffer, so re-queue it
            if(i < transferCount-bufferCount) {
                RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(largeRecvBuffer.data() + (i % bufferCount)*eachBufferLen,
                                                                                eachBufferLen, &completedTransfers[queuedSendBuffers++], nullptr, 1000));
            }
        }
    });
    RDMA_ASSERT_NO_THROW(sender.get());
    RDMA_ASSERT_NO_THROW(receiver.get());
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffer
}

TEST_P(RdmaTest, FlowControl_AddCredit) {
    // Completion objects must live longer than the connections
    BufferCompletion sendCompletion;
    BufferCompletion receiveCompletion;
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    // Configure receiver to use smaller buffer than sender, and ensure receive buffer
    // is queued after sender queues by using external buffer
    const size_t sendSize = 100;
    const size_t receiveSize = 10;
    std::vector<uint8_t> recvBuffer(receiveSize);
    std::vector<uint8_t> sendBuffer(sendSize);
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(sendSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(recvBuffer.data(), recvBuffer.size(), 1));

    // Send should just be queued and not complete until we add credit
    RDMA_ASSERT_NO_THROW(connections.sender.SendWithCallback(sendBuffer, &sendCompletion));
    RDMA_ASSERT_THROW_WITHCODE(sendCompletion.WaitForCompletion(10), -1); // Should timeout on test side waiting for completion

    // Add credit by queuing buffer, triggering error in AddCredit
    RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(recvBuffer.data(), receiveSize, &receiveCompletion, nullptr, 10));

    // Due to the design of the the internals, the asynchronous failure during the credit addition does not
    // trigger an error in the callback of the Send, it simply times out. Currently you only can see that error
    // on a subsequent operation. The reciever also is not notified since the sender simply doesn't send anything.
    RDMA_EXPECT_THROW_WITHCODE(sendCompletion.WaitForCompletion(10), -1);
    RDMA_EXPECT_THROW_WITHCODE(receiveCompletion.WaitForCompletion(10), -1);

    // Try sending again, but this time the proper size. This will now fail because the session cached the failure and keeps it
    // indefinitely.
    RDMA_EXPECT_THROW_WITHCODE(connections.sender.SendBlankData(receiveSize), easyrdma_Error_SendTooLargeForRecvBuffer);
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffer
}

TEST_P(RdmaTest, Send_NoQueuedRx) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    const size_t numSendBuffers = 3;
    std::vector<uint8_t> receiveBuffer(bufferSize);
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, numSendBuffers));
    // External buffer requires manual queuing, which we won't do
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(receiveBuffer.data(), receiveBuffer.size(), 1));

    std::vector<uint8_t> sendBuffer(bufferSize);
    // We can queue as many buffers as we set the max overlapped count without any Rx queued because it just queues on the sender side
    for(size_t i=0; i < numSendBuffers; ++i) {
        RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    }
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.Send(sendBuffer, 10), easyrdma_Error_Timeout);
    RDMA_ASSERT_NO_THROW(connections.Close()); // Explicitly close the sessions before destroying the external buffer
}


TEST_P(RdmaTest, Receive_Timeout) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.Receive(10), easyrdma_Error_Timeout);
}


TEST_P(RdmaTest, Receive_Cancel_WithClose) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    auto receive = std::async(std::launch::async, [&]() {
        return connections.receiver.Receive(5000);
    } );
    // Give time for async receive thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Destroy receiver to cancel
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(connections.receiver.Close());
    RDMA_ASSERT_THROW_WITHCODE(receive.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(500));
}

TEST_P(RdmaTest, Receive_Cancel_WithAbort) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    auto receive = std::async(std::launch::async, [&]() {
        return connections.receiver.Receive(5000);
    } );
    // Give time for async receive thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Abort receiver
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(connections.receiver.Abort());

    // Abort again to make sure it is harmless
    RDMA_ASSERT_NO_THROW(connections.receiver.Abort());

    RDMA_ASSERT_THROW_WITHCODE(receive.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(500));
}


TEST_P(RdmaTest, Receive_MultipleSimultaneous) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    auto receive = std::async(std::launch::async, [&]() {
        return connections.receiver.Receive(200);
    } );
    // Make sure first recv has started, then initiate second recv. Should immediately fail.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    RDMA_EXPECT_THROW_WITHCODE(connections.receiver.Receive(50), easyrdma_Error_BufferWaitInProgress);
    RDMA_EXPECT_THROW_WITHCODE(receive.get(), easyrdma_Error_Timeout);
}


TEST_P(RdmaTest, Send_Cancel_WithClose) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    // First send two sends should work because we have one receive buffer. First
    // one gets transferred, then the second should get queued on the send side
    std::vector<uint8_t> sendBuffer(bufferSize);
    RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));

    // Second send should block
    auto blockingSend = std::async(std::launch::async, [&]() {
        return connections.sender.Send(sendBuffer, 5000);
    } );
    // Give time for async receive thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Destroy sender to cancel
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(connections.sender.Close());
    RDMA_ASSERT_THROW_WITHCODE(blockingSend.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(500));
}

TEST_P(RdmaTest, Send_Cancel_WithAbort) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    // First send two sends should work because we have one receive buffer. First
    // one gets transferred, then the second should get queued on the send side
    std::vector<uint8_t> sendBuffer(bufferSize);
    RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));

    // Second send should block
    auto blockingSend = std::async(std::launch::async, [&]() {
        return connections.sender.Send(sendBuffer, 5000);
    } );
    // Give time for async receive thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Abort sender
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(connections.sender.Abort());

    // Abort again and make sure it is harmless
    RDMA_ASSERT_NO_THROW(connections.sender.Abort());

    RDMA_ASSERT_THROW_WITHCODE(blockingSend.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(500));
}

TEST_P(RdmaTest, Sender_Close_Does_Not_Abort_Already_Recv) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 40960*1024; // Use large buffers so transfer takes a while
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 6));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 6));
    std::vector<uint8_t> sendBuffer(bufferSize);

    // Send 5 buffers
    for(int i=0; i < 5; i++) {
        RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    }
    // Wait for sends to complete
    auto pollStart = std::chrono::steady_clock::now();
    while(connections.sender.GetPropertyU64(easyrdma_Property_QueuedBuffers) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_TRUE(std::chrono::steady_clock::now()-pollStart < std::chrono::milliseconds(500));
    }
    // Close sender
    RDMA_ASSERT_NO_THROW(connections.sender.Close());

    // Receive 5 buffers without error (both with timeout and without)
    BufferRegion region;
    RDMA_ASSERT_NO_THROW(region = connections.receiver.GetReceivedRegion(100));
    // Releasing the region should silently move the buffer to idle if it can't re-queue
    // due to disconnection
    RDMA_ASSERT_NO_THROW(connections.receiver.ReleaseReceivedRegion(region));
    // Releasing again should now fail as expected
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.ReleaseReceivedRegion(region), easyrdma_Error_InvalidOperation);

    // Waiting for remaining buffers with different timeout should not fail.
    RDMA_ASSERT_NO_THROW(connections.receiver.ReceiveBlankData(0));
    RDMA_ASSERT_NO_THROW(connections.receiver.ReceiveBlankData(1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ReceiveBlankData(10));
    RDMA_ASSERT_NO_THROW(connections.receiver.ReceiveBlankData(-1));

    // Further recv calls should fail
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.Receive(0), easyrdma_Error_Disconnected);
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.Receive(100), easyrdma_Error_Disconnected);
}


TEST_P(RdmaTest, Connect_Error_BadPort) {
    RdmaAddress localAddressConnector = GetEndpointAddresses().first;
    RdmaAddress localAddressToConnect = GetEndpointAddresses().second;
    localAddressToConnect.SetPort(3); // unused port

    Session sessionConnector;
    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    try {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressToConnect.GetAddrString(), localAddressToConnect.GetPort());
        ASSERT_FALSE(true) << "Should have asserted!";
    }
    catch(const RdmaTestException& e) {
        #ifdef _WIN32
            const int expectedErrorCode = easyrdma_Error_ConnectionRefused;
        #else
            const int expectedErrorCode = easyrdma_Error_UnableToConnect;
        #endif
        ASSERT_EQ(e.errorCode, expectedErrorCode);
    }
    catch(std::exception&) {
        RDMA_ASSERT_NO_THROW(throw);
    }
}

TEST_P(RdmaTest, Connect_Error_BadRemoteAddress) {
    RdmaAddress localAddressConnector = GetEndpointAddresses().first;
    RdmaAddress localAddressToConnect = RdmaAddress("8.8.8.8", 5000);
    Session sessionConnector;

    RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
    try {
        sessionConnector.Connect(easyrdma_Direction_Send, localAddressToConnect.GetAddrString(), localAddressToConnect.GetPort(), 50);
        ASSERT_FALSE(true) << "Should have asserted!";
    }
    catch(const RdmaTestException& e) {
        ASSERT_TRUE(e.errorCode == easyrdma_Error_Timeout || e.errorCode == easyrdma_Error_UnableToConnect);
    }
    catch(std::exception&) {
        RDMA_ASSERT_NO_THROW(throw);
    }
}


TEST_P(RdmaTest, Connect_Error_BadRemoteAddressString) {
    RdmaAddress localAddressConnector = GetEndpointAddresses().first;

    const char* addresses[] = {
        "address.invalid",
        "",
        "1.2.3.4.5"
    };

    for(auto& addr : addresses) {
        Session sessionConnector;
        RDMA_ASSERT_NO_THROW(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()));
        RDMA_EXPECT_THROW_WITHCODE(sessionConnector.Connect(easyrdma_Direction_Send, addr, 5000, 50), easyrdma_Error_InvalidAddress);
    }
}

TEST_P(RdmaTest, Connect_Error_BadLocalAddressString) {
    const char* addresses[] = {
        "address.invalid",
        "",
        "1.2.3.4.5"
    };

    for(auto& addr : addresses) {
        Session sessionConnector;
        RDMA_EXPECT_THROW_WITHCODE(sessionConnector = Session::CreateConnector(addr, 5000), easyrdma_Error_InvalidAddress);
    }
}

TEST_P(RdmaTest, Listen_Error_BadLocalAddressString) {
    const char* addresses[] = {
        "address.invalid",
        "",
        "1.2.3.4.5"
    };

    for(auto& addr : addresses) {
        Session sessionListener;
        RDMA_EXPECT_THROW_WITHCODE(sessionListener = Session::CreateListener(addr, 5000), easyrdma_Error_InvalidAddress);
    }
}

TEST_P(RdmaTest, Listen_Error_InvalidLocalAddress) {
    const char* addresses[] = {
        "169.254.0.1", // LLA address that is in reserved range to won't be automatically assigned
    };

    for(auto& addr : addresses) {
        Session sessionListener;
        RDMA_EXPECT_THROW_WITHCODE(sessionListener = Session::CreateListener(addr, 5000), easyrdma_Error_InvalidAddress);
    }
}

TEST_P(RdmaTest, Connect_Error_BadLocalAddress) {
    RdmaAddress localAddressConnector = RdmaAddress("8.8.8.8", 5000);
    Session sessionConnector;
    RDMA_ASSERT_THROW_WITHCODE(sessionConnector = Session::CreateConnector(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()), easyrdma_Error_InvalidAddress);
}

TEST_P(RdmaTest, Listen_Error_BadLocalAddress) {
    Session sessionListener;
    RdmaAddress localAddressConnector = RdmaAddress("8.8.8.8", 5000);
    RDMA_ASSERT_THROW_WITHCODE(sessionListener = Session::CreateListener(localAddressConnector.GetAddrString(), localAddressConnector.GetPort()), easyrdma_Error_InvalidAddress);
}

TEST_P(RdmaTest, Connect_Error_InvalidDirection) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
    Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);

    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Send);
    } );
    #ifdef _WIN32
        const int expectedErrorCode = easyrdma_Error_ConnectionRefused;
    #else
        const int expectedErrorCode = easyrdma_Error_UnableToConnect;
    #endif
    RDMA_ASSERT_THROW_WITHCODE(sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort()), expectedErrorCode);
    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_InvalidDirection);
}

TEST_P(RdmaTest, Accept_Error_InvalidDirection) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
    Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);
    // Setting the connection data to the wrong direction to force us down the invalid data codepath on the other side
    EasyRDMA::easyrdma_ConnectionData cd = {
        kConnectionDataProtocol,
        1,
        1,
        static_cast<uint8_t>(easyrdma_Direction_Send)
    };
    sessionListener.SetProperty(easyrdma_Property_ConnectionData, &cd, sizeof(cd));

    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive, 100);
    } );
    RDMA_ASSERT_THROW_WITHCODE(sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort()), easyrdma_Error_InvalidDirection);
#ifdef _WIN32
    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_Timeout);
#else
    // On Linux, there is no mechanism to cause the accept to fail after the data has been sent to the connect side
    RDMA_ASSERT_NO_THROW(accept.get());
#endif
}


TEST_P(RdmaTest, ConnectionData_SetExpected) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
    Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);
    easyrdma_ConnectionData cd = {
        kConnectionDataProtocol,
        1,
        1,
        static_cast<uint8_t>(easyrdma_Direction_Receive)
    };
    sessionListener.SetProperty(easyrdma_Property_ConnectionData, &cd, sizeof(cd));
    cd.direction = static_cast<uint8_t>(easyrdma_Direction_Send);
    sessionConnectorSender.SetProperty(easyrdma_Property_ConnectionData, &cd, sizeof(cd));
    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive);
    } );
    RDMA_ASSERT_NO_THROW(sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort()));
    RDMA_ASSERT_NO_THROW(accept.get());
}

TEST_P(RdmaTest, ConnectionData_InvalidProtocol) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
    Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);
    char buffer[] = "garbage";
    sessionConnectorSender.SetProperty(easyrdma_Property_ConnectionData, &buffer, sizeof(buffer));
    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive);
    } );
    #ifdef _WIN32
        const int expectedErrorCode = easyrdma_Error_ConnectionRefused;
    #else
        const int expectedErrorCode = easyrdma_Error_UnableToConnect;
    #endif
    RDMA_ASSERT_THROW_WITHCODE(sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort()), expectedErrorCode);
    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_IncompatibleProtocol);
}

TEST_P(RdmaTest, ConnectionData_NewerCompatibleVersion) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
    Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);
    size_t updatedConnectionDataSize = sizeof(easyrdma_ConnectionData) + 3; // totally arbitrary larger data size
    std::vector<uint8_t> connectionDataBuffer(updatedConnectionDataSize, 0);
    easyrdma_ConnectionData& cd = reinterpret_cast<easyrdma_ConnectionData&>(*connectionDataBuffer.data());
    cd.protocolId = kConnectionDataProtocol;
    cd.protocolVersion = 3; // newer than 1!
    cd.oldestCompatibleVersion = 1; // backwards compatible with initial release
    cd.direction = static_cast<uint8_t>(easyrdma_Direction_Send);
    sessionConnectorSender.SetProperty(easyrdma_Property_ConnectionData, connectionDataBuffer.data(), connectionDataBuffer.size());
    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive);
    } );
    RDMA_ASSERT_NO_THROW(sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort()));
    RDMA_ASSERT_NO_THROW(accept.get());
}

TEST_P(RdmaTest, ConnectionData_NewerIncompatibleVersion) {
    auto endpoints = GetEndpointAddresses();
    RdmaAddress localAddressListener = endpoints.first;
    RdmaAddress localAddressConnector = endpoints.second;

    std::future<Session> accept;
    Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
    Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);
    size_t updatedConnectionDataSize = sizeof(easyrdma_ConnectionData) + 3; // totally arbitrary larger data size
    std::vector<uint8_t> connectionDataBuffer(updatedConnectionDataSize, 0);
    easyrdma_ConnectionData& cd = reinterpret_cast<easyrdma_ConnectionData&>(*connectionDataBuffer.data());
    cd.protocolId = kConnectionDataProtocol;
    cd.protocolVersion = 3; // newer than 1!
    cd.oldestCompatibleVersion = 2; // NOT backwards compatible with initial release
    cd.direction = static_cast<uint8_t>(easyrdma_Direction_Send);
    sessionConnectorSender.SetProperty(easyrdma_Property_ConnectionData, connectionDataBuffer.data(), connectionDataBuffer.size());
    accept = std::async(std::launch::async, [&]() {
        return sessionListener.Accept(easyrdma_Direction_Receive);
    } );
    #ifdef _WIN32
        const int expectedErrorCode = easyrdma_Error_ConnectionRefused;
    #else
        const int expectedErrorCode = easyrdma_Error_UnableToConnect;
    #endif
    RDMA_ASSERT_THROW_WITHCODE(sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort()), expectedErrorCode);
    RDMA_ASSERT_THROW_WITHCODE(accept.get(), easyrdma_Error_IncompatibleVersion);
}

TEST_P(RdmaTest, SendAfterDisconnect) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    // Close receiver and make sure disconnect gets handled on other end
    RDMA_ASSERT_NO_THROW(connections.receiver.Close());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send fails
    std::vector<uint8_t> sendBuffer(bufferSize);
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.Send(sendBuffer), easyrdma_Error_Disconnected);
}

TEST_P(RdmaTest, ReceiveAfterDisconnect) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    // Close sender and make sure disconnect gets handled on other end
    RDMA_ASSERT_NO_THROW(connections.sender.Close());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // New wait for receive fails
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.Receive(), easyrdma_Error_Disconnected);
}

TEST_P(RdmaTest, ReceiveDuringDisconnect) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    // Start receive and allow thread to start
    auto receive = std::async(std::launch::async, [&]() {
        return connections.receiver.Receive();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Close sender and make sure disconnect gets handled on other end
    RDMA_ASSERT_NO_THROW(connections.sender.Close());

    // In progress wait fails
    RDMA_ASSERT_THROW_WITHCODE(receive.get(), easyrdma_Error_Disconnected);
}



TEST_P(RdmaTest, Property_QueuedBuffers) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));

    // Receiver always queues all buffers. Sender queues on send
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(1U, connections.receiver.GetPropertyU64(easyrdma_Property_QueuedBuffers)));
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(0U, connections.sender.GetPropertyU64(easyrdma_Property_QueuedBuffers)));
}

TEST_P(RdmaTest, Property_SessionsOpened) {
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumOpenedSessions)));
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(2U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumOpenedSessions)));
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
    RDMA_ASSERT_NO_THROW(connections.Close());
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumOpenedSessions)));
    RDMA_ASSERT_NO_THROW(EXPECT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
}


TEST_P(RdmaTest, SendReceive_Continuous) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t kNumBuffers = 10;
    const size_t kEachTransferSize = 1024;
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kEachTransferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kEachTransferSize, kNumBuffers));

    // Loop through all the buffers multiple times
    const int kTotalTransfers = kNumBuffers*100;

    // Generate send data
    std::vector<std::vector<uint8_t>> sendData(kTotalTransfers, std::vector<uint8_t>(kEachTransferSize));
    for(auto& xferBuffer : sendData) {
        for(auto& byte : xferBuffer) {
            byte = static_cast<uint8_t>(rand());
        }
    }

    // recv loop
    auto receiver = std::async(std::launch::async, [&]() {
        for(size_t i = 0; i < kTotalTransfers; ++i) {
            std::vector<uint8_t> data;
            RDMA_ASSERT_NO_THROW(data = std::move(connections.receiver.Receive()));
            EXPECT_EQ(data.size(), kEachTransferSize);
            EXPECT_EQ(0, memcmp(data.data(), sendData[i].data(), kEachTransferSize)) << "Iteration: " << i;
        }
    });

    // send loop
    auto sender = std::async(std::launch::async, [&]() {
        for(size_t i = 0; i < kTotalTransfers; ++i) {
            RDMA_ASSERT_NO_THROW(connections.sender.Send(sendData[i]))  << "Iteration: " << i;
        }
    });
}

TEST_P(RdmaTest, TestBandwidth) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t kNumBuffers = 10;
    const int kEachTransferSize = 1024*1024;
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kEachTransferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kEachTransferSize, kNumBuffers));

    const int count = 1000;
    int64_t totalSendSize = 0;
    auto start = std::chrono::steady_clock::now();

    int64_t totalRecvSize = 0;
    auto receiver = std::async(std::launch::async, [&]() {
        for(int i = 0; i < count; ++i) {
            RDMA_ASSERT_NO_THROW(totalRecvSize += connections.receiver.ReceiveBlankData()) << "Iteration: " << i;
        }
    });

    auto sender = std::async(std::launch::async, [&]() {
        for(int i = 0; i < count; ++i) {
            RDMA_ASSERT_NO_THROW(connections.sender.SendBlankData(kEachTransferSize)) << "Iteration: " << i;
            totalSendSize += kEachTransferSize;
        }
    });

    RDMA_ASSERT_NO_THROW(sender.get());
    RDMA_ASSERT_NO_THROW(receiver.get());

    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    double bwGbitsPerSec = (totalRecvSize*8 / 1000000000.0) / (durationMs / 1000.0);
    double bwGBPerSec = (totalRecvSize / (1024.0*1024.0*1024.0)) / (durationMs / 1000.0);
    info() << "Bandwidth: " << bwGbitsPerSec << "Gbit/s; " << bwGBPerSec << "GB/s";
    ::testing::Test::RecordProperty("Bandwidth_Gbit/s", std::to_string(bwGbitsPerSec));
}

TEST_P(RdmaTest, TestLatency) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const int count = 1000;
    const size_t kTransferSize = 128;
    std::vector<int64_t> durations(count);
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kTransferSize, 10));
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kTransferSize, 10));

    for(int iteration = 0; iteration < count; ++iteration) {

        auto bufferRegion = connections.sender.GetSendRegion();

        // Timestamp before we queue send to after we recv. We are intentionally not timing copying the data out of the buffer
        auto start = std::chrono::steady_clock::now();
        RDMA_ASSERT_NO_THROW(connections.sender.QueueRegion(bufferRegion));
        RDMA_ASSERT_NO_THROW(connections.receiver.ReceiveBlankData());
        durations[iteration] = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    }

    double nanosecAccum = 0;
    for(auto n : durations) {
        nanosecAccum += n;
    }

    double latencyUs = nanosecAccum / count / 1000;
    info() << "Average one-way latency: " << latencyUs << "us";
    ::testing::Test::RecordProperty("One-way latency (us)", std::to_string(latencyUs));
}

TEST_P(RdmaTest, FlowControl_Internal) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    const int kSendBuffers = 5;
    const int kRecvBuffers = 1; // use only one buffer so receiver can push back
    const size_t kEachTransferSize = 1024;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kEachTransferSize, kSendBuffers));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kEachTransferSize, kRecvBuffers));

    // Loop through all the buffers multiple times
    const int kTotalTransfers = 15;

    // Generate send data
    std::vector<std::vector<uint8_t>> sendData(kSendBuffers, std::vector<uint8_t>(kEachTransferSize));
    for(auto& xferBuffer : sendData) {
        for(auto& byte : xferBuffer) {
            byte = static_cast<uint8_t>(rand());
        }
    }

    // recv loop
    auto receiver = std::async(std::launch::async, [&]() {
        for(size_t i = 0; i < kTotalTransfers; ++i) {
            // Throttle recv to test flow control
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            std::vector<uint8_t> data;
            RDMA_ASSERT_NO_THROW(data = std::move(connections.receiver.Receive()));
            EXPECT_EQ(data.size(), kEachTransferSize);
            const auto& sendBufferData = sendData[i%kSendBuffers];
            EXPECT_EQ(0, memcmp(data.data(), sendBufferData.data(), kEachTransferSize)) << "Iteration: " << i;
        }
    });


    // send loop
    auto sender = std::async(std::launch::async, [&]() {
        for(size_t i = 0; i < kTotalTransfers; ++i) {
            auto& sendBufferData = sendData[i%kSendBuffers];
            RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBufferData)) << "Iteration: " << i;
        }
    });
}

TEST_P(RdmaTest, QueueBuffer_SendOutOfOrder) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 1;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 2));

    auto bufferRegion1 = connections.sender.GetSendRegion();
    bufferRegion1.CopyFromVector( {1} );
    auto bufferRegion2 = connections.sender.GetSendRegion();
    bufferRegion2.CopyFromVector( {2} );

    // Queue in reverse order we got regions
    RDMA_ASSERT_NO_THROW(connections.sender.QueueRegion(bufferRegion2));
    RDMA_ASSERT_NO_THROW(connections.sender.QueueRegion(bufferRegion1));

    // Data should match send order
    RDMA_ASSERT_NO_THROW( EXPECT_EQ( std::vector<uint8_t>({2}), connections.receiver.Receive()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ( std::vector<uint8_t>({1}), connections.receiver.Receive()));
}

TEST_P(RdmaTest, QueueBuffer_ReleaseReceiveOutOfOrder) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 1;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 5));

    // Send two buffers
    uint8_t sendSequence = 0;
    uint8_t recvSequence = 0;
    RDMA_ASSERT_NO_THROW(connections.sender.Send({sendSequence++}));
    RDMA_ASSERT_NO_THROW(connections.sender.Send({sendSequence++}));

    // First receive the two buffers
    BufferRegion region1, region2;
    RDMA_ASSERT_NO_THROW(region1 = connections.receiver.GetReceivedRegion());
    EXPECT_EQ(region1.ToVector(), std::vector<uint8_t>({recvSequence++}));
    RDMA_ASSERT_NO_THROW(region2 = connections.receiver.GetReceivedRegion());
    EXPECT_EQ(region2.ToVector(), std::vector<uint8_t>({recvSequence++}));

    // Release in opposite order
    RDMA_ASSERT_NO_THROW(connections.receiver.ReleaseReceivedRegion(region2));
    RDMA_ASSERT_NO_THROW(connections.receiver.ReleaseReceivedRegion(region1));

    // Send again and expect data is still in correct order
    RDMA_ASSERT_NO_THROW(connections.sender.Send({sendSequence++}));
    RDMA_ASSERT_NO_THROW(connections.sender.Send({sendSequence++}));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ( std::vector<uint8_t>({recvSequence++}), connections.receiver.Receive()));
    RDMA_ASSERT_NO_THROW( EXPECT_EQ( std::vector<uint8_t>({recvSequence++}), connections.receiver.Receive()));
}

TEST_P(RdmaTest, QueueBuffer_SendBufferTwice) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 1;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 5));

    auto bufferRegion = connections.sender.GetSendRegion();
    RDMA_ASSERT_NO_THROW(connections.sender.QueueRegion(bufferRegion));

    // Try to queue same buffer region again, even though we never retrieved it again
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.QueueRegion(bufferRegion), easyrdma_Error_InvalidOperation);
}

TEST_P(RdmaTest, QueueBuffer_ReleaseReceivedBufferTwice) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 1024;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 5));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 5));

    RDMA_ASSERT_NO_THROW(connections.sender.SendBlankData(bufferSize));

    BufferRegion region;
    RDMA_ASSERT_NO_THROW(region = connections.receiver.GetReceivedRegion());
    RDMA_ASSERT_NO_THROW(connections.receiver.ReleaseReceivedRegion(region));

    // Try to release same buffer region again, even though we never retrieved it again
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.ReleaseReceivedRegion(region), easyrdma_Error_InvalidOperation);
}


TEST_P(RdmaTest, Scaling_Connections) {
    // First make connections in parallel
    const size_t kNumConnections = 100;
    const size_t kBufferSize = 4096;
    std::vector<ConnectionPair> connections;
    std::vector<std::future<ConnectionPair>> connectionAttempts;
    for(size_t i = 0; i < kNumConnections; ++i) {
        connectionAttempts.push_back(std::async(std::launch::async, [&]() {
            return std::move(GetLoopbackConnection());
        }));
    }
    for(size_t i = 0; i < kNumConnections; ++i) {
        RDMA_ASSERT_NO_THROW(connections.push_back(std::move(connectionAttempts[i].get())));
    }

    // Configure in parallel
    std::vector<std::future<void>> configureFutures;
    for(auto& connection : connections) {
        configureFutures.push_back(std::async(std::launch::async, [&]() {
            connection.sender.ConfigureBuffers(kBufferSize, 10);
            connection.receiver.ConfigureBuffers(kBufferSize, 10);
        }));
    }
    for(auto& configure : configureFutures) {
        RDMA_ASSERT_NO_THROW(configure.get());
    }

    // Send/recv
    std::vector<std::future<void>> transferFutures;
    for(auto& connection : connections) {
        transferFutures.push_back(std::async(std::launch::async, [&]() {
            std::vector<uint8_t> receiveBuffer;
            for(size_t i = 0; i < 100; ++i) {
                std::vector<uint8_t> sendBuffer(kBufferSize);
                for(size_t j=0; j < kBufferSize; ++j) {
                    sendBuffer[j] = static_cast<uint8_t>(i+j);
                }
                connection.sender.Send(sendBuffer);
                receiveBuffer = connection.receiver.Receive();
                ASSERT_EQ(sendBuffer.size(), receiveBuffer.size());
                EXPECT_EQ(sendBuffer, receiveBuffer);
            }
        }));
    }
    for(auto& transfer : transferFutures) {
        RDMA_ASSERT_NO_THROW(transfer.get());
    }
}

TEST_P(RdmaTest, Scaling_Buffers_LessThanCreditCount) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t kBufferSize = 4096;
    const size_t kNumBuffers = 100; // Currently the dll uses 100 buffers for issuing credits

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kBufferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kBufferSize, kNumBuffers));

    std::vector<uint8_t> sendBuffer(kBufferSize);
    for(size_t i=0; i < kBufferSize; ++i) {
        sendBuffer[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> receiveBuffer;

    for(size_t i = 0; i < kNumBuffers; ++i) {
        RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    }

    for(size_t i = 0; i < kNumBuffers; ++i) {
        RDMA_ASSERT_NO_THROW(receiveBuffer = connections.receiver.Receive());
        ASSERT_EQ(sendBuffer.size(), receiveBuffer.size());
        EXPECT_EQ(sendBuffer, receiveBuffer);
    }
}

TEST_P(RdmaTest, Scaling_Buffers_MoreThanCreditCount) {
    // This test occasionally takes a long time to run (see #1622458)
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t kBufferSize = 4096;
    const size_t kNumBuffers = 1024;

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kBufferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kBufferSize, kNumBuffers));

    std::vector<uint8_t> sendBuffer(kBufferSize);
    for(size_t i=0; i < kBufferSize; ++i) {
        sendBuffer[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> receiveBuffer;

    for(size_t i = 0; i < kNumBuffers; ++i) {
        RDMA_ASSERT_NO_THROW(connections.sender.Send(sendBuffer));
    }

    for(size_t i = 0; i < kNumBuffers; ++i) {
        RDMA_ASSERT_NO_THROW(receiveBuffer = connections.receiver.Receive());
        ASSERT_EQ(sendBuffer.size(), receiveBuffer.size());
        EXPECT_EQ(sendBuffer, receiveBuffer);
    }
}

TEST_P(RdmaTest, SendCallbacks_CancellationFromWaitingForCredits) {
    const int rxOverlappedCount = 25;
    const int transferCount = 50;
    const size_t eachBufferLen = 4096;
    std::vector<uint8_t> sendBuffer(eachBufferLen);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), transferCount));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(eachBufferLen, rxOverlappedCount));

    BufferCompletion completedTransfers[transferCount];
    auto sender = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            RDMA_ASSERT_NO_THROW(connections.sender.QueueExternalBufferWithCallback(sendBuffer.data(), sendBuffer.size(), &completedTransfers[i], nullptr, 100));
        }
    });
    RDMA_ASSERT_NO_THROW(sender.get());

    // Now 50 should be queued, but only 25 sent
    for(int i = 0; i < rxOverlappedCount; ++i) {
        RDMA_ASSERT_NO_THROW(completedTransfers[i].WaitForCompletion(100));
    }
    // 25th should be queued waiting for a free buffer and not complete
    EXPECT_THROW(completedTransfers[rxOverlappedCount].WaitForCompletion(50), std::runtime_error);

    RDMA_EXPECT_NO_THROW(connections.Close());

    // Now all the buffers should have completed with a cancelled state
    for(int i = rxOverlappedCount; i < transferCount; ++i) {
        RDMA_ASSERT_THROW_WITHCODE(completedTransfers[i].WaitForCompletion(0), easyrdma_Error_OperationCancelled);
    }
}

TEST_P(RdmaTest, SendCallbacks_CancellationFromQueued) {
    const int rxOverlappedCount = 500;
    const int transferCount = 500;
    const size_t eachBufferLen = 1024*1024;
    std::vector<uint8_t> sendBuffer(eachBufferLen);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), transferCount));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(eachBufferLen, rxOverlappedCount));

    BufferCompletion completedTransfers[transferCount];
    auto sender = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            RDMA_ASSERT_NO_THROW(connections.sender.QueueExternalBufferWithCallback(sendBuffer.data(), sendBuffer.size(), &completedTransfers[i], nullptr, 100));
        }
    });
    RDMA_ASSERT_NO_THROW(sender.get());

    RDMA_ASSERT_NO_THROW(connections.sender.Close());

    // Now all the buffers should have completed with a cancelled state
    bool sawError = false;
    int successfullCompletions = 0;
    for(int i = 0; i < transferCount; ++i) {
        try {
            completedTransfers[i].WaitForCompletion(0);
            ++successfullCompletions;
            // Once we saw an error, we should never get successful completions afterwards, since they
            // complete in-order
            ASSERT_FALSE(sawError);
        }
        catch(std::exception&) {
            sawError = true;
            RDMA_ASSERT_THROW_WITHCODE(throw, easyrdma_Error_OperationCancelled) << "At buffer: " << i;
        }
    }
    info() << "Completed " << successfullCompletions << " buffers before cancellation";

    RDMA_ASSERT_NO_THROW(connections.Close());
}

TEST_P(RdmaTest, SendCallbacks_DisconnectFromQueued) {
    const int rxOverlappedCount = 500;
    const int transferCount = 500;
    const size_t eachBufferLen = 1024*1024;
    std::vector<uint8_t> sendBuffer(eachBufferLen);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureExternalBuffer(sendBuffer.data(), sendBuffer.size(), transferCount));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(eachBufferLen, rxOverlappedCount));

    BufferCompletion completedTransfers[transferCount];
    auto sender = std::async(std::launch::async, [&]() {
        for(int i = 0; i < transferCount; ++i) {
            RDMA_ASSERT_NO_THROW(connections.sender.QueueExternalBufferWithCallback(sendBuffer.data(), sendBuffer.size(), &completedTransfers[i], nullptr, 100));
        }
    });
    RDMA_ASSERT_NO_THROW(sender.get());

    // Close receiver and wait for sender to notice
    RDMA_ASSERT_NO_THROW(connections.receiver.Close());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_FALSE(connections.sender.GetPropertyBool(easyrdma_Property_Connected));

    // Now all the buffers should have completed with a cancelled state
    bool sawError = false;
    int successfullCompletions = 0;
    for(int i = 0; i < transferCount; ++i) {
        try {
            completedTransfers[i].WaitForCompletion(0);
            ++successfullCompletions;
            // Once we saw an error, we should never get successful completions afterwards, since they
            // complete in-order
            ASSERT_FALSE(sawError);
        }
        catch(std::exception&) {
            sawError = true;
            RDMA_ASSERT_THROW_WITHCODE(throw, easyrdma_Error_Disconnected) << "At buffer: " << i;
        }
    }
    info() << "Completed " << successfullCompletions << " buffers before cancellation";
}

TEST_P(RdmaTest, RecvCallbacks_CancellationFromQueued) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    const int bufferCount = 20;
    const size_t eachBufferLen = 1024*1024;
    std::vector<uint8_t> largeRecvBuffer(bufferCount*eachBufferLen);

    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(largeRecvBuffer.data(), largeRecvBuffer.size(), bufferCount));

    // Queue all rx buffers at start
    BufferCompletion completedTransfers[bufferCount];
    for(int i = 0; i < bufferCount; ++i) {
        RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(largeRecvBuffer.data() + (i % bufferCount)*eachBufferLen,
                                                                                eachBufferLen, &completedTransfers[i], nullptr, 1000));
    }

    RDMA_ASSERT_NO_THROW(connections.receiver.Close());

    // Now all the buffers should have completed with a cancelled state
    for(int i = 0; i < bufferCount; ++i) {
        RDMA_ASSERT_THROW_WITHCODE(completedTransfers[i].WaitForCompletion(0), easyrdma_Error_OperationCancelled);
    }

    RDMA_ASSERT_NO_THROW(connections.Close());
}

TEST_P(RdmaTest, RecvCallbacks_DisconnectFromQueued) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    const int bufferCount = 20;
    const size_t eachBufferLen = 1024*1024;
    std::vector<uint8_t> largeRecvBuffer(bufferCount*eachBufferLen);

    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureExternalBuffer(largeRecvBuffer.data(), largeRecvBuffer.size(), bufferCount));

    // Queue all rx buffers at start
    BufferCompletion completedTransfers[bufferCount];
    for(int i = 0; i < bufferCount; ++i) {
        RDMA_ASSERT_NO_THROW(connections.receiver.QueueExternalBufferWithCallback(largeRecvBuffer.data() + (i % bufferCount)*eachBufferLen,
                                                                                eachBufferLen, &completedTransfers[i], nullptr, 1000));
    }

    // Close sender and wait for receiver to notice
    RDMA_ASSERT_NO_THROW(connections.sender.Close());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_FALSE(connections.receiver.GetPropertyBool(easyrdma_Property_Connected));

    // Now all the buffers should have completed with a cancelled state
    for(int i = 0; i < bufferCount; ++i) {
        RDMA_ASSERT_THROW_WITHCODE(completedTransfers[i].WaitForCompletion(0), easyrdma_Error_Disconnected);
    }

    RDMA_ASSERT_NO_THROW(connections.Close());
}

TEST_P(RdmaTest, Delayed_Destruction_Sender) {
    RDMA_ASSERT_NO_THROW(ASSERT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t kNumBuffers = 10;
    const size_t kBufferSize = 1024*1024;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kBufferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kBufferSize, kNumBuffers));

    std::vector<BufferRegion> sendRegions(kNumBuffers);
    for(size_t i = 0; i < sendRegions.size(); ++i) {
        RDMA_ASSERT_NO_THROW(sendRegions[i] = connections.sender.GetSendRegion());
    }
    RDMA_ASSERT_NO_THROW(connections.receiver.Close(easyrdma_CloseFlags_DeferWhileUserBuffersOutstanding));
    auto savedSenderHandle = connections.sender.GetSessionHandle();
    RDMA_ASSERT_NO_THROW(connections.sender.Close(easyrdma_CloseFlags_DeferWhileUserBuffersOutstanding));
    RDMA_ASSERT_NO_THROW(ASSERT_EQ(1U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
    RDMA_ASSERT_NO_THROW(ASSERT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumOpenedSessions)));

    // Attempt to access sender memory
    for(size_t i = 0; i < sendRegions.size(); ++i) {
        RDMA_ASSERT_NO_THROW(sendRegions[i].ToVector());
    }

    // Now release the buffers
    for(size_t i = 0; i < sendRegions.size(); ++i) {
        RDMA_ASSERT_NO_THROW(Session::ReleaseUserRegionToIdle(savedSenderHandle, sendRegions[i]));
    }

    RDMA_ASSERT_NO_THROW(ASSERT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
}

TEST_P(RdmaTest, Delayed_Destruction_Receiver) {
    RDMA_ASSERT_NO_THROW(ASSERT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t kNumBuffers = 10;
    const size_t kBufferSize = 1024*1024;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kBufferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kBufferSize, kNumBuffers));

    for(size_t i = 0; i < kNumBuffers; ++i) {
        RDMA_ASSERT_NO_THROW(connections.sender.SendBlankData(kBufferSize));
    }
    std::vector<BufferRegion> recvRegions(kNumBuffers);
    for(size_t i = 0; i < recvRegions.size(); ++i) {
        RDMA_ASSERT_NO_THROW(recvRegions[i] = connections.receiver.GetReceivedRegion());
    }

    auto savedReceiverHandle = connections.receiver.GetSessionHandle();
    RDMA_ASSERT_NO_THROW(connections.receiver.Close(easyrdma_CloseFlags_DeferWhileUserBuffersOutstanding));
    RDMA_ASSERT_NO_THROW(connections.sender.Close(easyrdma_CloseFlags_DeferWhileUserBuffersOutstanding));

    RDMA_ASSERT_NO_THROW(ASSERT_EQ(1U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
    RDMA_ASSERT_NO_THROW(ASSERT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumOpenedSessions)));

    // Attempt to access receiver memory
    for(size_t i = 0; i < recvRegions.size(); ++i) {
        RDMA_ASSERT_NO_THROW(recvRegions[i].ToVector());
    }

    // Now release the buffers
    for(size_t i = 0; i < recvRegions.size(); ++i) {
        RDMA_ASSERT_NO_THROW(Session::ReleaseUserRegionToIdle(savedReceiverHandle, recvRegions[i]));
    }

    RDMA_ASSERT_NO_THROW(ASSERT_EQ(0U, Session::GetPropertyOnSession<uint64_t>(easyrdma_InvalidSession, easyrdma_Property_NumPendingDestructionSessions)));
}


TEST_P(RdmaTest, PollingMode_EnableDisable) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());

    RDMA_ASSERT_NO_THROW(ASSERT_EQ(false, connections.sender.GetPropertyBool(easyrdma_Property_UseRxPolling)));
    RDMA_ASSERT_NO_THROW(ASSERT_EQ(false, connections.receiver.GetPropertyBool(easyrdma_Property_UseRxPolling)));

    // Send side can't enable polling
    RDMA_ASSERT_THROW_WITHCODE(connections.sender.SetPropertyBool(easyrdma_Property_UseRxPolling, true), easyrdma_Error_OperationNotSupported);

    // Polling on receiver only supported on Linux
    #ifdef __linux__
        RDMA_ASSERT_NO_THROW(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, true));
        RDMA_ASSERT_NO_THROW(ASSERT_EQ(true, connections.receiver.GetPropertyBool(easyrdma_Property_UseRxPolling)));
    #else
        RDMA_ASSERT_THROW_WITHCODE(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, true), easyrdma_Error_OperationNotSupported);
        RDMA_ASSERT_NO_THROW(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, false));
    #endif
    const size_t kNumBuffers = 10;
    const size_t kBufferSize = 1024;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(kBufferSize, kNumBuffers));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(kBufferSize, kNumBuffers));

    // Can't set it after configure
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, false), easyrdma_Error_AlreadyConfigured);
}

#ifdef __linux__
TEST_P(RdmaTest, PollingMode_RecvAbort) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 4096;

    RDMA_ASSERT_NO_THROW(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, true));

    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 1));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 1));

    auto receive = std::async(std::launch::async, [&]() {
        return connections.receiver.Receive(5000);
    } );
    // Give time for async receive thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Abort receiver
    auto cancelStart = std::chrono::steady_clock::now();
    RDMA_ASSERT_NO_THROW(connections.receiver.Abort());

    // Abort again to make sure it is harmless
    RDMA_ASSERT_NO_THROW(connections.receiver.Abort());

    RDMA_ASSERT_THROW_WITHCODE(receive.get(), easyrdma_Error_OperationCancelled);
    auto cancelDuration = std::chrono::steady_clock::now()-cancelStart;
    EXPECT_LE(cancelDuration, std::chrono::milliseconds(500));
}

TEST_P(RdmaTest, PollingMode_ExternalBuffers) {
    std::vector<uint8_t> receiveBuffer(4096*1024);
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(1024, 10));

    // ConfigureExternal fails polling mode enabled
    RDMA_ASSERT_NO_THROW(connections.receiver.SetPropertyBool(easyrdma_Property_UseRxPolling, true));
    RDMA_ASSERT_THROW_WITHCODE(connections.receiver.ConfigureExternalBuffer(receiveBuffer.data(), receiveBuffer.size(), 10), easyrdma_Error_OperationNotSupported);
}
#endif

TEST_P(RdmaTest, Close_WithUserBuffersHeld) {
    ConnectionPair connections;
    RDMA_ASSERT_NO_THROW(connections = GetLoopbackConnection());
    const size_t bufferSize = 100;
    RDMA_ASSERT_NO_THROW(connections.sender.ConfigureBuffers(bufferSize, 2));
    RDMA_ASSERT_NO_THROW(connections.receiver.ConfigureBuffers(bufferSize, 2));

    // Send a buffer
    RDMA_ASSERT_NO_THROW(connections.sender.SendBlankData(bufferSize));

    // Hold a send region
    BufferRegion sendRegion;
    RDMA_ASSERT_NO_THROW(sendRegion = connections.sender.GetSendRegion());

    // Hold a recv region
    BufferRegion recvRegion;
    RDMA_ASSERT_NO_THROW(recvRegion = connections.receiver.GetReceivedRegion());

    RDMA_ASSERT_NO_THROW(connections.sender.Close());
    RDMA_ASSERT_NO_THROW(connections.receiver.Close());
}


}; //EasyRDMA