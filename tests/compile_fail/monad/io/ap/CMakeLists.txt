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

# The offending type is the merged workhorse's function alias (`fn_io_type {aka int}`), so pin
# only through the reason and leave the type's spelling free.
set(_expected_error_regex
    [[fnfelt: IO ap error: Named ap\{\(<unknown>\(\)\(<unknown>\(\)\) => <unknown>\)\}: ]]
    [[rejected by custom ap source traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.fn_not_io_custom_traits
  SOURCE
  ap_fn_not_io_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})
