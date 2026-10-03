# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.ap)

set(_expected_error_regex
    [[fnfelt: IO ap error: ApIO{\(FnIO\(ValueIO\(\)\) => int\)}: rejected by custom ap traits: ]]
    [[fn_io_type]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  ap_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO ap error: IO{\(IO\(IO\(\)\) => <unknown>\)}: function IO's value is not callable]]
    [[ with the value IO's value: fn_io_type]])

fnfelt_test_compile_fail(
  ${_prefix}.incompatible
  SOURCE
  ap_incompatible.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO ap error: IO{\(IO\(IO\(\)\) => void\)}: does not return a value: fn_io_type]])

fnfelt_test_compile_fail(
  ${_prefix}.returns_void
  SOURCE
  ap_returns_void.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO ap error: IO{\(<unknown>\(IO\(\)\) => <unknown>\)}: is not an IO: int]])

fnfelt_test_compile_fail(
  ${_prefix}.fn_not_io
  SOURCE
  ap_fn_not_io.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO ap error: IO{\(IO\(<unknown>\(\)\) => <unknown>\)}: is not an IO: int]])

fnfelt_test_compile_fail(
  ${_prefix}.value_not_io
  SOURCE
  ap_value_not_io.cpp
  ERROR_REGEX
  ${_expected_error_regex})
