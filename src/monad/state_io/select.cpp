// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/select.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <expected>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/just.hpp>

namespace
{
namespace io = fnfelt::monad::io;
namespace state_io = fnfelt::monad::state_io;
using choice_type = std::expected<int, int>;
using function_type = int (*)(int);
inline constexpr char selected_name[] = "SelectedStateIO";

template <bool is_async>
auto make_choice(bool has_value, std::atomic<int> * executions)
{
    return state_io::create<int>(
        [has_value, executions](int state)
        {
            if constexpr (is_async)
            {
                return io::create_async(
                    [](auto, int current, bool is_complete, std::atomic<int> * count)
                        -> lf::task<std::pair<choice_type, int>>
                    {
                        ++*count;
                        co_return std::pair{
                            is_complete ? choice_type{42} : choice_type{std::unexpected{current}},
                            current + 1};
                    },
                    state,
                    has_value,
                    executions);
            }
            else
            {
                return io::create(
                    [state, has_value, executions]
                    {
                        ++*executions;
                        return std::pair{
                            has_value ? choice_type{42} : choice_type{std::unexpected{state}},
                            state + 1};
                    });
            }
        });
}

template <bool is_async>
auto make_fallback(std::atomic<int> * builds, std::atomic<int> * executions)
{
    return state_io::create<int>(
        [builds, executions](int state)
        {
            ++*builds;
            CHECK_EQ(state, 5);
            if constexpr (is_async)
            {
                return io::create_async(
                    [](auto,
                       int current,
                       std::atomic<int> * count) -> lf::task<std::pair<function_type, int>>
                    {
                        ++*count;
                        co_return std::pair{+[](int input) { return input * 2; }, current + 10};
                    },
                    state,
                    executions);
            }
            else
            {
                return io::create(
                    [state, executions]
                    {
                        ++*executions;
                        return std::pair{+[](int input) { return input * 2; }, state + 10};
                    });
            }
        });
}

struct MoveOnlyFunction
{
    std::unique_ptr<int> offset;
    auto operator()(std::unique_ptr<int> input) &&
    {
        return std::make_unique<int>(*input + *offset);
    }
};

template <class TValue>
struct MoveValue
{
    TValue value;
    auto operator()(this auto && self)
    {
        return std::move(self.value);
    }
};

struct MoveOnlyFallback
{
    std::unique_ptr<int> offset;
    auto operator()(this auto && self, std::unique_ptr<int> state)
    {
        ++*state;
        return io::create(
            MoveValue{std::pair{MoveOnlyFunction{std::move(self.offset)}, std::move(state)}});
    }
};

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

struct DirectAndSpread
{
    constexpr int operator()(std::tuple<int, int> input) const
    {
        return std::get<0>(input) + std::get<1>(input);
    }
    constexpr int operator()(int first, int second) const
    {
        return first * second;
    }
};
}  // namespace

TEST_CASE("StateIO select preserves choice state and threads it into the fallback")
{
    constexpr auto choice = state_io::create<int>(
        [](int state)
        { return io::just(std::pair{choice_type{std::unexpected{state}}, state + 1}); });
    constexpr auto fallback = state_io::create<int>(
        [](int state)
        { return io::just(std::pair{[state](int input) { return input + state; }, state + 10}); });
    constexpr auto action = state_io::select(choice, fallback);
    static_assert(action.build(4).sync_wait() == std::pair{9, 15});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{9, 15}));
    constexpr auto complete = state_io::create<int>(
        [](int state) { return io::just(std::pair{choice_type{42}, state + 1}); });
    static_assert(complete.select(fallback).build(4).sync_wait() == std::pair{42, 5});
    constexpr auto named = complete.select<selected_name>(fallback);
    static_assert(decltype(named)::traits::name == selected_name);
    constexpr auto named_free = state_io::select<selected_name>(complete, fallback);
    static_assert(decltype(named_free)::traits::name == selected_name);
    CHECK_EQ(named.build(4).sync_wait(), (std::pair{42, 5}));
}

