# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  interop.stdexec.invalid_direct_value
  SOURCE
  invalid_direct_value.cpp
  ERROR_REGEX
  "fnfelt: a direct sender completion cannot be")
