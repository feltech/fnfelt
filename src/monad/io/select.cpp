// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/select.hpp>

#include <doctest/doctest.h>

#include <meta>

#include <expected>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
namespace io = fnfelt::monad::io;

struct Double
{
    constexpr int operator()(int value) const
    {
        return value * 2;
    }
};

struct DirectFirst
{
    int operator()(std::pair<int, int> value) const
    {
        return value.first + value.second;
    }
    int operator()(int lhs, int rhs) const
    {
        return lhs * rhs;
    }
};

struct QualifiedFunction
{
    int operator()([[maybe_unused]] int value) &&
    {
        return -1;
    }
    int operator()(int value) const &&
    {
        return value * 2;
    }
};

struct MoveFunction
{
    std::unique_ptr<int> factor;
    auto operator()(std::unique_ptr<int> input) &&
    {
        return std::make_unique<int>(*input * *factor);
    }
};

struct TrackedChoice
{
    int * copies;
    int * moves;
    TrackedChoice(int & copy_count, int & move_count) : copies{&copy_count}, moves{&move_count} {}
    TrackedChoice(TrackedChoice const & other) : copies{other.copies}, moves{other.moves}
    {
        ++*copies;
    }
    TrackedChoice(TrackedChoice && other) noexcept : copies{other.copies}, moves{other.moves}
    {
        ++*moves;
    }
    std::expected<int, int> operator()() const
    {
        return std::unexpected{21};
    }
};

