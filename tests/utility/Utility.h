// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include "api/easyrdma.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include "common/RdmaError.h"
#include <boost/filesystem.hpp>

#if _WIN32
    #include <windows.h>
#endif

inline std::string GetLastErrorString() {
    char translatedString[512] = { 0 };
    int err = easyrdma_GetLastErrorString(translatedString, sizeof(translatedString));
    return err == easyrdma_Error_Success ? std::string(translatedString) : std::string("GetLastError returned error ") + std::to_string(err);
}

class RdmaTestException : public std::runtime_error {
    public:
    RdmaTestException(int32_t _errorCode, const std::string& message) : std::runtime_error(message), errorCode(_errorCode) {
    }
    int32_t errorCode;
};


#define RDMA_THROW_TEST_ERROR(code)                         \
{                                                           \
    assert(code != 0);                                      \
    throw RdmaTestException(code, "Test-specific error");   \
}

#define RDMA_THROW_IF_FATAL(code)                    \
    {                                                \
        auto capturedCode = (code);                  \
        if(capturedCode != 0) {                      \
            easyrdma_ErrorInfo status;               \
            easyrdma_GetLastError(&status);          \
            if(status.errorCode != 0) {              \
                throw RdmaTestException(status.errorCode, GetLastErrorString()); \
        }                                            \
        else {                                       \
            assert(0);                               \
        }                                            \
        } \
    } \


#ifdef __linux__
    #include <boost/process.hpp>

    static inline void TestAndCheck(const char* option, int expectedValue) {
        namespace bp = boost::process;
        bp::ipstream processOutput;
        std::string output;
        std::string command = std::string("sysctl ") + option + " -b";
        bp::child processChild(command.c_str(), bp::std_out > processOutput);
        std::getline(processOutput, output);
        if(output == std::to_string(expectedValue)) {
            return;
        }
        else {
            std::cout << "Resetting " << option << " to " << std::to_string(expectedValue) << std::endl;
            std::string command = std::string("sudo sysctl -w ") + option + "=" + std::to_string(expectedValue);
            if(system(command.c_str()) != 0) {
                std::cerr << "Cannot write sysctl " << option << " as non-root" << std::endl;
                RDMA_THROW_TEST_ERROR(-1);
            }
        }
    }

    static inline void TestAndFixIpv4Loopback() {
        TestAndCheck("net.ipv4.conf.all.arp_ignore", 2);
        TestAndCheck("net.ipv4.conf.all.accept_local", 1);
    }
#endif

#define SKIP_IF(predicate, message)                                             \
    if (predicate) {                                                            \
        std::cout << "Skipping test because: " << message << std::endl;         \
        GTEST_SKIP();                                                           \
    }


#define RDMA_CAT_HELPER(x,y) x ## y
#define RDMA_CAT_HELPER2(a,b) RDMA_CAT_HELPER(a,b)    // Concatenate 2 tokens macro


namespace {
   template <typename T>
   class RdmaErrInternalGtestAutoPtrDoNotUse
   {
   public:
      RdmaErrInternalGtestAutoPtrDoNotUse(T* ptr) : ptr_(ptr) {}
      ~RdmaErrInternalGtestAutoPtrDoNotUse() { delete ptr_; }
      operator bool() const { return ptr_!= NULL; }
      T* operator->() const { return ptr_; }
      T& operator*() const { return *ptr_; }
   private:
      T* ptr_;
   };
}

#define RDMA_ERROR_TEST_THROW_WITH_CODE_INTERNAL_(statement, statement_str, expected_code, fail) \
   if (RdmaErrInternalGtestAutoPtrDoNotUse<std::stringstream> fail_msg = new std::stringstream) { \
      *fail_msg << "Expected: " statement_str " throws an exception derived from RdmaTestException or RdmaException"; \
      *fail_msg << " with the status code " << expected_code << "\n" \
                << "  Actual: "; \
      try { \
         statement; \
         *fail_msg << "it didn't throw anything."; \
         goto RDMA_CAT_HELPER2(rdma_label_output_failure_message_, __LINE__); \
      } \
      catch (const RdmaTestException& e) { \
         const bool matchesStatus = e.errorCode == expected_code; \
         if(!matchesStatus) { \
            *fail_msg << "it threw an exception derived from RdmaTestException that" \
                      << " doesn't have the expected status code: " \
                      << "Description: " << e.what(); \
            goto RDMA_CAT_HELPER2(rdma_label_output_failure_message_, __LINE__); \
         } \
      } \
      catch (const RdmaException& e) { \
         const bool matchesStatus = e.rdmaError.errorCode == expected_code; \
         if(!matchesStatus) { \
            *fail_msg << "it threw an exception derived from RdmaException that" \
                      << " doesn't have the expected status code: " \
                      << "Description: " << e.GetExtendedErrorInfo(); \
            goto RDMA_CAT_HELPER2(rdma_label_output_failure_message_, __LINE__); \
         } \
      } \
      catch (const std::exception& e) { \
         *fail_msg << "it threw an exception derived from std::exception with description: " << e.what(); \
         goto RDMA_CAT_HELPER2(rdma_label_output_failure_message_, __LINE__); \
      } \
      catch (...) { \
         *fail_msg << "it threw an unknown exception."; \
         goto RDMA_CAT_HELPER2(rdma_label_output_failure_message_, __LINE__); \
      } \
   } else \
      RDMA_CAT_HELPER2(rdma_label_output_failure_message_, __LINE__): \
         fail() << fail_msg->str() << "\n"

