# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.io.or_else.custom_traits
  SOURCE
  or_else_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: IO or_else error: RecoveryIO: rejected by custom recovery traits]])
