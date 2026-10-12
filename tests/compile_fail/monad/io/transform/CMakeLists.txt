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

# The offending type is the merged workhorse's source alias, so pin only through the reason
# and leave the type's spelling free.
set(_expected_error_regex
    [[fnfelt: IO transform error: Named transform{\(<unknown>\(\)\) => <unknown>\}: rejected ]]
    [[by custom transform source traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.not_io_custom_traits
  SOURCE
  transform_not_io_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})

# The source is an async leaf, so the diagnostic must splice the proxy's unwrapped value type
# without blowing up. Pin through the reason and leave the transformer's type spelling free.
set(_expected_error_regex
    [[fnfelt: IO transform error: IO\{\(AsyncSource\(\)\) => ResultIO\}: transformer function ]]
    [[should not return an IO: ]])

fnfelt_test_compile_fail(
  ${_prefix}.transformer_not_io_async
  SOURCE
  transformer_not_io_async.cpp
  ERROR_REGEX
  ${_expected_error_regex})

# The source value is std::optional<int>: all-type template arguments, but not tuple-like, so it is
# no longer spread. Pin through the friendly reason and leave the type's spelling free.
set(_expected_error_regex
    [[fnfelt: IO transform error: IO\{\(IO\(\)\) => <unknown>\}: transformer ]]
    [[function does not accept the source IO's value: ]])

fnfelt_test_compile_fail(
  ${_prefix}.optional_value_not_spread
  SOURCE
  transform_optional_value_not_spread.cpp
  ERROR_REGEX
  ${_expected_error_regex})
