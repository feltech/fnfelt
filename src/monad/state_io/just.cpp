// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/just.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <utility>

TEST_CASE("state_io::just preserves state and supports constexpr replay")
{
    constexpr auto action = fnfelt::monad::state_io::just<int>(4);
    static_assert(action.build(8).sync_wait() == std::pair{4, 8});
    CHECK_EQ(action.build(8).sync_wait(), (std::pair{4, 8}));
    CHECK_EQ(action.build(9).sync_wait(), (std::pair{4, 9}));
    auto move_state = fnfelt::monad::state_io::just<std::unique_ptr<int>>(4);
    auto result = std::move(move_state).build(std::make_unique<int>(8)).sync_wait();
    CHECK_EQ(*result.second, 8);
    using traits_type = fnfelt::monad::state_io::detail::JustStateIOTraits<>;
    static_assert(!traits_type::validate_just(^^std::unique_ptr<int>).empty());
}
#endif  // DOCTEST_CONFIG_DISABLE
