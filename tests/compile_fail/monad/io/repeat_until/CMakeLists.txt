# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.repeat_until)

set(_custom_traits_error_regex
    [[fnfelt: IO repeat_until error: RepeatUntilIO{SourceIO, lambda\(int, int\) ]]
    [[=> int\}: rejected by custom repeat traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  repeat_until_custom_traits.cpp
  ERROR_REGEX
  ${_custom_traits_error_regex})
