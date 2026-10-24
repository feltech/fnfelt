// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/lift.hpp>

#include <doctest/doctest.h>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/detail/lift.hpp>
#include <fnfelt/static_string.hpp>
#include <libfork/schedule/lazy_pool.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// State that cannot be copied (only moved) - lifts ignore the state entirely.
struct NonCopyableState  // NOLINT(*-special-member-functions)
{
    NonCopyableState() = default;
    NonCopyableState(NonCopyableState &&) = default;
    NonCopyableState & operator=(NonCopyableState &&) = delete;
};

/// Movable-only IO action - its IO is move-only, so lifts must source it from an rvalue and the
/// ReaderIO must build from a non-const rvalue.
struct MoveOnlyIoAction  // NOLINT(*-special-member-functions)
{
    MoveOnlyIoAction() = default;
    MoveOnlyIoAction(MoveOnlyIoAction &&) = default;
    MoveOnlyIoAction & operator=(MoveOnlyIoAction &&) = delete;
    auto operator()() const
    {
        return 11;
    }
};

/// Action counting its runs through a static counter.
struct CountingIoAction
{
    int operator()() const
    {
        return ++runs;
    }

    static inline int runs = 0;
};

/// Stateless coroutine leaf doubling its stored value.
constexpr auto async_double = [](auto /*self*/, int value) -> lf::task<int>
{ co_return value * 2; };

inline constexpr char custom_reader_io_name[] = "CustomReader";
using CustomLiftReaderIOTraits =
    fnfelt::monad::reader_io::detail::LiftReaderIOTraits<custom_reader_io_name>;

/// Constant-evaluated pipeline: lift a just IO, build with a state, run.
consteval int constexpr_pipeline()
{
    constexpr auto embedded = fnfelt::monad::io::just(7);
    constexpr auto reader = fnfelt::monad::reader_io::lift<int>(embedded);
    return reader.build(123).sync_wait();
}
}  // namespace

TEST_CASE("lift embeds an IO that is yielded for any state")
{
    auto embedded = fnfelt::monad::io::just(5);
    auto reader = fnfelt::monad::reader_io::lift<int>(embedded);

    // The state is ignored entirely - even a non-copyable one - and every build yields the same
    // value.
    CHECK_EQ(reader.build(1).sync_wait(), 5);
    CHECK_EQ(reader.build(2).sync_wait(), 5);
    NonCopyableState state;
    auto move_state_reader = fnfelt::monad::reader_io::lift<NonCopyableState>(embedded);
    CHECK_EQ(std::move(move_state_reader).build(std::move(state)).sync_wait(), 5);
}

TEST_CASE("LiftReaderIOTraits validates the source identity")
{
    using traits_type = fnfelt::monad::reader_io::detail::LiftReaderIOTraits<>;
    using io_type = decltype(fnfelt::monad::io::just(1));
    using reader_type = decltype(fnfelt::monad::reader_io::lift<int>(fnfelt::monad::io::just(1)));

    static_assert(traits_type::validate_lift(^^io_type).empty());
    static_assert(traits_type::validate_lift(^^int) == "provided source is not an IO");
    static_assert(
        traits_type::validate_lift(^^reader_type) ==
        "provided source is already a ReaderIO, pass it directly");
}

TEST_CASE("lift preserves the embedded IO's type identity")
{
    auto embedded = fnfelt::monad::io::just(5);
    using embedded_t = decltype(embedded);
    auto reader = fnfelt::monad::reader_io::lift<int>(embedded);
    using action_t = decltype(reader)::action_type;
    // The action's IO is the embedded IO's type.
    static_assert(std::is_same_v<
                  fnfelt::monad::reader_io::ReaderIO<int, action_t>::value_type,
                  embedded_t::value_type>);
    static_assert(std::is_same_v<decltype(reader.build(1).io()), embedded_t>);
}

TEST_CASE("lift does not run the source IO until sync_wait")
{
    auto embedded = fnfelt::monad::io::create(CountingIoAction{});
    CHECK_EQ(CountingIoAction::runs, 0);

    auto reader = fnfelt::monad::reader_io::lift<int>(embedded);
    // Lifting copies the IO but never runs its action.
    CHECK_EQ(CountingIoAction::runs, 0);

    CHECK_EQ(reader.build(1).sync_wait(), 1);
    CHECK_EQ(CountingIoAction::runs, 1);

    // Replays run the embedded IO again (per-build copies of the action).
    CHECK_EQ(reader.build(2).sync_wait(), 2);
    CHECK_EQ(CountingIoAction::runs, 2);
}

TEST_CASE("lift preserves an async source IO")
{
    auto embedded = fnfelt::monad::io::create_async(async_double, 20);
    auto reader = fnfelt::monad::reader_io::lift<int>(embedded);

    // The state is ignored; the async leaf doubles its stored value.
    CHECK_EQ(reader.build(1).sync_wait(lf::lazy_pool{2}), 40);
}

TEST_CASE("lift accepts a move-only IO via an rvalue ReaderIO build")
{
    auto reader =
        fnfelt::monad::reader_io::lift<int>(fnfelt::monad::io::create(MoveOnlyIoAction{}));
    CHECK_EQ(std::move(reader).build(0).sync_wait(), 11);
}

TEST_CASE("lift takes a name and explicit traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto embedded = fnfelt::monad::io::just(5);

    auto named = fnfelt::monad::reader_io::lift<"MyReader"_ss, int>(embedded);
    static_assert(decltype(named)::traits::name == "MyReader");
    CHECK_EQ(named.build(1).sync_wait(), 5);

    auto custom = fnfelt::monad::reader_io::lift<int, CustomLiftReaderIOTraits>(embedded);
    static_assert(decltype(custom)::traits::name == "CustomReader");
    CHECK_EQ(custom.build(1).sync_wait(), 5);
}

TEST_CASE("lift pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline() == 7);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
