# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.reader_io.and_then)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  and_then_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: RejectReader and_then error: RejectReader{.*}: rejected by custom and_then traits: ")
