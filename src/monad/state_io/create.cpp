// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/create.hpp>

#include <doctest/doctest.h>

#include <string_view>
#include <utility>

#include <fnfelt/monad/io/just.hpp>

namespace
{
using traits_type = fnfelt::monad::state_io::detail::CreateStateIOTraits<>;
constexpr auto valid_action = [](int state)
{ return fnfelt::monad::io::just(std::pair{state * 2, state + 1}); };
constexpr auto wrong_state = [](int state)
{ return fnfelt::monad::io::just(std::pair{state, true}); };
constexpr auto no_pair = [](int state) { return fnfelt::monad::io::just(state); };
constexpr auto no_io = [](int state) { return state; };
struct Incomplete;
}  // namespace

TEST_CASE("state_io::create validates state and result shape")
{
    using fnfelt::literals::operator""_ss;
    static_assert(traits_type::validate_action(^^decltype(valid_action), ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(wrong_state), ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(no_pair), ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(no_io), ^^int).empty());
    static_assert(!traits_type::validate_action(^^Incomplete, ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(valid_action), ^^Incomplete).empty());
    static_assert(!traits_type::validate_action(^^decltype(valid_action), ^^int &).empty());
    CHECK_EQ(
        fnfelt::monad::state_io::create<int>(valid_action).build(3).sync_wait(), (std::pair{6, 4}));
    constexpr auto named = fnfelt::monad::state_io::create<"named"_ss, int>(valid_action);
    static_assert(decltype(named)::traits::name == "named");
    static_assert(named.build(3).sync_wait() == std::pair{6, 4});
}
#endif  // DOCTEST_CONFIG_DISABLE
