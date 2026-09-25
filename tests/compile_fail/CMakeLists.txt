# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

# fnfelt_compile_fail(<name> SOURCE <file.cpp> ERROR_REGEX <regex>)
#
# Register a CTest test that passes iff <file.cpp> FAILS to compile against fnfelt.lib and the
# compiler output matches ERROR_REGEX.
function(fnfelt_test_compile_fail name)
  cmake_parse_arguments(
    PARSE_ARGV
    1
    _arg
    ""
    "SOURCE;ERROR_REGEX"
    "")
  if(NOT _arg_SOURCE)
    message(FATAL_ERROR "fnfelt_compile_fail: SOURCE required")
  endif()
  if(NOT _arg_ERROR_REGEX)
    message(FATAL_ERROR "fnfelt_compile_fail: ERROR_REGEX required")
  endif()

  set(target ${PROJECT_NAME}.test.compile_fail.${name})

  # Executable that fails to compile - doctest main with test source.
  add_executable(${target} ${PROJECT_SOURCE_DIR}/tests/main.cpp ${_arg_SOURCE})
  # Link to main project.
  target_link_libraries(${target} PRIVATE doctest::doctest ${PROJECT_NAME}.lib)
  # Do not build as part of a whole-project build (since it'll fail by design).
  set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  # Building the target _is_ the test.
  add_test(NAME ${target} COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target ${target}
                                  --config $<CONFIG>)
  set_tests_properties(
    ${target}
    PROPERTIES
      # Regex allowing the failing build to pass the test (i.e. expected error message).
      PASS_REGULAR_EXPRESSION
      "${_arg_ERROR_REGEX}"
      # Serialize the compile-fail tests: each one builds a target, so running them concurrently
      # could race on the build directory.
      RESOURCE_LOCK
      fnfelt.test.compile_fail)
endfunction()

####################################################################################################
# Compiler error test packages

add_subdirectory(type_traits)
add_subdirectory(invoke)
