# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.async)

# Pin the readable statelessness reason; the closure's display string is left free.
set(_expected_error_regex
    [[fnfelt: IO async action is invalid: provided async function must be stateless: ]])

fnfelt_test_compile_fail(
  ${_prefix}.create_async_stateful_fn
  SOURCE
  create_async_stateful_fn.cpp
  ERROR_REGEX
  ${_expected_error_regex})

# Pin the missing-value_type reason; the action's display string is left free.
set(_expected_error_regex
    [[fnfelt: IO action is invalid: provided action's async proxy does not expose a value_type ]]
    [[alias: ]])

fnfelt_test_compile_fail(
  ${_prefix}.missing_value_type_alias
  SOURCE
  missing_value_type_alias.cpp
  ERROR_REGEX
  ${_expected_error_regex})
