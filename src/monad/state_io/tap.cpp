// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/tap.hpp>

#include <doctest/doctest.h>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/get_state.hpp>

TEST_CASE("StateIO tap discards a complete logical tuple and threads state")
{
    constexpr auto source = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{std::tuple{2, 3}, state + 1}); });
    constexpr auto action = source.tap(fnfelt::monad::state_io::get_state<int>());
    static_assert(action.build(4).sync_wait() == std::pair{5, 5});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{5, 5}));
    using traits_type = fnfelt::monad::state_io::detail::TapStateIOTraits<>;
    static_assert(!traits_type::validate_tap(^^int, ^^int).empty());
}
#endif  // DOCTEST_CONFIG_DISABLE
