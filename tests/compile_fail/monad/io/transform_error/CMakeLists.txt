# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.io.transform_error.custom_traits
  SOURCE
  transform_error_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: IO transform_error error: RejectedErrorMap: rejected by custom error mapper traits]])
