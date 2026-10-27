// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/select.hpp>

#include <doctest/doctest.h>

#include <meta>

#include <expected>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/lift.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
namespace io = fnfelt::monad::io;
namespace reader_io = fnfelt::monad::reader_io;

struct Double
{
    constexpr int operator()(int value) const
    {
        return value * 2;
    }
};

struct MoveFunction
{
    std::unique_ptr<int> factor;
    std::unique_ptr<int> operator()(std::unique_ptr<int> value) &&
    {
        return std::make_unique<int>(*factor * *value);
    }
};

inline constexpr auto async_choice = [](auto, bool is_hit) -> lf::task<std::expected<int, int>>
{
    if (is_hit)
    {
        co_return 42;
    }
    co_return std::unexpected{21};
};
inline constexpr auto async_fallback = [](auto, int * calls) -> lf::task<Double>
{
    ++*calls;
    co_return Double{};
};

constexpr auto choice = reader_io::create<int>(
    [](int state) { return io::just(std::expected<int, int>{std::unexpected{state}}); });
constexpr auto fallback = reader_io::create<int>([](int) { return io::just(Double{}); });
using choice_type = decltype(choice);
using fallback_type = decltype(fallback);
using traits_type = reader_io::detail::SelectReaderIOTraits<>;
static_assert(traits_type::validate_select(^^choice_type, ^^fallback_type).empty());
static_assert(
    traits_type::validate_select(^^int, ^^fallback_type) == "choice source is not a ReaderIO");
static_assert(
    traits_type::validate_select(^^choice_type, ^^int) == "fallback source is not a ReaderIO");
static_assert(reader_io::select(choice, fallback).build(21).sync_wait() == 42);
using move_state = std::unique_ptr<int>;
using move_choice_type = decltype(reader_io::create<move_state>(
    [](move_state state) { return io::just(std::expected<int, int>{*state}); }));
using move_fallback_type =
    decltype(reader_io::create<move_state>([](move_state) { return io::just(Double{}); }));
static_assert(
    traits_type::validate_select(^^move_choice_type, ^^move_fallback_type) ==
    "state must be copy constructible");
using namespace fnfelt::literals;  // NOLINT
static_assert(choice.select<"Named select"_ss>(fallback).build(21).sync_wait() == 42);
static_assert(
    reader_io::select<reader_io::detail::SelectReaderIOTraits<"Custom select"_ss>>(choice, fallback)
        .build(21)
        .sync_wait() == 42);
}  // namespace

TEST_CASE("ReaderIO select skips fallback materialisation execution and invocation")
{
    int builds = 0;
    int runs = 0;
    int calls = 0;
    auto choice =
        reader_io::create<int>([](int state) { return io::just(std::expected<int, int>{state}); });
    auto fallback = reader_io::create<int>(
        [&]([[maybe_unused]] int state)
        {
            ++builds;
            return io::create(
                [&]
                {
                    ++runs;
                    return [&](int value)
                    {
                        ++calls;
                        return value;
                    };
                });
        });
    auto selected = choice.select(fallback);
    auto bound = selected.build(42);
    CHECK_EQ(builds, 0);
    CHECK_EQ(std::move(bound).sync_wait(), 42);
    CHECK_EQ(std::as_const(selected).build(42).sync_wait(), 42);
    CHECK_EQ(builds, 0);
    CHECK_EQ(runs, 0);
    CHECK_EQ(calls, 0);
}

TEST_CASE("ReaderIO select preserves the original environment for its fallback")
{
    auto choice = reader_io::create<int>(
        [](int state)
        {
            state += 1;
            return io::just(std::expected<int, int>{std::unexpected{state}});
        });
    auto fallback = reader_io::create<int>(
        [](int state) { return io::just([state](int input) { return state + input; }); });
    auto selected = reader_io::select(choice, fallback);
    CHECK_EQ(selected.build(20).sync_wait(), 41);
    CHECK_EQ(selected.build(20).sync_wait(), 41);
    using other_state = decltype(reader_io::create<bool>([](bool) { return io::just(Double{}); }));
    static_assert(
        traits_type::validate_select(^^choice_type, ^^other_state) ==
        "choice and fallback must have the same state type");
}

