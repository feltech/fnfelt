// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/traverse.hpp>

#include <doctest/doctest.h>

#include <string>
#include <tuple>
#include <utility>

#include <immer/array.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/static_string.hpp>

TEST_CASE("StateIO traversal maps values then threads updated state")
{
    constexpr auto action = fnfelt::monad::state_io::traverse<int>(
        std::tuple{2, 3},
        [](int value)
        {
            return fnfelt::monad::state_io::create<int>(
                [value](int state)
                { return fnfelt::monad::io::just(std::pair{value + state, state + 1}); });
        });
    static_assert(action.build(4).sync_wait() == std::pair{std::tuple{6, 8}, 6});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{std::tuple{6, 8}, 6}));
}
TEST_CASE("StateIO array traversal maps eagerly and threads state in input order")
{
    namespace state_io = fnfelt::monad::state_io;
    using fnfelt::literals::operator""_ss;
    int mapped = 0;
    auto action = state_io::traverse<"array state"_ss, std::string>(
        immer::array<int>{2, 3},
        [&mapped](int value)
        {
            ++mapped;
            return state_io::create<std::string>(
                [value = std::to_string(value)](std::string state)
                {
                    auto next = state + value;
                    return fnfelt::monad::io::just(std::pair{state, std::move(next)});
                });
        });
    CHECK_EQ(mapped, 2);
    CHECK_EQ(
        action.build("x").sync_wait(),
        (std::pair{immer::array<std::string>{"x", "x2"}, std::string{"x23"}}));
    CHECK_EQ(
        action.build("y").sync_wait(),
        (std::pair{immer::array<std::string>{"y", "y2"}, std::string{"y23"}}));
    CHECK_EQ(mapped, 2);
    auto empty = state_io::traverse<int>(
        immer::array<int>{},
        [](int value)
        {
            return state_io::create<int>(
                [value = std::to_string(value)](int state)
                { return fnfelt::monad::io::just(std::pair{value, state}); });
        });
    auto result = empty.build(42).sync_wait();
    CHECK(result.first.empty());
    CHECK_EQ(result.second, 42);
}

TEST_CASE("StateIO array traversal awaits async results before the next state")
{
    namespace state_io = fnfelt::monad::state_io;
    namespace io = fnfelt::monad::io;
    constexpr auto leaf = [](auto, int value, int state) -> lf::task<std::pair<int, int>>
    { co_return std::pair{state, state * 10 + value}; };
    auto action = state_io::traverse<int>(
        immer::array<int>{2, 3},
        [leaf](int value)
        {
            return state_io::create<int>(
                [leaf, value = std::to_string(value)](int state)
                { return io::create_async(leaf, std::stoi(value), state); });
        });
    CHECK_EQ(action.build(1).sync_wait(), (std::pair{immer::array<int>{1, 12}, 123}));
}
#endif  // DOCTEST_CONFIG_DISABLE
