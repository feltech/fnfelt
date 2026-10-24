// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/lift.hpp>

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/state_io/and_then.hpp>
#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/repeat_until.hpp>
#include <fnfelt/monad/state_io/select.hpp>
#include <fnfelt/monad/state_io/sequence.hpp>
#include <fnfelt/monad/state_io/transform.hpp>

TEST_CASE("state_io::lift preserves state for IO and ReaderIO sources")
{
    auto lifted = fnfelt::monad::state_io::lift<int>(fnfelt::monad::io::just(4));
    CHECK_EQ(lifted.build(8).sync_wait(), (std::pair{4, 8}));
    auto reader = fnfelt::monad::reader_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(state * 2); });
    auto lifted_reader = fnfelt::monad::state_io::lift<int>(reader);
    CHECK_EQ(lifted_reader.build(8).sync_wait(), (std::pair{16, 8}));
    auto move_state =
        fnfelt::monad::state_io::lift<std::unique_ptr<int>>(fnfelt::monad::io::just(4));
    auto result = std::move(move_state).build(std::make_unique<int>(8)).sync_wait();
    CHECK_EQ(result.first, 4);
    CHECK_EQ(*result.second, 8);
    using traits_type = fnfelt::monad::state_io::detail::LiftStateIOTraits<>;
    static_assert(!traits_type::validate_lift(^^int, ^^int).empty());
    static_assert(!traits_type::validate_lift(^^decltype(reader), ^^bool).empty());
}

TEST_CASE("lifting a ReaderIO retains borrowed state through execution")
{
    auto reader = fnfelt::monad::reader_io::create<std::string>(
        [](std::string const & state)
        { return fnfelt::monad::io::create([&state] { return state.size(); }); });
    auto lifted = fnfelt::monad::state_io::lift<std::string>(reader);
    auto result = lifted.build(std::string(1000, 'x')).sync_wait();
    CHECK_EQ(result.first, 1000);
    CHECK_EQ(result.second.size(), 1000);
}

TEST_CASE("StateIO lift and chaining preserve constant evaluation of parallel IO compositions")
{
    namespace io = fnfelt::monad::io;
    namespace state_io = fnfelt::monad::state_io;
    constexpr auto summed = io::sequence(io::just(20), io::just(22))
                                .transform([](int first, int second) { return first + second; });
    constexpr auto source = io::ap(io::just([](int value) { return value * 2; }), summed);
    constexpr auto action = state_io::lift<int>(source).and_then(
        [](int value)
        {
            return state_io::create<int>([value](int state)
                                         { return io::just(std::pair{value + state, state + 1}); });
        });
    static_assert(action.build(3).sync_wait() == std::pair{87, 4});
    CHECK_EQ(action.build(3).sync_wait(lf::lazy_pool{2}), (std::pair{87, 4}));
}

TEST_CASE("StateIO preserves nonassignable state and step order around lifted parallel IO")
{
    namespace io = fnfelt::monad::io;
    namespace state_io = fnfelt::monad::state_io;
    struct State
    {
        int value;

        explicit State(int initial) : value{initial} {}
        State(State &&) = default;
        State & operator=(State &&) = delete;
    };
    static_assert(std::is_move_constructible_v<State>);
    static_assert(!std::is_move_assignable_v<State>);

    auto step = state_io::lift<State>(io::sequence(io::just(2), io::just(3)))
                    .and_then(
                        [](int first, int second)
                        {
                            return state_io::create<State>(
                                [first, second](State state)
                                {
                                    return io::create([value = first + second + state.value,
                                                       next = state.value + 1]
                                                      { return std::pair{value, State{next}}; });
                                });
                        });
    auto action = state_io::sequence<State>(step, step);
    auto result = std::move(action).build(State{4}).sync_wait(lf::lazy_pool{2});
    static_assert(!std::is_move_assignable_v<decltype(result)>);
    CHECK_EQ(result.first, (std::tuple{9, 10}));
    CHECK_EQ(result.second.value, 6);
}

TEST_CASE("StateIO selection and repetition constant evaluate lifted parallel IO")
{
    namespace io = fnfelt::monad::io;
    namespace state_io = fnfelt::monad::state_io;
    constexpr auto choice = state_io::lift<int>(io::sequence(io::just(1), io::just(2)))
                                .transform(
                                    [](int first, int second) -> std::expected<int, int>
                                    { return std::unexpected{first + second}; });
    constexpr auto fallback = state_io::create<int>(
        [](int state)
        { return io::just(std::pair{[state](int value) { return value + state; }, state + 1}); });
    constexpr auto action = choice.select(fallback).repeat_until(
        0,
        [](int accumulator, int value) -> std::expected<int, int>
        {
            if (accumulator == 1)
            {
                return value;
            }
            return std::unexpected{accumulator + 1};
        });
    static_assert(action.build(4).sync_wait() == std::pair{8, 6});
    CHECK_EQ(action.build(4).sync_wait(lf::lazy_pool{2}), (std::pair{8, 6}));
}
#endif  // DOCTEST_CONFIG_DISABLE
