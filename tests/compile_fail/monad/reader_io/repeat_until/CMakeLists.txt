# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.reader_io.repeat_until.custom_traits
  SOURCE
  custom_traits.cpp
  ERROR_REGEX
  "fnfelt: ReaderIO repeat_until error: rejected by custom repeat_until traits")
