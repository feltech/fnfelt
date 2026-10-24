# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.reader_io.transform)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  transform_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: RejectReader transform error: RejectReader{.*}: rejected by custom transform traits: ")
