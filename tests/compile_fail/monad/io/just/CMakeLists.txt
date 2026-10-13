# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.just)

# The move-only value is rejected by validate_just with the copy-constructibility reason. The value
# type's short name is pinned; the trailing display spelling is left free.
set(_expected_error_regex
    [[fnfelt: IO just error: IO\{\(\) => unique_ptr\}: provided value must be copy constructible: ]]
)

fnfelt_test_compile_fail(
  ${_prefix}.not_copy_constructible
  SOURCE
  just_not_copy_constructible.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO just error: JustIO\{\(\) => int\}: rejected by custom just traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  just_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
