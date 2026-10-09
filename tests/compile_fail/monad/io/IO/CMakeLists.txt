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
