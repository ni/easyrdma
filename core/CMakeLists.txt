# Copyright (c) 2022 National Instruments
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.6)
project(easyrdma)

set(CMAKE_CXX_STANDARD 14)

file(GLOB_RECURSE sources api/*.cpp common/*.cpp)

add_definitions(-D_BUILDING_EASYRDMA)

if(WIN32)
    add_definitions(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
    file(GLOB_RECURSE os_sources windows/*.cpp)
    set(OS_HEADERS windows)
    set(RDMA_HEADER_DIR ${NetDirect_HEADER_DIR})
elseif(UNIX)
    file(GLOB_RECURSE os_sources linux/*.cpp)
    set(OS_HEADERS linux)
    set(RDMA_HEADER_DIR ${VERBS_HEADER_DIR})
endif()

set(SOURCE_FILES ${sources} ${os_sources})
include_directories(. common ${OS_HEADERS} ${RDMA_HEADER_DIR})

# This defines the variables Boost_LIBRARIES that containts all library names
# that we need to link into the program.
find_package(Boost COMPONENTS system thread filesystem REQUIRED )
include_directories(${Boost_INCLUDE_DIRS})

add_library(easyrdma SHARED ${SOURCE_FILES})

set_target_properties(easyrdma PROPERTIES SOVERSION 1)
set(CMAKE_SKIP_RPATH TRUE)

target_link_libraries(easyrdma PRIVATE ${Boost_LIBRARIES})

if(WIN32)
    target_link_libraries(easyrdma PRIVATE ws2_32 ${NetDirect_LIBRARYDIR}/ndutil.lib)
elseif(UNIX)
    target_link_directories(easyrdma PRIVATE ${VERBS_LIB_DIR})
    target_link_libraries(easyrdma PRIVATE libibverbs.so librdmacm.so)
endif()
