# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.just)

set(_expected_error_regex
    [[fnfelt: IO just error: JustIO\{\(\) => int\}: rejected by custom just traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  just_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
