# Copyright (c) 2022 National Instruments
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 14)

set(TEST_SOURCES AccessMgrTests.cpp LastErrorTests.cpp)
set(CORE_SOURCES ../core/api/errorhandling.cpp)

if(UNIX)
    set(OS_SPECIFIC_TESTS LinuxPollTests.cpp)
endif()

if(WIN32)
    add_definitions(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
endif()

set(SOURCE_FILES ${TEST_SOURCES} ${CORE_SOURCES} ${OS_SPECIFIC_TESTS})
include_directories(. ../ ../core)

# This defines the variables Boost_LIBRARIES that containts all library names
# that we need to link into the program.
find_package(Boost COMPONENTS system thread filesystem  REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# googletest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include_directories(${gtest_SOURCE_DIR}/include)

add_executable(easyrdma_unit_tests ${SOURCE_FILES})
target_link_libraries(easyrdma_unit_tests ${Boost_LIBRARIES} gtest_main)

if(UNIX)
  target_link_libraries(easyrdma_unit_tests -lpthread)
endif()