TEST_CASE("ReaderIO select supports move-only actions payloads and functions")
{
    auto choice = reader_io::create<int>(
        [owned = std::make_unique<int>(21)]([[maybe_unused]] int state)
        {
            return io::create(
                [value = *owned]() -> std::expected<std::unique_ptr<int>, std::unique_ptr<int>>
                { return std::unexpected{std::make_unique<int>(value)}; });
        });
    auto fallback = reader_io::create<int>(
        [factor = std::make_unique<int>(2)]([[maybe_unused]] int state)
        {
            return io::create([value = *factor]
                              { return MoveFunction{std::make_unique<int>(value)}; });
        });
    auto selected = reader_io::select(std::move(choice), std::move(fallback));
    CHECK_EQ(*std::move(selected).build(0).sync_wait(), 42);
}

TEST_CASE("ReaderIO select handles all sync async combinations and skips fallback builds")
{
    for (bool const is_hit : {false, true})
    {
        int builds = 0;
        int runs = 0;
        auto sync_choice = reader_io::create<int>(
            [=]([[maybe_unused]] int state)
            {
                return io::create(
                    [=]() -> std::expected<int, int>
                    {
                        if (is_hit)
                        {
                            return 42;
                        }
                        return std::unexpected{21};
                    });
            });
        auto async_choice_reader = reader_io::lift<int>(io::create_async(async_choice, is_hit));
        auto sync_fallback = reader_io::create<int>(
            [&]([[maybe_unused]] int state)
            {
                ++builds;
                return io::create(
                    [&]
                    {
                        ++runs;
                        return Double{};
                    });
            });
        auto async_fallback_reader = reader_io::create<int>(
            [&]([[maybe_unused]] int state)
            {
                ++builds;
                return io::create_async(async_fallback, &runs);
            });
        CHECK_EQ(reader_io::select(sync_choice, sync_fallback).build(0).sync_wait(), 42);
        CHECK_EQ(builds, is_hit ? 0 : 1);
        CHECK_EQ(runs, is_hit ? 0 : 1);
        builds = 0;
        runs = 0;
        CHECK_EQ(
            reader_io::select(async_choice_reader, sync_fallback)
                .build(0)
                .sync_wait(lf::lazy_pool{2}),
            42);
        CHECK_EQ(builds, is_hit ? 0 : 1);
        CHECK_EQ(runs, is_hit ? 0 : 1);
        builds = 0;
        runs = 0;
        CHECK_EQ(
            reader_io::select(sync_choice, async_fallback_reader)
                .build(0)
                .sync_wait(lf::lazy_pool{2}),
            42);
        CHECK_EQ(builds, is_hit ? 0 : 1);
        CHECK_EQ(runs, is_hit ? 0 : 1);
        builds = 0;
        runs = 0;
        CHECK_EQ(
            reader_io::select(async_choice_reader, async_fallback_reader)
                .build(0)
                .sync_wait(lf::lazy_pool{2}),
            42);
        CHECK_EQ(builds, is_hit ? 0 : 1);
        CHECK_EQ(runs, is_hit ? 0 : 1);
    }
}

TEST_CASE("ReaderIO select preserves a nonassignable fallback callable from composed IO")
{
    auto fallback = reader_io::create<int>(
        [](int state)
        {
            return io::sequence(io::just(state))
                .transform([](int factor)
                           { return [factor](int value) { return factor * value; }; });
        });
    using callable_type = decltype(fallback)::value_type;
    static_assert(!std::is_move_assignable_v<callable_type>);
    auto choice = reader_io::lift<int>(io::just(std::expected<int, int>{std::unexpected{21}}));
    auto selected = reader_io::select(choice, fallback);
    CHECK_EQ(selected.build(2).sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ReaderIO select propagates materialisation failures without evaluating fallback")
{
    int builds = 0;
    auto choice = reader_io::create<int>(
        [](int) -> decltype(io::just(std::expected<int, int>{1}))
        { throw std::runtime_error("choice build failure"); });
    auto fallback = reader_io::create<int>(
        [&]([[maybe_unused]] int state)
        {
            ++builds;
            return io::just(Double{});
        });
    auto selected = reader_io::select(choice, fallback);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(selected.build(0).sync_wait()),
        "choice build failure",
        std::runtime_error);
    CHECK_EQ(builds, 0);
    auto failing_fallback = reader_io::create<int>(
        [](int) -> decltype(io::just(Double{}))
        { throw std::runtime_error("fallback build failure"); });
    auto missed = reader_io::select(::choice, failing_fallback);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(missed.build(0).sync_wait()),
        "fallback build failure",
        std::runtime_error);
}

#endif  // DOCTEST_CONFIG_DISABLE
