# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.reader_io.ap)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  ap_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: RejectReader ap error: RejectReader\\{.*\\}: rejected by custom ap traits: ")
