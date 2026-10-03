# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.create)

fnfelt_test_compile_fail(
  ${_prefix}.create_invalid_action
  SOURCE
  create_invalid_action.cpp
  ERROR_REGEX
  "fnfelt: FileIO action is invalid: is not callable with no arguments")
