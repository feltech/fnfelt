# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.and_then)

set(_expected_error_regex
    [[fnfelt: IO and_then error: AndThenIO{\(SourceIO\(\)\) => ResultIO\}: rejected by custom ]]
    [[continuation traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  and_then_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
