# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix monad.io.traverse)

# The offending type is the continuation, named when every element of the all-valid tuple has
# already passed the scan, whose spelling is compiler-formatted, so pin through the custom reason
# and leave the element list and type free.
set(_expected_error_regex
    [[fnfelt: IO traverse error: TraverseIO\{.*\}: rejected by custom traverse traits: ]])

fnfelt_test_compile_fail(
  ${_prefix}.custom_traits
  SOURCE
  traverse_custom_traits.cpp
  ERROR_REGEX
  ${_expected_error_regex})

# As the default-traits case, but through the named overload: the non-tuple argument reaches the
# named traits validation, whose name prefixes the friendly diagnostic. The offending type's
# spelling is compiler-formatted, so pin through the reason and leave the type free.
set(_expected_error_regex [[fnfelt: IO traverse error: Traverse\{<unknown> => <unknown>\}: ]]
                          [[provided tuple is not a std::tuple: ]])

fnfelt_test_compile_fail(
  ${_prefix}.named_first_arg_not_tuple
  SOURCE
  traverse_named_first_arg_not_tuple.cpp
  ERROR_REGEX
  ${_expected_error_regex})
