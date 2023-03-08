// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>
#include <exception>
#include <sstream>

class RdmaError
{
public:
    RdmaError() :
        errorCode(0), errorSubCode(0), filename(nullptr), fileLineNumber(0){};
    RdmaError(int32_t _errorCode, int32_t _errorSubCode, const char* _filename, int32_t _fileLineNumber) :
        errorCode(_errorCode), errorSubCode(_errorSubCode), filename(_filename), fileLineNumber(_fileLineNumber){};
    RdmaError(const RdmaError& other) :
        errorCode(other.errorCode), errorSubCode(other.errorSubCode), filename(other.filename), fileLineNumber(other.fileLineNumber){};
    void Clear()
    {
        errorCode = 0;
        errorSubCode = 0;
        filename = nullptr;
        fileLineNumber = 0;
    };

    int32_t GetCode() const
    {
        return errorCode;
    };

    bool IsError() const
    {
        return errorCode != 0;
    };

    bool IsSuccess() const
    {
        return errorCode == 0;
    };

    void Assign(int32_t _errorCode, int32_t _errorSubCode, const char* _filename, int32_t _fileLineNumber)
    {
        if (errorCode == 0) {
            errorCode = _errorCode;
            errorSubCode = _errorSubCode;
            filename = _filename;
            fileLineNumber = _fileLineNumber;
        }
    };

    void Assign(const RdmaError& other)
    {
        if (errorCode == 0) {
            errorCode = other.errorCode;
            errorSubCode = other.errorSubCode;
            filename = other.filename;
            fileLineNumber = other.fileLineNumber;
        }
    };

    int errorCode;
    int errorSubCode;
    const char* filename;
    int fileLineNumber;
};

#define RDMA_SET_ERROR(rdmaError, code) \
    rdmaError.Assign(code, 0, __FILE__, __LINE__)

#define RDMA_SET_ERROR_WITH_SUBCODE(rdmaError, code, subcode) \
    rdmaError.Assign(code, subcode, __FILE__, __LINE__)

class RdmaException : public std::exception
{
public:
    RdmaException(const RdmaError& _rdmaError) :
        rdmaError(_rdmaError){};
    virtual ~RdmaException() noexcept
    {
    }

    virtual const char* what() const noexcept
    {
        return "RdmaException";
    };
    std::string GetExtendedErrorInfo() const
    {
        std::stringstream ss;
        ss << "RdmaException: "
           << "ErrorCode: " << rdmaError.GetCode() << " ErrorSubCode: " << rdmaError.errorSubCode << " File: " << rdmaError.filename << ":" << rdmaError.fileLineNumber;
        return ss.str();
    };
    RdmaError rdmaError;
};

#define RDMA_THROW(code)                          \
    {                                             \
        RdmaError s(code, 0, __FILE__, __LINE__); \
        throw RdmaException(s);                   \
    }

#define RDMA_THROW_WITH_SUBCODE(code, subcode)          \
    {                                                   \
        RdmaError s(code, subcode, __FILE__, __LINE__); \
        throw RdmaException(s);                         \
    }

// No-op tracing by default - not used many places
#define TRACE(x, ...)