struct TrackedFallback
{
    int * copies;
    int * moves;
    TrackedFallback(int & copy_count, int & move_count) : copies{&copy_count}, moves{&move_count} {}
    TrackedFallback(TrackedFallback const & other) : copies{other.copies}, moves{other.moves}
    {
        ++*copies;
    }
    TrackedFallback(TrackedFallback && other) noexcept : copies{other.copies}, moves{other.moves}
    {
        ++*moves;
    }
    Double operator()() const
    {
        return {};
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

inline constexpr auto async_move_choice = [](auto, std::unique_ptr<int> input)
    -> lf::task<std::expected<std::unique_ptr<int>, std::unique_ptr<int>>>
{ co_return std::unexpected{std::move(input)}; };

inline constexpr auto async_move_fallback =
    [](auto, std::unique_ptr<int> factor) -> lf::task<MoveFunction>
{ co_return MoveFunction{std::move(factor)}; };

inline constexpr auto async_failed_choice = [](auto) -> lf::task<std::expected<int, int>>
{
    throw std::runtime_error("async choice failure");
    co_return 0;
};

inline constexpr auto async_failed_fallback = [](auto) -> lf::task<Double>
{
    throw std::runtime_error("async fallback failure");
    co_return Double{};
};

struct RejectSelect : io::detail::CreateIOTraits<>
{
    static consteval std::string_view validate_select(std::meta::info, std::meta::info)
    {
        return "rejected selective composition";
    }
};

using expected_type = std::expected<int, int>;
constexpr auto choice_action = []() -> expected_type { return std::unexpected{21}; };
constexpr auto fallback_action = [] { return Double{}; };
using choice_type = decltype(io::create(choice_action));
using fallback_type = decltype(io::create(fallback_action));
using select_traits = io::detail::SelectIOTraits<>;

static_assert(select_traits::validate_select(^^choice_type, ^^fallback_type).empty());
using nested_choice = std::expected<choice_type, int>;
static_assert(
    io::detail::validate_select_values(^^nested_choice, ^^Double) ==
    "choice expected must contain a plain value");
static_assert(
    select_traits::validate_select(^^int, ^^fallback_type) == "choice source is not an IO");
static_assert(
    select_traits::validate_select(^^choice_type, ^^int) == "fallback source is not an IO");
static_assert(
    io::detail::validate_select_values(^^int, ^^Double) ==
    "choice must produce a std::expected by value");
static_assert(
    io::detail::validate_select_values(^^std::expected<void, int>, ^^Double) ==
    "choice expected must contain a non-void value");
static_assert(
    io::detail::validate_select_values(^^std::expected<int, int>, ^^int) ==
    "fallback function does not accept the choice's error");
static_assert(
    io::detail::validate_select_values(
        ^^std::expected<int, int>, ^^decltype([](int) { return 1L; })) ==
    "fallback function must return the choice's value type by value");
static_assert(io::detail::validate_select_values(
                  ^^std::expected<int, int>, ^^decltype([](int) -> int & { static int value; return value; })) ==
              "fallback function must return the choice's value type by value");

constexpr auto constexpr_select =
    io::select(io::create(choice_action), io::create(fallback_action));
static_assert(constexpr_select.build().sync_wait() == 42);
using namespace fnfelt::literals;  // NOLINT
static_assert(
    io::create(choice_action)
        .select<"Named select"_ss>(io::create(fallback_action))
        .build()
        .sync_wait() == 42);
static_assert(
    io::select<io::detail::SelectIOTraits<"Custom select"_ss>>(
        io::create(choice_action), io::create(fallback_action))
        .build()
        .sync_wait() == 42);
}  // namespace

TEST_CASE("IO select skips fallback execution and invocation on a completed choice")
{
    int fallback_calls = 0;
    int function_calls = 0;
    auto selected = io::create([]() -> std::expected<int, int> { return 42; })
                        .select(
                            io::create(
                                [&]
                                {
                                    ++fallback_calls;
                                    return [&](int value)
                                    {
                                        ++function_calls;
                                        return value * 2;
                                    };
                                }));
    CHECK_EQ(fallback_calls, 0);
    CHECK_EQ(selected.build().sync_wait(), 42);
    CHECK_EQ(selected.build().sync_wait(), 42);
    CHECK_EQ(fallback_calls, 0);
    CHECK_EQ(function_calls, 0);
}

TEST_CASE("IO select runs fallback after an unexpected choice and replays")
{
    int stage = 0;
    auto selected = io::select(
        io::create(
            [&]() -> std::expected<int, int>
            {
                CHECK_EQ(stage, 0);
                stage = 1;
                return std::unexpected{21};
            }),
        io::create(
            [&]
            {
                CHECK_EQ(stage, 1);
                stage = 2;
                return [&](int value)
                {
                    CHECK_EQ(stage, 2);
                    stage = 3;
                    return value * 2;
                };
            }));
    CHECK_EQ(selected.build().sync_wait(), 42);
    CHECK_EQ(stage, 3);
    stage = 0;
    CHECK_EQ(std::as_const(selected).build().sync_wait(), 42);
}

TEST_CASE("IO select forwards each operand independently")
{
    int choice_copies = 0;
    int choice_moves = 0;
    int fallback_copies = 0;
    int fallback_moves = 0;
    auto choice = io::create(TrackedChoice{choice_copies, choice_moves});
    auto fallback = io::create(TrackedFallback{fallback_copies, fallback_moves});
    choice_moves = 0;
    fallback_moves = 0;
    auto selected = io::select(choice, std::move(fallback));
    CHECK_EQ(choice_copies, 1);
    // The copied choice subsequently moves with the aggregate action into the result IO.
    CHECK_EQ(choice_moves, 1);
    CHECK_EQ(fallback_copies, 0);
    CHECK_GT(fallback_moves, 0);
    CHECK_EQ(selected.build().sync_wait(), 42);
}

TEST_CASE("IO select dispatches pair and tuple errors with direct invocation first")
{
    auto pair_choice = io::create(
        []() -> std::expected<int, std::pair<int, int>>
        { return std::unexpected{std::pair{20, 22}}; });
    CHECK_EQ(
        io::select(pair_choice, io::create([] { return DirectFirst{}; })).build().sync_wait(), 42);
    auto tuple_choice = io::create(
        []() -> std::expected<int, std::tuple<int, int>>
        { return std::unexpected{std::tuple{20, 22}}; });
    CHECK_EQ(
        io::select(
            tuple_choice, io::create([] { return [](int lhs, int rhs) { return lhs + rhs; }; }))
            .build()
            .sync_wait(),
        42);
}

TEST_CASE("IO select moves owned actions payloads and produced functions")
{
    auto choice = io::create(
        [owned = std::make_unique<int>(
             21)]() -> std::expected<std::unique_ptr<int>, std::unique_ptr<int>>
        { return std::unexpected{std::make_unique<int>(*owned)}; });
    auto fallback = io::create([factor = std::make_unique<int>(2)]
                               { return MoveFunction{std::make_unique<int>(*factor)}; });
    auto selected = io::select(std::move(choice), std::move(fallback));
    CHECK_EQ(*std::move(selected).build().sync_wait(), 42);
    auto hit = io::create(
                   []() -> std::expected<std::unique_ptr<int>, std::unique_ptr<int>>
                   { return std::make_unique<int>(42); })
                   .select(io::create([] { return MoveFunction{std::make_unique<int>(2)}; }));
    CHECK_EQ(*std::move(hit).build().sync_wait(), 42);
}

TEST_CASE("IO select short circuits and completes all sync async combinations")
{
    for (bool const is_hit : {false, true})
    {
        int calls = 0;
        auto sync_choice = io::create(
            [=]() -> std::expected<int, int>
            {
                if (is_hit)
                {
                    return 42;
                }
                return std::unexpected{21};
            });
        auto sync_fallback = io::create(
            [&]
            {
                ++calls;
                return Double{};
            });
        CHECK_EQ(io::select(sync_choice, sync_fallback).build().sync_wait(), 42);
        CHECK_EQ(calls, is_hit ? 0 : 1);
        calls = 0;
        auto mixed_choice = io::select(io::create_async(async_choice, is_hit), sync_fallback);
        CHECK_EQ(mixed_choice.build().sync_wait(lf::lazy_pool{2}), 42);
        CHECK_EQ(calls, is_hit ? 0 : 1);
        calls = 0;
        auto mixed_fallback = io::select(sync_choice, io::create_async(async_fallback, &calls));
        CHECK_EQ(mixed_fallback.build().sync_wait(lf::lazy_pool{2}), 42);
        CHECK_EQ(calls, is_hit ? 0 : 1);
        calls = 0;
        auto both = io::select(
            io::create_async(async_choice, is_hit), io::create_async(async_fallback, &calls));
        CHECK_EQ(both.build().sync_wait(lf::lazy_pool{2}), 42);
        CHECK_EQ(calls, is_hit ? 0 : 1);
    }
}

TEST_CASE("IO select propagates exceptions and skips fallback after choice failure")
{
    int calls = 0;
    auto failed_choice =
        io::create([]() -> std::expected<int, int> { throw std::runtime_error("choice failure"); });
    auto selected = io::select(
        failed_choice,
        io::create(
            [&]
            {
                ++calls;
                return Double{};
            }));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(selected.build().sync_wait()), "choice failure", std::runtime_error);
    CHECK_EQ(calls, 0);
    auto failed_fallback = io::select(
        io::create(choice_action),
        io::create([]() -> Double { throw std::runtime_error("fallback failure"); }));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(failed_fallback.build().sync_wait()),
        "fallback failure",
        std::runtime_error);
    auto failed_function = io::select(
        io::create(choice_action),
        io::create([]
                   { return [](int) -> int { throw std::runtime_error("function failure"); }; }));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(failed_function.build().sync_wait()),
        "function failure",
        std::runtime_error);
}

