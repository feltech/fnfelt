// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/invoke.hpp>

#include <type_traits>
#include <vector>

// `int` is neither directly invocable with std::vector<int> nor a tuple of arguments the callable
// accepts, so it must take the unsupported path and fail with the friendly static_assert message.

using Result = fnfelt::invoke_or_apply_result_t<std::vector<int>, int>;
static_assert(std::is_same_v<Result, Result>);