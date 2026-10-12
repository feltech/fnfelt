# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.async)

# Pin the custom traits name and reason; the async function's display string is left free.
set(_expected_error_regex
    [[fnfelt: CustomAsyncIO async action is invalid: rejected by custom async traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.create_async_custom_traits
  SOURCE
  create_async_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
