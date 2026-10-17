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
    [[fnfelt: IO sequence error: IO\{.*\}: element of the tuple is not an IO: ]])

fnfelt_test_compile_fail(
  ${_prefix}.element_not_io
  SOURCE
  sequence_element_not_io.cpp
  ERROR_REGEX
  ${_expected_error_regex})

# The offending element is an IO reference, whose spelling is compiler-formatted, so pin through the
# reference reason and leave the element list and type free.
set(_expected_error_regex
    [[fnfelt: IO sequence error: .*: element of the tuple must not be a reference type: ]])

fnfelt_test_compile_fail(
  ${_prefix}.element_reference
  SOURCE
  sequence_element_reference.cpp
  ERROR_REGEX
  ${_expected_error_regex})
