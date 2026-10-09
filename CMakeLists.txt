# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

# The cxx_std_26 meta-feature used by src/CMakeLists.txt was first documented by CMake 3.25 but
# only fully implemented by CMake 3.30.
cmake_minimum_required(VERSION 3.30)
project(
  fnfelt
  VERSION 0.1.0
  LANGUAGES CXX C)

# fnfelt requires C++26 (P2996) reflection support. Probe for it now so that an unsupported
# compiler fails with a clear message instead of a cascade of header parse errors. The result is
# cached, so the probe compiles only once per build tree.
include(CheckCXXSourceCompiles)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_REQUIRED_FLAGS "-freflection")
endif()
check_cxx_source_compiles(
  "
#include <meta>
consteval bool probe() { std::meta::info info = ^^int; return true; }
int main() { return probe() ? 0 : 1; }
"
  FNFELT_HAS_REFLECTION)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT FNFELT_HAS_REFLECTION)
  string(CONCAT _fnfelt_reflection_error
                "fnfelt requires a compiler with C++26 reflection support (P2996). "
                "Use GCC 16+ with libstdc++; MSVC and Clang are not supported yet.")
  message(FATAL_ERROR "${_fnfelt_reflection_error}")
endif()

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
