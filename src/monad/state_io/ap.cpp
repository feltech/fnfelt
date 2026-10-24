// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/ap.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>

namespace
{
using function_type = int (*)(int);
struct QualifiedFunction
{
    constexpr int operator()(int value) &&
    {
        return value + 1;
    }
    constexpr int operator()(int value) const &&
    {
        return value + 2;
    }
};
}  // namespace

TEST_CASE("StateIO ap threads state from function into value computation")
{
    constexpr auto function = fnfelt::monad::state_io::create<int>(
        [](int state)
        {
            return fnfelt::monad::io::just(
                std::pair{[](int value) { return value * 2; }, state + 1});
        });
    constexpr auto value = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{state, state + 1}); });
    constexpr auto action = fnfelt::monad::state_io::ap(function, value);
    static_assert(action.build(4).sync_wait() == std::pair{10, 6});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{10, 6}));
}

TEST_CASE("StateIO ap retains produced function const qualification")
{
    constexpr auto function = fnfelt::monad::state_io::create<int>(
        [](int state)
        { return fnfelt::monad::io::just(std::pair<QualifiedFunction const, int>{{}, state}); });
    constexpr auto value = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{4, state}); });
    static_assert(fnfelt::monad::state_io::ap(function, value).build(0).sync_wait().first == 6);
    using traits_type = fnfelt::monad::state_io::detail::ApStateIOTraits<>;
    static_assert(!traits_type::validate_ap(^^int, ^^int).empty());
    static_assert(!traits_type::validate_ap(^^decltype(value), ^^decltype(value)).empty());
}

TEST_CASE("StateIO ap async function completes before value materialisation")
{
    std::atomic<bool> has_completed = false;
    auto function = fnfelt::monad::state_io::create<int>(
        [&](int state)
        {
            return fnfelt::monad::io::create_async(
                [](auto,
                   int current,
                   std::atomic<bool> * flag) -> lf::task<std::pair<function_type, int>>
                {
                    flag->store(true);
                    co_return std::pair{+[](int value) { return value * 2; }, current + 1};
                },
                state,
                &has_completed);
        });
    auto value = fnfelt::monad::state_io::create<int>(
        [&](int state)
        {
            CHECK(has_completed.load());
            return fnfelt::monad::io::just(std::pair{state, state + 1});
        });
    CHECK_EQ(
        fnfelt::monad::state_io::ap(function, value).build(4).sync_wait(lf::lazy_pool{2}),
        (std::pair{10, 6}));
}

TEST_CASE("StateIO ap does not materialise value action after function failure")
{
    bool has_materialised = false;
    auto function = fnfelt::monad::state_io::create<int>(
        []([[maybe_unused]] int state)
        {
            return fnfelt::monad::io::create(
                []() -> std::pair<function_type, int> { throw std::runtime_error{"failure"}; });
        });
    auto value = fnfelt::monad::state_io::create<int>(
        [&](int state)
        {
            has_materialised = true;
            return fnfelt::monad::io::just(std::pair{state, state});
        });
    CHECK_THROWS_AS(
        static_cast<void>(fnfelt::monad::state_io::ap(function, value).build(4).sync_wait()),
        std::runtime_error);
    CHECK_FALSE(has_materialised);
}
#endif  // DOCTEST_CONFIG_DISABLE
