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

# The source is an async leaf, so the diagnostic must splice the proxy's unwrapped value type
# without blowing up. Pin through the reason and leave the continuation's type spelling free.
set(_expected_error_regex
    [[fnfelt: IO and_then error: IO\{\(AsyncSource\(\)\) => <unknown>\}: continuation function ]]
    [[must return an IO: ]])

fnfelt_test_compile_fail(
  ${_prefix}.continuation_not_io_async
  SOURCE
  and_then_continuation_not_io_async.cpp
  ERROR_REGEX
  ${_expected_error_regex})

# The source value is std::optional<int>: all-type template arguments, but not tuple-like, so it is
# no longer spread. Pin through the friendly reason and leave the type's spelling free.
set(_expected_error_regex
    [[fnfelt: IO and_then error: IO\{\(IO\(\)\) => <unknown>\}: continuation ]]
    [[function does not accept the source IO's value: ]])

fnfelt_test_compile_fail(
  ${_prefix}.optional_value_not_spread
  SOURCE
  and_then_optional_value_not_spread.cpp
  ERROR_REGEX
  ${_expected_error_regex})
