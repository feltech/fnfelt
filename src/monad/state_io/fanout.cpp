// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/fanout.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/just.hpp>

TEST_CASE("StateIO fanout validates stored continuations before receiving input")
{
    using traits_type = fnfelt::monad::state_io::detail::FanoutStateIOTraits<>;
    static_assert(!traits_type::validate_fanout_factory(^^std::tuple<int>, ^^int).empty());
    static_assert(!traits_type::validate_fanout_factory(^^std::tuple<>, ^^int &).empty());
    static_assert(traits_type::validate_fanout_factory(^^std::tuple<>, ^^int).empty());
}

TEST_CASE("StateIO fanout shares stable input and threads branch state sequentially")
{
    constexpr auto branch = [](int & first, int & second)
    {
        return fnfelt::monad::state_io::create<int>(
            [value = first + second](int state)
            { return fnfelt::monad::io::just(std::pair{value + state, state + 1}); });
    };
    auto adapter = fnfelt::monad::state_io::fanout<int>(branch, branch);
    std::tuple values{2, 3};
    auto action = adapter(values);
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{std::tuple{9, 10}, 6}));
    auto empty = fnfelt::monad::state_io::fanout<int>()(std::make_unique<int>(4));
    CHECK_EQ(empty.build(4).sync_wait(), (std::pair{std::tuple{}, 4}));
}

TEST_CASE("StateIO fanout keeps move-only shared input available to every branch")
{
    auto branch = [](std::unique_ptr<int> const & value)
    { return fnfelt::monad::state_io::just<int>(*value); };
    auto action = fnfelt::monad::state_io::fanout<int>(branch, branch)(std::make_unique<int>(2));
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{std::tuple{2, 2}, 4}));
}
#endif  // DOCTEST_CONFIG_DISABLE
