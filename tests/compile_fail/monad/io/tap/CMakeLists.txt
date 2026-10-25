# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.io.tap.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "CustomIO.*rejected by custom traits")
