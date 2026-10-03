# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.IO)

fnfelt_test_compile_fail(
  ${_prefix}.not_callable
  SOURCE
  not_callable.cpp
  ERROR_REGEX
  "fnfelt: IO action is invalid: is not callable with no arguments")

fnfelt_test_compile_fail(
  ${_prefix}.returns_void
  SOURCE
  returns_void.cpp
  ERROR_REGEX
  "fnfelt: IO action is invalid: does not return a value")

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "fnfelt: IO action is invalid: rejected by custom traits")

fnfelt_test_compile_fail(
  ${_prefix}.custom_name
  SOURCE
  custom_name.cpp
  ERROR_REGEX
  "fnfelt: FileIO action is invalid: is not callable with no arguments")
