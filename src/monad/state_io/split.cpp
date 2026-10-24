// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/split.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/state_io/just.hpp>

TEST_CASE("StateIO split validates stored continuations before receiving input")
{
    using traits_type = fnfelt::monad::state_io::detail::SplitStateIOTraits<>;
    static_assert(!traits_type::validate_split_factory(^^std::tuple<int>, ^^int).empty());
    static_assert(!traits_type::validate_split_factory(^^std::tuple<>, ^^int &).empty());
    static_assert(traits_type::validate_split_factory(^^std::tuple<>, ^^int).empty());
}

TEST_CASE("StateIO split pairs distinct inputs with continuations")
{
    constexpr auto branch = [](int value) { return fnfelt::monad::state_io::just<int>(value * 2); };
    constexpr auto action = fnfelt::monad::state_io::split<int>(branch, branch)(std::tuple{2, 3});
    static_assert(action.build(4).sync_wait() == std::pair{std::tuple{4, 6}, 4});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{std::tuple{4, 6}, 4}));
    using traits_type = fnfelt::monad::state_io::detail::SplitStateIOTraits<>;
    static_assert(
        !traits_type::validate_split(^^std::tuple<decltype(branch)>, ^^std::tuple<>, ^^int)
             .empty());
}

TEST_CASE("StateIO split consumes move-only distinct inputs and state")
{
    auto branch = [](std::unique_ptr<int> value)
    {
        return fnfelt::monad::state_io::create<std::unique_ptr<int>>(
            [value = std::move(value)](auto state) mutable
            {
                return fnfelt::monad::io::create(
                    [value = std::move(value), state = std::move(state)]() mutable
                    {
                        *state += *value;
                        return std::pair{std::move(value), std::move(state)};
                    });
            });
    };
    auto action = fnfelt::monad::state_io::split<std::unique_ptr<int>>(
        branch, branch)(std::tuple{std::make_unique<int>(2), std::make_unique<int>(3)});
    auto result = std::move(action).build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(*std::get<0>(result.first), 2);
    CHECK_EQ(*std::get<1>(result.first), 3);
    CHECK_EQ(*result.second, 9);
}
#endif  // DOCTEST_CONFIG_DISABLE
