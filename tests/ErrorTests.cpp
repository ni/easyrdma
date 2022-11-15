// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include "core/common/RdmaAddress.h"
#include "core/common/RdmaConnectionData.h"
#include "utility/RdmaTestBase.h"
#include "tests/session/Session.h"
#include "tests/utility/Utility.h"
#include <gtest/gtest.h>
#include <regex>

class RdmaTestErrors : public RdmaTestBase<::testing::Test>  {
};

TEST_F(RdmaTestErrors, GetLastErrorString_Empty) {
    // First do a call that succeeds to ensure any previous error is cleared
    RDMA_ASSERT_NO_THROW(Enumeration::EnumerateInterfaces());
    EXPECT_EQ(GetLastErrorString(), "");
}

TEST_F(RdmaTestErrors, GetLastErrorString_GoodError) {
    RDMA_ASSERT_THROW_WITHCODE(Session::CreateConnector("", 1000), easyrdma_Error_InvalidAddress);
    std::string lastErrorStr = GetLastErrorString();
    std::string expectedMatch = "^Invalid address.";
    expectedMatch += "\n";
    expectedMatch += "Location: (.*):(\\d+)\n";
    EXPECT_TRUE(std::regex_search(lastErrorStr, std::regex(expectedMatch))) << "Error string: " << lastErrorStr << " doesn't match regex: " << expectedMatch;
}

TEST_F(RdmaTestErrors, GetLastErrorString_UserBufferTooSmall) {
    RDMA_ASSERT_THROW_WITHCODE(Session::CreateConnector("", 1000), easyrdma_Error_InvalidAddress);
    char smallBuffer[3] = { 0 };
    EXPECT_EQ(easyrdma_GetLastErrorString(smallBuffer, sizeof(smallBuffer)), easyrdma_Error_InvalidSize);
}

TEST_F(RdmaTestErrors, GetLastErrorString_OsErrorElaboration) {
    // OS Errors are treated special and include special elaboration of the original error.
    // Since we do not have a good way to generate these unexpected errors, we have a test entrypoint
    // for explicitly generating them.
    #ifdef _WIN32
        int osError = -2147024891; // E_ACCESSDENIED (Windows)
    #else
        int osError = 8; // ENOEXEC (Linux)
    #endif
    RDMA_ASSERT_NO_THROW(easyrdma_testsetLastOsError(osError)); //

    std::string lastErrorStr = GetLastErrorString();

    std::stringstream expectedMatch;
    expectedMatch << "^Operating system error.";
    expectedMatch << "\n";
    expectedMatch << "Subcode: " << std::to_string(osError);
    expectedMatch << "\n";
    expectedMatch << "Location: (.*):(\\d+)\n";

    EXPECT_TRUE(std::regex_search(lastErrorStr, std::regex(expectedMatch.str()))) << "Error string: " << lastErrorStr << " doesn't match regex: " << expectedMatch.str();
}