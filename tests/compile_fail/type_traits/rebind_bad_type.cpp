// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/type_traits.hpp>

#include <type_traits>

// `int` is not a template specialization, so rebind_t must fail with the friendly static_assert
// message naming the first failing precondition.

using Rebound = fnfelt::rebind_t<int, double>;
static_assert(std::is_same_v<Rebound, Rebound>);
