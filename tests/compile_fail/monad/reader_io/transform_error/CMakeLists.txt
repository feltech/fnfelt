# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.reader_io.transform_error.custom_traits
  SOURCE
  transform_error_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: RejectReader transform_error error:.*rejected by custom error mapping traits]])
