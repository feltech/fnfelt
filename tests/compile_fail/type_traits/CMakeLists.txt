# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

set(_prefix type_traits)

fnfelt_test_compile_fail(
  ${_prefix}.rebind_bad_type
  SOURCE
  rebind_bad_type.cpp
  ERROR_REGEX
  "fnfelt: rebind_t cannot rebind: is not a template: int")

fnfelt_test_compile_fail(
  ${_prefix}.rebind_wrong_arity
  SOURCE
  rebind_wrong_arity.cpp
  ERROR_REGEX
  "fnfelt: rebind_t cannot rebind: cannot be specialized with the requested type arguments: \
std::map<int, double>")

fnfelt_test_compile_fail(
  ${_prefix}.rebind_stub
  SOURCE
  rebind_stub.cpp
  ERROR_REGEX
  "fnfelt: rebind_t cannot rebind: stub validator reason: std::vector<int>")

fnfelt_test_compile_fail(
  ${_prefix}.specialise_unspecialise_stub
  SOURCE
  specialise_unspecialise_stub.cpp
  ERROR_REGEX
  "fnfelt: Unspecialise requires a class template specialization with only type parameters: stub \
validator reason: int")
