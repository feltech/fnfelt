# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.reader_io.split.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "CustomReaderIO.*rejected by custom traits")
