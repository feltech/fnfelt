# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.bind)

set(_expected_error_regex
    [[fnfelt: IO bind error: BoundIO{\(SourceIO\(\)\) => ResultIO\}: rejected by custom kleisli]]
    [[ traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  bind_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO bind error: IO\{\(<unknown>\(\)\) => <unknown>\}: is not an IO: ]])

fnfelt_test_compile_fail(
  ${_prefix}.not_io
  SOURCE
  not_io.cpp
  ERROR_REGEX
  ${_expected_error_regex})
