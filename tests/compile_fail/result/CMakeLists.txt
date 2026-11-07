# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  result.custom_traits
  SOURCE
  result_custom_traits.cpp
  ERROR_REGEX
  [[rejected by custom result traits]])

fnfelt_test_compile_fail(
  result.error_custom_traits
  SOURCE
  error_custom_traits.cpp
  ERROR_REGEX
  [[rejected by custom error traits]])

fnfelt_test_compile_fail(
  result.factory_custom_traits
  SOURCE
  factory_custom_traits.cpp
  ERROR_REGEX
  [[rejected by custom Result factory traits]])

fnfelt_test_compile_fail(
  result.access_custom_traits
  SOURCE
  access_custom_traits.cpp
  ERROR_REGEX
  [[rejected by custom Result access traits]])

fnfelt_test_compile_fail(
  result.maybe_access_custom_traits
  SOURCE
  maybe_access_custom_traits.cpp
  ERROR_REGEX
  [[rejected by custom Result optional access traits]])
