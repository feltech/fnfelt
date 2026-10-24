# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

####################################################################################################
# Library target

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(${PROJECT_NAME}.lib INTERFACE)
set(_lib_target ${PROJECT_NAME}.lib)

# Export the target as fnfelt::fnfelt, hiding the internal .lib suffix from consumers.
set_target_properties(${_lib_target} PROPERTIES EXPORT_NAME ${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${_lib_target})

target_sources(
  ${_lib_target}
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${PROJECT_SOURCE_DIR}/include
         FILES
         ${PROJECT_SOURCE_DIR}/include/fnfelt/static_string.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/macros_push.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/macros_pop.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/detail/callable_shape.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/detail/invocability.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/detail/type_names.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/fwd.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/and_then.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/ap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/async.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/create.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/fwd.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/just.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/repeat_until.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/sequence.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/transform.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/select.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/traverse.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/just.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/split.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/split.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/fanout.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/fanout.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/tap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail/tap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/repeat_until.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/transform.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/select.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/and_then.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/ap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/async.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/create.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/sequence.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/traverse.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/IO.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/ReaderIO.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/fwd.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/create.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/bind.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/create.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/and_then.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/and_then.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/transform.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/select.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/transform.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/select.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/ap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/ap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/async.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/async.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/just.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/just.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/detail/lift.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/reader_io/lift.hpp)

set(_package_config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(
  TARGETS ${_lib_target}
  EXPORT ${PROJECT_NAME}-targets
  FILE_SET HEADERS)

install(
  EXPORT ${PROJECT_NAME}-targets
  FILE ${PROJECT_NAME}-targets.cmake
  DESTINATION ${_package_config_dir}
  NAMESPACE ${PROJECT_NAME}::)

configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
  INSTALL_DESTINATION ${_package_config_dir})

write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
                                 COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        DESTINATION ${_package_config_dir})

target_compile_features(${_lib_target} INTERFACE cxx_std_26)
set_target_properties(${_lib_target} PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(${_lib_target} INTERFACE immer::immer libfork::libfork)

# Only the reflection flag is exported: warning flags are a project-internal policy applied by
# fnfelt_target_linters, so consumers keep ownership of their own warning settings.
# The generator expression defers the compiler check to the consumer's configuration.
# TODO(DF): MSVC has no reflection support yet.
target_compile_options(${_lib_target} INTERFACE "$<$<CXX_COMPILER_ID:GNU,Clang>:-freflection>")

####################################################################################################
# Doctest test suite.

if(${PROJECT_NAME}_ENABLE_DEV)
  add_executable(
    ${PROJECT_NAME}.main
    main.cpp
    detail/callable_shape.cpp
    detail/invocability.cpp
    detail/type_names.cpp
    monad/io/detail.cpp
    monad/io/traits.cpp
    monad/io/just.cpp
    monad/io/split.cpp
    monad/io/fanout.cpp
    monad/io/tap.cpp
    monad/io/and_then.cpp
    monad/io/repeat_until.cpp
    monad/io/transform.cpp
    monad/io/select.cpp
    monad/io/create.cpp
    monad/io/ap.cpp
    monad/io/sequence.cpp
    monad/io/traverse.cpp
    monad/io/async.cpp
    monad/io/IO.cpp
    monad/io.cpp
    monad/reader_io/ReaderIO.cpp
    monad/reader_io/detail.cpp
    monad/reader_io/detail/bind.cpp
    monad/reader_io/traits.cpp
    monad/reader_io/create.cpp
    monad/reader_io/and_then.cpp
    monad/reader_io/transform.cpp
    monad/reader_io/select.cpp
    monad/reader_io/ap.cpp
    monad/reader_io/async.cpp
    monad/reader_io/lift.cpp
    monad/reader_io/just.cpp)
  set(_main_target ${PROJECT_NAME}.main)
  target_link_libraries(${_main_target} PRIVATE doctest::doctest ${PROJECT_NAME}.lib)
  fnfelt_target_linters(${_main_target})
  add_test(NAME ${_main_target} COMMAND ${_main_target})
endif()
