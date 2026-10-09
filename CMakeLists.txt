# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

cmake_minimum_required(VERSION 3.28)
project(
  fnfelt
  VERSION 0.1.0
  LANGUAGES CXX C)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # Compiler options break clang-tidy

####################################################################################################
# Options

option(${PROJECT_NAME}_ENABLE_DEV "Enable dev mode - tests and linting" OFF)
option(${PROJECT_NAME}_ENABLE_SANITIZER_ASAN "Enable ASan and UBSan" OFF)

####################################################################################################
# Development

if(${PROJECT_NAME}_ENABLE_DEV)
  find_package(doctest REQUIRED)
  enable_testing()
  include(cmake/linters.cmake)
endif()

####################################################################################################
# Main project

find_package(libfork REQUIRED)

add_subdirectory(src)

if(${PROJECT_NAME}_ENABLE_DEV)
  ##################################################################################################
  # CTest dedicated test executables.

  add_subdirectory(tests)
endif()