TEST_CASE("StateIO select conditionally materialises and executes mixed sync async effects")
{
    auto check = []<bool is_choice_async, bool is_fallback_async>()
    {
        for (bool has_value : {false, true})
        {
            std::atomic<int> choices = 0;
            std::atomic<int> builds = 0;
            std::atomic<int> fallbacks = 0;
            auto action = state_io::select(
                make_choice<is_choice_async>(has_value, &choices),
                make_fallback<is_fallback_async>(&builds, &fallbacks));
            CHECK_EQ(choices.load(), 0);
            CHECK_EQ(builds.load(), 0);
            auto bound = std::move(action).build(4);
            CHECK_EQ(choices.load(), 0);
            CHECK_EQ(builds.load(), 0);
            auto inner = std::move(bound).io();
            CHECK_EQ(choices.load(), 0);
            CHECK_EQ(builds.load(), 0);
            std::pair<int, int> result;
            if constexpr (is_choice_async || is_fallback_async)
            {
                result = std::move(inner).build().sync_wait(lf::lazy_pool{2});
            }
            else
            {
                result = std::move(inner).build().sync_wait();
            }
            CHECK_EQ(result, (has_value ? std::pair{42, 5} : std::pair{8, 15}));
            CHECK_EQ(choices.load(), 1);
            CHECK_EQ(builds.load(), has_value ? 0 : 1);
            CHECK_EQ(fallbacks.load(), has_value ? 0 : 1);
        }
    };
    check.operator()<false, false>();
    check.operator()<false, true>();
    check.operator()<true, false>();
    check.operator()<true, true>();
}

TEST_CASE("StateIO select moves its state values and produced function")
{
    using state_type = std::unique_ptr<int>;
    using owned_choice = std::expected<state_type, state_type>;
    auto check = [](bool has_value)
    {
        auto choice = state_io::create<state_type>(
            [has_value](state_type state)
            {
                ++*state;
                auto value = has_value ? owned_choice{std::make_unique<int>(42)}
                                       : owned_choice{std::unexpected{std::make_unique<int>(21)}};
                return io::create(MoveValue{std::pair{std::move(value), std::move(state)}});
            });
        auto fallback = state_io::create<state_type>(MoveOnlyFallback{std::make_unique<int>(2)});
        auto result = std::move(choice)
                          .select(std::move(fallback))
                          .build(std::make_unique<int>(4))
                          .sync_wait();
        CHECK_EQ(*result.first, has_value ? 42 : 23);
        CHECK_EQ(*result.second, has_value ? 5 : 6);
    };
    check(false);
    check(true);
}

TEST_CASE("StateIO select moves state through an asynchronous choice")
{
    using state_type = std::unique_ptr<int>;
    using owned_choice = std::expected<state_type, state_type>;
    for (bool has_value : {false, true})
    {
        auto choice = state_io::create<state_type>(
            [has_value](state_type state)
            {
                return io::create_async(
                    [](auto,
                       state_type current,
                       bool is_complete) -> lf::task<std::pair<owned_choice, state_type>>
                    {
                        ++*current;
                        auto value = is_complete
                            ? owned_choice{std::make_unique<int>(42)}
                            : owned_choice{std::unexpected{std::make_unique<int>(21)}};
                        co_return std::pair{std::move(value), std::move(current)};
                    },
                    std::move(state),
                    has_value);
            });
        auto fallback = state_io::create<state_type>(MoveOnlyFallback{std::make_unique<int>(2)});
        auto result = std::move(choice)
                          .select(std::move(fallback))
                          .build(std::make_unique<int>(4))
                          .sync_wait(lf::lazy_pool{2});
        CHECK_EQ(*result.first, has_value ? 42 : 23);
        CHECK_EQ(*result.second, has_value ? 5 : 6);
    }
}

TEST_CASE("StateIO select preserves the produced function const qualification")
{
    constexpr auto choice = state_io::just<int>(choice_type{std::unexpected{4}});
    constexpr auto fallback = state_io::create<int>(
        [](int state) { return io::just(std::pair<QualifiedFunction const, int>{{}, state}); });
    static_assert(state_io::select(choice, fallback).build(0).sync_wait().first == 6);
    constexpr auto const_choice = state_io::create<int>(
        [](int state)
        {
            return io::just(
                std::pair<choice_type const, int>{choice_type{std::unexpected{4}}, state});
        });
    static_assert(state_io::select(const_choice, fallback).build(0).sync_wait().first == 6);
    CHECK_EQ(state_io::select(choice, fallback).build(0).sync_wait().first, 6);
}

