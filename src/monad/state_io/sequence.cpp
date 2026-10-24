// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/sequence.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <immer/array.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>

namespace
{
constexpr auto increment = fnfelt::monad::state_io::create<int>(
    [](int state) { return fnfelt::monad::io::just(std::pair{state, state + 1}); });
}  // namespace

TEST_CASE("StateIO sequence preserves tuple values and threads state in order")
{
    constexpr auto action = fnfelt::monad::state_io::sequence<int>(increment, increment, increment);
    static_assert(action.build(4).sync_wait() == std::pair{std::tuple{4, 5, 6}, 7});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{std::tuple{4, 5, 6}, 7}));
    auto empty = fnfelt::monad::state_io::sequence<std::unique_ptr<int>>();
    auto result = std::move(empty).build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(*result.second, 4);
}

TEST_CASE("StateIO immutable-array sequence executes sequentially and replays")
{
    auto sources = immer::array{increment, increment, increment};
    auto action = fnfelt::monad::state_io::sequence<int>(sources);
    auto result = action.build(4).sync_wait();
    CHECK_EQ(result.first, (immer::array{4, 5, 6}));
    CHECK_EQ(result.second, 7);
    CHECK_EQ(action.build(4).sync_wait().first, (immer::array{4, 5, 6}));
    auto empty = fnfelt::monad::state_io::sequence<int>(
        immer::array<std::remove_cvref_t<decltype(increment)>>{});
    CHECK_EQ(empty.build(4).sync_wait().second, 4);
}

TEST_CASE("StateIO async sequence runs under one scheduler in state order")
{
    auto source = fnfelt::monad::state_io::create<int>(
        [](int state)
        {
            return fnfelt::monad::io::create_async(
                [](auto, int current) -> lf::task<std::pair<int, int>>
                { co_return std::pair{current, current + 1}; },
                state);
        });
    auto tuple_action = fnfelt::monad::state_io::sequence<int>(source, increment, source);
    CHECK_EQ(
        tuple_action.build(4).sync_wait(lf::lazy_pool{2}), (std::pair{std::tuple{4, 5, 6}, 7}));
    auto array_action =
        fnfelt::monad::state_io::sequence<int>(immer::array{source, source, source});
    auto result = array_action.build(4).sync_wait(lf::lazy_pool{2});
    CHECK_EQ(result.first, (immer::array{4, 5, 6}));
    CHECK_EQ(result.second, 7);
}
TEST_CASE("StateIO sequence preserves whole pair and tuple logical values")
{
    constexpr auto tuples = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{std::tuple{2, 3}, state + 1}); });
    constexpr auto pairs = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{std::pair{4, 5}, state + 1}); });
    constexpr auto action = fnfelt::monad::state_io::sequence<int>(tuples, pairs);
    static_assert(
        action.build(4).sync_wait() == std::pair{std::tuple{std::tuple{2, 3}, std::pair{4, 5}}, 6});
    constexpr auto qualified = fnfelt::monad::state_io::create<int>(
        [](int state)
        {
            return fnfelt::monad::io::create([state]
                                             { return std::pair<int const, int>{2, state}; });
        });
    auto qualified_action = fnfelt::monad::state_io::sequence<int>(qualified);
    static_assert(std::is_same_v<decltype(qualified_action)::value_type, std::tuple<int const>>);
    using traits_type = fnfelt::monad::state_io::detail::SequenceStateIOTraits<>;
    static_assert(!traits_type::validate_sequence(^^std::tuple<int>, ^^int).empty());
    static_assert(
        !traits_type::validate_sequence(^^std::tuple<decltype(increment)>, ^^bool).empty());
}

TEST_CASE("StateIO array sequence threads move-only state and produces move-only values")
{
    auto source = fnfelt::monad::state_io::create<std::unique_ptr<int>>(
        [](auto state)
        {
            return fnfelt::monad::io::create(
                [state = std::move(state)]() mutable
                {
                    auto value = std::make_unique<int>(*state);
                    ++*state;
                    return std::pair{std::move(value), std::move(state)};
                });
        });
    auto action =
        fnfelt::monad::state_io::sequence<std::unique_ptr<int>>(immer::array{source, source});
    auto result = action.build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(*result.first[0], 4);
    CHECK_EQ(*result.first[1], 5);
    CHECK_EQ(*result.second, 6);
    auto replay = action.build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(*replay.first[0], 4);
    CHECK_EQ(*replay.first[1], 5);
}

TEST_CASE("StateIO async array failure prevents later action materialisation")
{
    std::atomic<int> materialisations{0};
    struct CountingState
    {
        int current;
        std::atomic<int> * materialisations;
    };
    auto source = fnfelt::monad::state_io::create<CountingState>(
        [](CountingState state)
        {
            ++*state.materialisations;
            return fnfelt::monad::io::create_async(
                [](auto, CountingState current) -> lf::task<std::pair<int, CountingState>>
                {
                    if (current.current == 5)
                    {
                        throw std::runtime_error{"failure"};
                    }
                    auto const value = current.current++;
                    co_return std::pair{value, current};
                },
                state);
        });
    auto action =
        fnfelt::monad::state_io::sequence<CountingState>(immer::array{source, source, source});
    CHECK_THROWS_AS(
        static_cast<void>(
            action.build(CountingState{4, &materialisations}).sync_wait(lf::lazy_pool{2})),
        std::runtime_error);
    CHECK_EQ(materialisations.load(), 2);
}

TEST_CASE("StateIO array validation diagnoses unsupported Immer input assignment")
{
    int marker = 0;
    auto source = fnfelt::monad::state_io::create<int>(
        [&marker](int state)
        {
            ++marker;
            return fnfelt::monad::io::just(std::pair{state, state + 1});
        });
    using traits_type = fnfelt::monad::state_io::detail::SequenceStateIOTraits<>;
    static_assert(
        traits_type::validate_sequence(^^immer::array<decltype(source)>, ^^int) ==
        "trivially copyable array elements must be copy assignable for Immer storage");
}

#endif  // DOCTEST_CONFIG_DISABLE