TEST_CASE("IO select owns move-only asynchronous operands and their produced values")
{
    auto selected = io::select(
        io::create_async(async_move_choice, std::make_unique<int>(21)),
        io::create_async(async_move_fallback, std::make_unique<int>(2)));
    CHECK_EQ(*std::move(selected).build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("IO select preserves the produced function's const qualification")
{
    auto fallback = io::create([]() -> QualifiedFunction const { return {}; });
    auto sync_selected = io::select(io::create(choice_action), fallback);
    CHECK_EQ(sync_selected.build().sync_wait(), 42);
    auto async_selected = io::select(io::create_async(async_choice, false), fallback);
    CHECK_EQ(async_selected.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("IO select propagates asynchronous exceptions and skips later effects")
{
    int calls = 0;
    auto selected = io::select(
        io::create_async(async_failed_choice),
        io::create(
            [&]
            {
                ++calls;
                return Double{};
            }));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(selected.build().sync_wait(lf::lazy_pool{2})),
        "async choice failure",
        std::runtime_error);
    CHECK_EQ(calls, 0);
    auto missed = io::select(io::create(choice_action), io::create_async(async_failed_fallback));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(missed.build().sync_wait(lf::lazy_pool{2})),
        "async fallback failure",
        std::runtime_error);
    auto failed_function = io::select(
        io::create_async(async_choice, false),
        io::create(
            [] { return [](int) -> int { throw std::runtime_error("async function failure"); }; }));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(failed_function.build().sync_wait(lf::lazy_pool{2})),
        "async function failure",
        std::runtime_error);
}

#endif  // DOCTEST_CONFIG_DISABLE
