# Copyright (c) 2022 National Instruments
# SPDX-License-Identifier: MIT

# EasyRDMA top-level CMakelists.txt file

cmake_minimum_required(VERSION 3.6)
project(easyrdma VERSION 1.0.0)

# Main library
add_subdirectory(core)

# Build tests directly against googletest so compiler settings match
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        58d77fa8070e8cec2dc1ed015d66b454c8d78850 # release-1.12.1
)

# Tests
add_subdirectory(unit_tests)
add_subdirectory(tests)