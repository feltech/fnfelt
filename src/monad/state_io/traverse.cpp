// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/traverse.hpp>

#include <doctest/doctest.h>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/just.hpp>

TEST_CASE("StateIO traversal maps values then threads updated state")
{
    constexpr auto action = fnfelt::monad::state_io::traverse<int>(
        std::tuple{2, 3},
        [](int value)
        {
            return fnfelt::monad::state_io::create<int>(
                [value](int state)
                { return fnfelt::monad::io::just(std::pair{value + state, state + 1}); });
        });
    static_assert(action.build(4).sync_wait() == std::pair{std::tuple{6, 8}, 6});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{std::tuple{6, 8}, 6}));
}
#endif  // DOCTEST_CONFIG_DISABLE
