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
         ${PROJECT_SOURCE_DIR}/include/fnfelt/detail/errors.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/macros_push.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/macros_pop.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/fwd.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/detail.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/traits.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/transform.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/and_then.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/ap.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/create.hpp
         ${PROJECT_SOURCE_DIR}/include/fnfelt/monad/io/IO.hpp)

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

target_link_libraries(${_lib_target} INTERFACE)

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
    monad/io/detail.cpp
    monad/io/traits.cpp
    monad/io/and_then.cpp
    monad/io/transform.cpp
    monad/io/create.cpp
    monad/io/ap.cpp
    monad/io/IO.cpp
    monad/io.cpp)
  set(_main_target ${PROJECT_NAME}.main)
  target_link_libraries(${_main_target} PRIVATE doctest::doctest ${PROJECT_NAME}.lib)
  fnfelt_target_linters(${_main_target})
  add_test(NAME ${_main_target} COMMAND ${_main_target})
endif()
