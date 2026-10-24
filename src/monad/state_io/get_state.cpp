// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/get_state.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>

TEST_CASE("StateIO get_state copies current state and preserves the original")
{
    constexpr auto action = fnfelt::monad::state_io::get_state<int>();
    static_assert(action.build(4).sync_wait() == std::pair{4, 4});
    auto strings = fnfelt::monad::state_io::get_state<std::string>();
    auto result = strings.build("initial").sync_wait();
    result.first += " changed";
    CHECK_EQ(result.second, "initial");
    auto inner = strings.build("replay").io();
    CHECK_EQ(inner.build().sync_wait(), (std::pair{std::string{"replay"}, std::string{"replay"}}));
    CHECK_EQ(inner.build().sync_wait(), (std::pair{std::string{"replay"}, std::string{"replay"}}));
    using traits_type = fnfelt::monad::state_io::detail::GetStateStateIOTraits<>;
    static_assert(!traits_type::validate_get_state(^^std::unique_ptr<int>).empty());
    static_assert(!traits_type::validate_get_state(^^int &).empty());
}
#endif  // DOCTEST_CONFIG_DISABLE
