// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "TestLogger.h"
#include <future>
#include <regex>
#include "core/common/RdmaAddress.h"
#include "tests/utility/RdmaTestBase.h"
#include "tests/session/Session.h"
#include "tests/utility/Enumeration.h"
#include "tests/utility/TestEndpoints.h"
#include "tests/utility/Utility.h"

template <typename GtestType>
class RdmaTestBase : public GtestType, public TestLogger
{
public:
    RdmaTestBase()
    {
        beginTest();
    }
    virtual ~RdmaTestBase()
    {
        auto test_log_data = endTest();
        ::testing::Test::RecordProperty("TestOutput", test_log_data.logs);
    }
    std::pair<RdmaAddress, RdmaAddress> GetEndpointAddresses()
    {
        TestEndpoints endpoints = GtestType::GetParam();
        return std::make_pair(RdmaAddress(endpoints.endpointA, 50001), RdmaAddress(endpoints.endpointB, 50002));
    }

    struct ConnectionPair
    {
        Session sender;
        Session receiver;
        void Close()
        {
            sender.Close();
            receiver.Close();
        }
    };

    ConnectionPair GetLoopbackConnection()
    {
        auto endpoints = GetEndpointAddresses();
        RdmaAddress localAddressListener = endpoints.first;
        RdmaAddress localAddressConnector = endpoints.second;

        std::future<Session> accept;
        Session sessionConnectorSender = Session::CreateConnector(localAddressConnector.GetAddrString(), 0);
        Session sessionListener = Session::CreateListener(localAddressListener.GetAddrString(), 0);

        accept = std::async(std::launch::async, [&]() {
            return sessionListener.Accept(easyrdma_Direction_Receive);
        });
        sessionConnectorSender.Connect(easyrdma_Direction_Send, localAddressListener.GetAddrString(), sessionListener.GetLocalPort());
        Session sessionReceiver = std::move(accept.get());

        return {std::move(sessionConnectorSender), std::move(sessionReceiver)};
    }
};