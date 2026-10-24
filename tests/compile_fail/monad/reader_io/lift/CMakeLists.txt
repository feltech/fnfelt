# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.reader_io.lift)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  lift_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: RejectReader lift error: RejectReader{.*}: rejected by custom lift traits: ")
