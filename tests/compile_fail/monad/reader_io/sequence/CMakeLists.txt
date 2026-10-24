# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.reader_io.sequence.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "fnfelt: ReaderIO sequence error: rejected by custom sequence traits")
