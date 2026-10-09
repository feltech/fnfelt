# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.IO)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "fnfelt: IO action is invalid: rejected by custom action traits")

fnfelt_test_compile_fail(
  ${_prefix}.sync_wait_lvalue_proxy
  SOURCE
  sync_wait_lvalue_proxy.cpp
  ERROR_REGEX
  [[sync_wait must be called on an rvalue run proxy]])

# Pin the executable-proxy reason; the action's display string is left free.
fnfelt_test_compile_fail(
  ${_prefix}.async_proxy_not_executable
  SOURCE
  async_proxy_not_executable.cpp
  ERROR_REGEX
  [[fnfelt: IO action is invalid: provided action's async proxy must expose an arg member]])
