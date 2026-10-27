# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.io.select.custom_traits
  SOURCE
  select_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: IO select error: SelectIO.*rejected by custom selective traits]])
