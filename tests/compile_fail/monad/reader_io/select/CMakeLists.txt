# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.reader_io.select.custom_traits
  SOURCE
  select_custom_traits.cpp
  ERROR_REGEX
  [[fnfelt: SelectReaderIO select error:.*rejected by custom selective traits]])
