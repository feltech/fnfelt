// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/StateIO.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>

namespace
{
struct Increment
{
    constexpr auto operator()(int state) const
    {
        return fnfelt::monad::io::just(std::pair{state, state + 1});
    }
};

struct AsyncIncrement
{
    auto operator()(int state) const
    {
        return fnfelt::monad::io::create_async(
            [](auto, int current) -> lf::task<std::pair<int, int>>
            { co_return std::pair{current, current + 1}; },
            state);
    }
};

struct Consume
{
    std::unique_ptr<int> value;
    auto operator()(this Consume && self, std::unique_ptr<int> state)
    {
        return fnfelt::monad::io::create(
            [result = std::pair{std::move(self.value), std::move(state)}]() mutable
            { return std::move(result); });
    }
};
}  // namespace

TEST_CASE("StateIO binds and returns updated state")
{
    using state_type = fnfelt::monad::state_io::StateIO<int, Increment>;
    static_assert(std::is_same_v<state_type::value_type, int>);
    constexpr state_type action{Increment{}};
    static_assert(action.build(4).sync_wait() == std::pair{4, 5});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{4, 5}));
    CHECK_EQ(action.build(8).io().build().sync_wait(), (std::pair{8, 9}));
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{4, 5}));
}

TEST_CASE("StateIO supports asynchronous actions under the supplied scheduler")
{
    fnfelt::monad::state_io::StateIO<int, AsyncIncrement> action{AsyncIncrement{}};
    CHECK_EQ(action.build(4).sync_wait(lf::lazy_pool{2}), (std::pair{4, 5}));
}

TEST_CASE("StateIO consumes move-only actions and state")
{
    fnfelt::monad::state_io::StateIO<std::unique_ptr<int>, Consume> action{
        Consume{std::make_unique<int>(4)}};
    auto result = std::move(action).build(std::make_unique<int>(8)).sync_wait();
    CHECK_EQ(*result.first, 4);
    CHECK_EQ(*result.second, 8);
    using traits_type = fnfelt::monad::state_io::detail::CreateStateIOTraits<>;
    static_assert(traits_type::validate_build(^^Consume &&).empty());
    static_assert(!traits_type::validate_build(^^Consume &).empty());
    static_assert(!traits_type::validate_sync_wait(^^int &).empty());
    static_assert(!traits_type::validate_sync_wait(^^int const &&).empty());
    static_assert(traits_type::validate_sync_wait(^^int &&).empty());
}

TEST_CASE("StateIO core validates its action and state contract")
{
    using traits_type = fnfelt::monad::state_io::detail::CreateStateIOTraits<>;
    constexpr auto no_pair = [](int state) { return fnfelt::monad::io::just(state); };
    constexpr auto wrong_state = [](int state)
    { return fnfelt::monad::io::just(std::pair{state, true}); };
    constexpr auto reference_value = [](int state)
    { return fnfelt::monad::io::just(std::pair<int &, int>{state, state}); };
    constexpr auto nested_value = [](int state)
    { return fnfelt::monad::io::just(std::pair{fnfelt::monad::io::just(state), state}); };
    static_assert(traits_type::validate_action(^^Increment, ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(no_pair), ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(wrong_state), ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(reference_value), ^^int).empty());
    static_assert(!traits_type::validate_action(^^decltype(nested_value), ^^int).empty());
    static_assert(!traits_type::validate_action(^^Increment, ^^int &).empty());
}
#endif  // DOCTEST_CONFIG_DISABLE
