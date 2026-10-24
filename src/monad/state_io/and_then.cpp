// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/and_then.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/just.hpp>

TEST_CASE("StateIO and_then threads updated state and spreads logical values only")
{
    constexpr auto source = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{std::tuple{2, 3}, state + 1}); });
    constexpr auto action = source.and_then(
        [](int first, int second)
        {
            return fnfelt::monad::state_io::create<int>(
                [=](int state)
                { return fnfelt::monad::io::just(std::pair{first + second + state, state + 1}); });
        });
    static_assert(action.build(4).sync_wait() == std::pair{10, 6});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{10, 6}));
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{10, 6}));
}

TEST_CASE("StateIO and_then supports move-only state and value")
{
    auto source = fnfelt::monad::state_io::create<std::unique_ptr<int>>(
        [](auto state)
        {
            return fnfelt::monad::io::create(
                [state = std::move(state)]() mutable
                { return std::pair{std::make_unique<int>(2), std::move(state)}; });
        });
    auto action = std::move(source).and_then(
        [](std::unique_ptr<int> value)
        {
            return fnfelt::monad::state_io::create<std::unique_ptr<int>>(
                [value = std::move(value)](auto state) mutable
                {
                    return fnfelt::monad::io::create(
                        [value = std::move(value), state = std::move(state)]() mutable
                        {
                            *state += *value;
                            return std::pair{*value, std::move(state)};
                        });
                });
        });
    auto result = std::move(action).build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(result.first, 2);
    CHECK_EQ(*result.second, 6);
}

TEST_CASE("StateIO and_then keeps borrowed state alive on mixed async paths")
{
    auto source = fnfelt::monad::state_io::create<std::string>(
        [](std::string const & state)
        { return fnfelt::monad::io::create([&state] { return std::pair{state.size(), state}; }); });
    auto action = source.and_then(
        [](std::size_t size)
        {
            return fnfelt::monad::state_io::create<std::string>(
                [size](std::string state)
                {
                    return fnfelt::monad::io::create_async(
                        [](auto,
                           std::size_t value,
                           std::string current) -> lf::task<std::pair<std::size_t, std::string>>
                        { co_return std::pair{value, std::move(current)}; },
                        size,
                        std::move(state));
                });
        });
    auto result = action.build(std::string(1000, 'x')).sync_wait(lf::lazy_pool{2});
    CHECK_EQ(result.first, 1000);
    CHECK_EQ(result.second.size(), 1000);
}
#endif  // DOCTEST_CONFIG_DISABLE
