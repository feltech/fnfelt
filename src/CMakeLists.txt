# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

####################################################################################################
# Library target

include(GNUInstallDirs)

add_library(${PROJECT_NAME}.lib INTERFACE)
set(_lib_target ${PROJECT_NAME}.lib)

target_sources(
  ${_lib_target}
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${PROJECT_SOURCE_DIR}/include
         FILES
         ${PROJECT_SOURCE_DIR}/include/fnfelt/invoke.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/functors.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/type_traits.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/static_string.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/detail/errors.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/macros_push.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/macros_pop.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/fwd.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/traits.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/IO.hpp)

install(TARGETS ${_lib_target} FILE_SET HEADERS)

target_compile_features(${_lib_target} INTERFACE cxx_std_26)
set_target_properties(${_lib_target} PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(${_lib_target} INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(_compiler_warnings
      -Wall
      -Wextra
      -Werror
      # Warns on C++23 var[a, b], which is used by libfork.
      -Wno-comma-subscript)
else()
  # TODO(DF): MSVC
  set(_compiler_warnings)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(_compiler_features -freflection)
else()
  # TODO(DF): MSVC
  set(_compiler_features)
endif()

target_compile_options(${_lib_target} INTERFACE ${_compiler_features} ${_compiler_warnings})

####################################################################################################
# Doctest test suite.

if(${PROJECT_NAME}_ENABLE_DEV)
  add_executable(
    ${PROJECT_NAME}.main
    main.cpp
    functors.cpp
    invoke.cpp
    type_traits.cpp
    monad/io/detail.cpp
    monad/io/traits.cpp
    monad/io/IO.cpp)
  set(_main_target ${PROJECT_NAME}.main)
  target_link_libraries(${_main_target} PRIVATE doctest::doctest ${PROJECT_NAME}.lib)
  fnfelt_target_linters(${_main_target})
  add_test(NAME ${_main_target} COMMAND ${_main_target})
endif()