/*! gtest expect that a statement throws a RdmaTestException with a specific error code
 * \param[in] statement
 * \param[in] expected_code
 */
#define RDMA_EXPECT_THROW_WITHCODE(statement, expected_code) \
   RDMA_ERROR_TEST_THROW_WITH_CODE_INTERNAL_(statement, #statement, expected_code, ADD_FAILURE)

/*! gtest assert that a statement throws a RdmaTestException with a specific error code
 * \param[in] statement
 * \param[in] expected_code
 */
#define RDMA_ASSERT_THROW_WITHCODE(statement, expected_code) \
   RDMA_ERROR_TEST_THROW_WITH_CODE_INTERNAL_(statement, #statement, expected_code, GTEST_FAIL)

/*! Implementation of gtest assert/expect that a statement does not throw an
 *  exception.  This is heavily based on GTEST_TEST_NO_THROW_, but with changes
 *  to add additional information and without using googletest internals.
 */
#define RDMA_TEST_NO_THROW_INTERNAL_(statement, statement_str, fail) \
   if (RdmaErrInternalGtestAutoPtrDoNotUse<std::stringstream> RDMA_CAT_HELPER2(rdma_exception_message_stream_,__LINE__) = new std::stringstream) { \
      try { \
         statement; \
      } \
      catch (const RdmaTestException& e) { \
         *(RDMA_CAT_HELPER2(rdma_exception_message_stream_,__LINE__)) \
            << statement_str " threw an exception derived from RdmaTestException: " << e.what() ; \
         goto RDMA_CAT_HELPER2(rdma_label_testnothrow_, __LINE__); \
      } \
      catch (const RdmaException& e) { \
         *(RDMA_CAT_HELPER2(rdma_exception_message_stream_,__LINE__)) \
            << statement_str " threw an exception derived from RdmaException: " << e.GetExtendedErrorInfo() ; \
         goto RDMA_CAT_HELPER2(rdma_label_testnothrow_, __LINE__); \
      } \
      catch (const std::exception& e) { \
         *(RDMA_CAT_HELPER2(rdma_exception_message_stream_,__LINE__)) \
            << statement_str " threw an exception derived from std::exception with description: " << e.what(); \
         goto RDMA_CAT_HELPER2(rdma_label_testnothrow_, __LINE__); \
      } \
      catch (...) { \
         *(RDMA_CAT_HELPER2(rdma_exception_message_stream_,__LINE__)) \
            << statement_str " threw an unknown exception."; \
         goto RDMA_CAT_HELPER2(rdma_label_testnothrow_, __LINE__); \
      } \
   } else \
      RDMA_CAT_HELPER2(rdma_label_testnothrow_, __LINE__): \
         fail() << "Expected: " statement_str " doesn't throw an exception.\n" \
                   "  Actual: " << RDMA_CAT_HELPER2(rdma_exception_message_stream_,__LINE__)->str() << "\n"

/*! gtest assert/expect that a statement does not throws an exception, but if
 *  it does throw one then include information in the failure message about
 *  what exception was thrown.
 *  \param[in] statement
 */
#define RDMA_ASSERT_NO_THROW(statement) \
        RDMA_TEST_NO_THROW_INTERNAL_(statement, #statement, GTEST_FAIL)
#define RDMA_EXPECT_NO_THROW(statement) \
        RDMA_TEST_NO_THROW_INTERNAL_(statement, #statement, ADD_FAILURE)

namespace std {
    namespace chrono {
        template <typename Rep, typename Ratio>
        inline void PrintTo(const duration<Rep, Ratio>& _duration, ::std::ostream *os) {
            auto durationMs = duration_cast<duration<double, std::milli>>(_duration);
            *os << durationMs.count() << " ms";
        }
    };
};


//////////////////////////////////////////////////////////////////////////////
//
//  GetTemporaryFilename
//
//  Description:
//      Returns a unique temporary file name.
//
//////////////////////////////////////////////////////////////////////////////
inline std::string GetTemporaryFilename() {
    std::string filename = boost::filesystem::temp_directory_path().string() + "/" + boost::filesystem::unique_path().string();
    return filename;
}