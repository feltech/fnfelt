# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix invoke)

fnfelt_test_compile_fail(
  ${_prefix}.invoke_or_apply_bad
  SOURCE
  invoke_or_apply_bad.cpp
  ERROR_REGEX
  "fnfelt: invoke_or_apply_result_t requires a value that is directly callable")
