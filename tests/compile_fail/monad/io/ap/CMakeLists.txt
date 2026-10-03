# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.ap)

set(_expected_error_regex [[fnfelt: IO ap error: ApIO{\(FnIO\(\)\(ValueIO\(\)\) => ]]
                          [[int\)}: rejected by custom ap traits: ]] [[fn_io_type]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  ap_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
