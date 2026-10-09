# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.transform)

set(_expected_error_regex
    [[fnfelt: IO transform error: TransformIO{\(SourceIO\(\)\) => int\}: rejected by custom ]]
    [[transformer traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  transform_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
