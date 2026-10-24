# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.reader_io.traverse.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "fnfelt: ReaderIO traverse error: rejected by custom traverse traits")
