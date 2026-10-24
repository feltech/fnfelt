# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.state_io.and_then.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "CustomStateIO.*rejected by custom traits")
