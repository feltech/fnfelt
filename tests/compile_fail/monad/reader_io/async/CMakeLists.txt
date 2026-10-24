# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.reader_io.async)

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  async_custom_traits.cpp
  ERROR_REGEX
  "fnfelt: RejectReader async action is invalid: rejected by custom async traits: ")
