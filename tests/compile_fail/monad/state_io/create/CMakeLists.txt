# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.state_io.create)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  create_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: CreateIO action is invalid: rejected by custom action traits: ")
