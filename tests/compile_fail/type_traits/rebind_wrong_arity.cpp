// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/type_traits.hpp>

#include <map>
#include <type_traits>

// std::map is a type-parameter-only specialization, but it needs at least a key and value type,
// so a single replacement argument cannot specialize it (template default arguments are not
// applied). rebind_t must fail with the arguments error.

using Rebound = fnfelt::rebind_t<std::map<int, double>, float>;
static_assert(std::is_same_v<Rebound, Rebound>);