TEST_CASE("StateIO select prefers direct tuple invocation and permits spreading")
{
    constexpr auto choice = state_io::just<int>(
        std::expected<int, std::tuple<int, int>>{std::unexpected{std::tuple{3, 4}}});
    static_assert(
        choice.select(state_io::just<int>(DirectAndSpread{})).build(9).sync_wait() ==
        std::pair{7, 9});
    constexpr auto fallback =
        state_io::just<int>([](int first, int second) { return first * second; });
    static_assert(state_io::select(choice, fallback).build(9).sync_wait() == std::pair{12, 9});
    CHECK_EQ(state_io::select(choice, fallback).build(9).sync_wait(), (std::pair{12, 9}));
}

TEST_CASE("StateIO select skips fallback materialisation after choice failure")
{
    std::atomic<int> builds = 0;
    std::atomic<int> executions = 0;
    auto choice = state_io::create<int>(
        [](int state)
        {
            return io::create(
                [state]() -> std::pair<choice_type, int>
                {
                    (void)state;
                    throw std::runtime_error{"choice failed"};
                });
        });
    auto fallback = make_fallback<true>(&builds, &executions);
    CHECK_THROWS_AS(
        static_cast<void>(state_io::select(choice, fallback).build(4).sync_wait(lf::lazy_pool{2})),
        std::runtime_error);
    CHECK_EQ(builds.load(), 0);
    CHECK_EQ(executions.load(), 0);
}

TEST_CASE("StateIO select propagates fallback materialisation and invocation failures")
{
    std::atomic<int> choices = 0;
    auto choice = make_choice<true>(false, &choices);
    auto fallback = state_io::create<int>(
        [](int state) -> decltype(io::just(std::pair{function_type{}, state}))
        { throw std::runtime_error{"fallback build failed"}; });
    CHECK_THROWS_AS(
        static_cast<void>(state_io::select(choice, fallback).build(4).sync_wait(lf::lazy_pool{2})),
        std::runtime_error);
    CHECK_EQ(choices.load(), 1);
    auto throwing =
        state_io::just<int>([](int) -> int { throw std::runtime_error{"function failed"}; });
    CHECK_THROWS_AS(
        static_cast<void>(state_io::select(choice, throwing).build(4).sync_wait(lf::lazy_pool{2})),
        std::runtime_error);
}

TEST_CASE("StateIO select validators reject invalid source and value contracts")
{
    using traits_type = state_io::detail::SelectStateIOTraits<>;
    constexpr auto choice = state_io::just<int>(choice_type{42});
    constexpr auto fallback = state_io::just<int>([](int value) { return value * 2; });
    static_assert(traits_type::validate_select(^^decltype(choice), ^^decltype(fallback)).empty());
    static_assert(!traits_type::validate_select(^^int, ^^decltype(fallback)).empty());
    static_assert(!traits_type::validate_select(^^decltype(choice), ^^int).empty());
    constexpr auto different_state = state_io::just<double>([](int value) { return value; });
    static_assert(
        !traits_type::validate_select(^^decltype(choice), ^^decltype(different_state)).empty());
    static_assert(
        !traits_type::validate_select(^^decltype(fallback), ^^decltype(fallback)).empty());
    constexpr auto void_choice = state_io::just<int>(std::expected<void, int>{});
    static_assert(
        !traits_type::validate_select(^^decltype(void_choice), ^^decltype(fallback)).empty());
    constexpr auto wrong_result =
        state_io::just<int>([](int value) { return static_cast<double>(value); });
    static_assert(
        !traits_type::validate_select(^^decltype(choice), ^^decltype(wrong_result)).empty());
    constexpr auto wrong_argument =
        state_io::just<int>([](std::string_view value) { return value.size(); });
    static_assert(
        !traits_type::validate_select(^^decltype(choice), ^^decltype(wrong_argument)).empty());
    using nested_type = decltype(state_io::just<int>(1));
    constexpr auto nested_choice =
        state_io::just<int>(std::expected<nested_type, int>{state_io::just<int>(1)});
    constexpr auto nested_fallback =
        state_io::just<int>([](int value) { return state_io::just<int>(value); });
    static_assert(
        !traits_type::validate_select(^^decltype(nested_choice), ^^decltype(nested_fallback))
             .empty());
    CHECK(traits_type::validate_select(^^decltype(choice), ^^decltype(fallback)).empty());
}
#endif  // DOCTEST_CONFIG_DISABLE
