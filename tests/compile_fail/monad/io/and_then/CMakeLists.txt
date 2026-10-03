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

# The offending type is the merged workhorse's source alias (`source_io_type {aka int}`), so pin
# only through the reason and leave the type's spelling free.
set(_expected_error_regex
    [[fnfelt: IO and_then error: Named and_then{\(<unknown>\(\)\) => <unknown>\}: rejected by ]]
    [[custom and_then source traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.not_io_custom_traits
  SOURCE
  and_then_not_io_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
