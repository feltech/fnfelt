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
  ${_prefix}.sync_wait_custom_traits
  SOURCE
  sync_wait_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: CustomRunIO sync_wait is invalid: rejected by custom sync_wait traits]])

fnfelt_test_compile_fail(
  ${_prefix}.build_custom_traits
  SOURCE
  build_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: RejectBuild build is invalid: rejected by custom build traits")
