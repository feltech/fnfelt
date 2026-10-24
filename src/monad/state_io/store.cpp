// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/store.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/state_io/just.hpp>

namespace
{
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

TEST_CASE("StateIO store preserves logical value const qualification")
{
    constexpr auto source = fnfelt::monad::state_io::create<int>(
        [](int state)
        {
            return fnfelt::monad::io::create(
                [state] { return std::pair<QualifiedFunction const, int>{{}, state}; });
        });
    constexpr auto action =
        source.store([](QualifiedFunction const &, int state) { return state + 1; });
    static_assert(std::is_same_v<decltype(action)::value_type, QualifiedFunction const>);
    static_assert(action.build(4).sync_wait().first(0) == 2);
}

TEST_CASE("StateIO store invocation forwards direct and spread values")
{
    auto consume = [](std::unique_ptr<int> value, int state) { return *value + state; };
    CHECK_EQ(
        fnfelt::monad::state_io::detail::invoke_store(consume, std::make_unique<int>(2), 4), 6);
    CHECK_EQ(
        fnfelt::monad::state_io::detail::invoke_store(
            consume, std::tuple{std::make_unique<int>(3)}, 4),
        7);
}

TEST_CASE("StateIO store preserves value and replaces updated state")
{
    constexpr auto action = fnfelt::monad::state_io::just<int>(2).store(
        [](int const & value, int state) { return value + state; });
    static_assert(action.build(4).sync_wait() == std::pair{2, 6});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{2, 6}));
}

TEST_CASE("StateIO store preserves move-only values and threads move-only state")
{
    auto source = fnfelt::monad::state_io::create<std::unique_ptr<int>>(
        [](auto state)
        {
            return fnfelt::monad::io::create(
                [state = std::move(state)]() mutable
                { return std::pair{std::make_unique<int>(2), std::move(state)}; });
        });
    auto action = std::move(source).store(
        [](std::unique_ptr<int> const & value, std::unique_ptr<int> state)
        {
            *state += *value;
            return state;
        });
    auto result = std::move(action).build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(*result.first, 2);
    CHECK_EQ(*result.second, 6);
    using traits_type = fnfelt::monad::state_io::detail::StoreStateIOTraits<>;
    static_assert(!traits_type::validate_store(^^int, ^^int).empty());
}

TEST_CASE("StateIO store spreads tuple and pair values with state last")
{
    constexpr auto action =
        fnfelt::monad::state_io::just<int>(std::tuple{2, 3})
            .store([](int first, int second, int state) { return state + first + second; });
    static_assert(action.build(4).sync_wait() == std::pair{std::tuple{2, 3}, 9});
    constexpr auto pair_action =
        fnfelt::monad::state_io::just<int>(std::pair{2, 3})
            .store([](int first, int second, int state) { return state + first + second; });
    static_assert(pair_action.build(4).sync_wait() == std::pair{std::pair{2, 3}, 9});
    constexpr auto empty_action =
        fnfelt::monad::state_io::just<int>(std::tuple{}).store([](int state) { return state + 1; });
    static_assert(empty_action.build(4).sync_wait().second == 5);
}

TEST_CASE("StateIO store prefers the whole tuple over spreading")
{
    struct Mutator
    {
        constexpr int operator()(std::tuple<int, int> const &, int state) const
        {
            return state + 1;
        }
        constexpr int operator()(int, int, int state) const
        {
            return state + 100;
        }
    };
    constexpr auto action = fnfelt::monad::state_io::just<int>(std::tuple{2, 3}).store(Mutator{});
    static_assert(action.build(4).sync_wait().second == 5);
}

TEST_CASE("StateIO store spreads move-only elements without consuming them")
{
    auto source = fnfelt::monad::state_io::create<std::unique_ptr<int>>(
        [](auto state)
        {
            return fnfelt::monad::io::create(
                [state = std::move(state)]() mutable
                {
                    return std::pair{
                        std::tuple{std::make_unique<int>(2), std::make_unique<int>(3)},
                        std::move(state)};
                });
        });
    auto action = std::move(source).store(
        [](auto const & first, auto const & second, std::unique_ptr<int> state)
        {
            *state += *first + *second;
            return state;
        });
    auto result = std::move(action).build(std::make_unique<int>(4)).sync_wait();
    CHECK_EQ(*std::get<0>(result.first), 2);
    CHECK_EQ(*std::get<1>(result.first), 3);
    CHECK_EQ(*result.second, 9);
}

TEST_CASE("StateIO store validates the selected spread signature")
{
    struct InvalidDirect
    {
        double operator()(std::tuple<int, int> const &, int) const;
        int operator()(int, int, int) const;
    };
    using source_type = decltype(fnfelt::monad::state_io::just<int>(std::tuple{2, 3}));
    using traits_type = fnfelt::monad::state_io::detail::StoreStateIOTraits<>;
    auto wrong_state = [](int, int, int) { return 1.0; };
    auto mutable_values = [](int &, int &, int state) { return state; };
    auto wrong_arguments = [](char const *, int) { return 0; };
    static_assert(
        traits_type::validate_store(^^source_type, ^^decltype(wrong_state)) ==
        "mutator must return the same state type by value");
    static_assert(!traits_type::validate_store(^^source_type, ^^decltype(mutable_values)).empty());
    static_assert(!traits_type::validate_store(^^source_type, ^^decltype(wrong_arguments)).empty());
    static_assert(
        traits_type::validate_store(^^source_type, ^^InvalidDirect) ==
        "mutator must return the same state type by value");
}

TEST_CASE("StateIO store preserves tuple reference element categories")
{
    int value = 2;
    auto action = fnfelt::monad::state_io::just<int>(std::tuple<int &>{value})
                      .store([](int & element, int state) { return state + element; });
    auto result = action.build(4).sync_wait();
    CHECK_EQ(&std::get<0>(result.first), &value);
    CHECK_EQ(result.second, 6);
}
#endif  // DOCTEST_CONFIG_DISABLE
