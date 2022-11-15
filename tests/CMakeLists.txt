# Copyright (c) 2022 National Instruments
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 14)

file(GLOB_RECURSE TEST_SOURCES *.cpp utility/*.cpp ../core/common/ThreadUtility.cpp)

if(WIN32)
    add_definitions(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
endif()

set(SOURCE_FILES ${TEST_SOURCES})
include_directories(. .. ../core ../core/api)

# This defines the variables Boost_LIBRARIES that containts all library names
# that we need to link into the program.
find_package(Boost COMPONENTS system thread filesystem program_options serialization chrono REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# googletest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include_directories(${gtest_SOURCE_DIR}/include)

add_executable(easyrdma_tests ${SOURCE_FILES})
target_link_libraries(easyrdma_tests ${Boost_LIBRARIES} easyrdma gtest)

if(WIN32)
  target_link_libraries(easyrdma_tests ws2_32)
elseif(UNIX)
  # We support some combinations of compilers and boost that need this warning suppressed
  add_definitions(-DBOOST_NO_CXX98_BINDERS)
  add_compile_options(-Wno-deprecated-declarations)

  # Because we are linking to easyrdma, which also links to a chain of distro-provided libraries, we
  # don't want ld to have to validate the entire chain since not all those libraries might be available
  # on the build machine
  target_link_libraries(easyrdma_tests -Wl,--allow-shlib-undefined -lrt -lpthread)
endif()