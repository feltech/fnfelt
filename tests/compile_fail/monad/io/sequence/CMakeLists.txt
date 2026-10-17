# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.sequence)

# The offending type is the first element of the tuple, whose spelling is compiler-formatted, so pin
# through the custom reason and leave the element list and type free.
set(_expected_error_regex
    [[fnfelt: IO sequence error: SequenceIO\{.*\}: rejected by custom sequence traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  sequence_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})

set(_expected_error_regex
    [[fnfelt: IO sequence error: IO\{\[IO\]\}: rejected by custom array sequence traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.array_custom_traits
  SOURCE
  sequence_array_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
