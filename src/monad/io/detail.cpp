// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/detail.hpp>

#include <doctest/doctest.h>

#include <array>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>

namespace
{
/// Incomplete type - has no meaningful invocation result.
struct Incomplete;
}  // namespace

TEST_CASE("detail::action_value_meta reflects the action's result")
{
    using fnfelt::monad::io::detail::action_value_meta;

    static_assert(is_same_type(action_value_meta(^^int (*)()), ^^int));
    // Non-callable and incomplete actions have no meaningful result.
    static_assert(is_same_type(action_value_meta(^^int), ^^void));
    static_assert(is_same_type(action_value_meta(^^Incomplete), ^^void));
}

#endif  // DOCTEST_CONFIG_DISABLE
