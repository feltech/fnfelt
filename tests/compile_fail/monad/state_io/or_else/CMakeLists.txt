# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.state_io.or_else.custom_traits
  SOURCE
  or_else_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: RejectState or_else error:.*rejected by custom recovery traits]])
