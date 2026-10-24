// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/ReaderIO.hpp>

#include <doctest/doctest.h>

#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <libfork/schedule/lazy_pool.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
struct StateA
{
    int value = 0;
};

struct StateB
{
    int value = 0;
};

struct StateRefAction
{
    constexpr auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

auto state_action(int const & state)
{
    return fnfelt::monad::io::just(state);
}

constexpr auto generic_action = [](auto const & state)
{ return fnfelt::monad::io::just(state.value); };

struct DoubleAction
{
    constexpr auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state * 2);
    }
};

/// Move-only action - buildable only from a non-const rvalue ReaderIO.
struct MoveOnlyAction  // NOLINT(*-special-member-functions)
{
    MoveOnlyAction() = default;
    MoveOnlyAction(MoveOnlyAction &&) = default;
    MoveOnlyAction & operator=(MoveOnlyAction &&) = delete;
    constexpr auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

/// Action counting how it is stored.
struct CopyCountingAction
{
    static inline int copies = 0;
    static inline int moves = 0;

    CopyCountingAction() = default;
    CopyCountingAction(CopyCountingAction const & /*other*/)
    {
        ++copies;
    }
    CopyCountingAction(CopyCountingAction && /*other*/)
    {
        ++moves;
    }
    auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

/// Stateless coroutine leaf doubling its stored value.
constexpr auto echo_double = [](auto /*self*/, int value) -> lf::task<int> { co_return value * 2; };

/// Action materialising an asynchronously executing leaf IO.
struct AsyncAction
{
    auto operator()(int const & state) const
    {
        return fnfelt::monad::io::create_async(echo_double, state);
    }
};

/// Constant-evaluated build + proxy consumption pipeline.
consteval int constexpr_pipeline()
{
    fnfelt::monad::reader_io::ReaderIO<int, DoubleAction> const reader{DoubleAction{}};
    return reader.build(21).sync_wait();
}
struct BuildOnlyTraits
{
    static consteval std::string_view validate_build(std::meta::info)
    {
        return {};
    }
    static constexpr std::string_view name = "BuildOnly";
};
}  // namespace

TEST_CASE("ReaderRunProxy consumes the action with the bound state")
{
    fnfelt::monad::reader_io::ReaderIO<int, StateRefAction> reader{StateRefAction{}};
    CHECK_EQ(reader.build(5).sync_wait(), 5);
    // The state type is part of the ReaderIO: the same reader rebinds a new state per build.
    CHECK_EQ(reader.build(6).sync_wait(), 6);
}

TEST_CASE("ReaderIO accepts a function pointer action")
{
    using fn_action_t = decltype(&state_action);
    fnfelt::monad::reader_io::ReaderIO<int, fn_action_t> reader{&state_action};
    CHECK_EQ(reader.build(3).sync_wait(), 3);
}

TEST_CASE("the state type is part of the ReaderIO type: one action, many ReaderIOs")
{
    auto const reader_a =
        fnfelt::monad::reader_io::ReaderIO<StateA, decltype(generic_action)>{generic_action};
    auto const reader_b =
        fnfelt::monad::reader_io::ReaderIO<StateB, decltype(generic_action)>{generic_action};
    // Same action type, distinct ReaderIO types (and distinct value types per state).
    static_assert(std::is_same_v<decltype(reader_a)::action_type, decltype(reader_b)::action_type>);
    static_assert(!std::is_same_v<decltype(reader_a), decltype(reader_b)>);
    CHECK_EQ(reader_a.build(StateA{10}).sync_wait(), 10);
    CHECK_EQ(reader_b.build(StateB{20}).sync_wait(), 20);
}

TEST_CASE("value_type names the action's IO value")
{
    using reader_type = fnfelt::monad::reader_io::ReaderIO<int, StateRefAction>;
    static_assert(std::is_same_v<reader_type::value_type, int>);
    using async_reader_type = fnfelt::monad::reader_io::ReaderIO<int, AsyncAction>;
    // The async leaf's lf::task value.
    static_assert(std::is_same_v<async_reader_type::value_type, int>);
}

TEST_CASE("proxy io() materialises the inner IO for composition")
{
    fnfelt::monad::reader_io::ReaderIO<int, StateRefAction> reader{StateRefAction{}};
    auto inner_io = reader.build(6).io();
    using inner_io_t = decltype(inner_io);
    static_assert(std::is_same_v<inner_io_t, decltype(fnfelt::monad::io::just(0))>);
    int const ran = std::move(inner_io).build().sync_wait();
    CHECK_EQ(ran, 6);
}

TEST_CASE("build copies from an lvalue ReaderIO and moves from an rvalue; replays work")
{
    CopyCountingAction::copies = 0;
    CopyCountingAction::moves = 0;
    fnfelt::monad::reader_io::ReaderIO<int, CopyCountingAction> reader{CopyCountingAction{}};
    CHECK_EQ(CopyCountingAction::copies, 0);
    CHECK_EQ(CopyCountingAction::moves, 1);

    auto proxy_from_lvalue = reader.build(1);
    CHECK_EQ(CopyCountingAction::copies, 1);
    CHECK_EQ(CopyCountingAction::moves, 1);

    auto proxy_from_rvalue = std::move(reader).build(2);
    CHECK_EQ(CopyCountingAction::copies, 1);
    CHECK_GT(CopyCountingAction::moves, 0);

    CHECK_EQ(std::move(proxy_from_lvalue).sync_wait(), 1);
    CHECK_EQ(std::move(proxy_from_rvalue).sync_wait(), 2);
}

TEST_CASE("sync and async actions run through the same proxy")
{
    fnfelt::monad::reader_io::ReaderIO<int, DoubleAction> sync_reader{DoubleAction{}};
    CHECK_EQ(sync_reader.build(21).sync_wait(), 42);
    fnfelt::monad::reader_io::ReaderIO<int, AsyncAction> async_reader{AsyncAction{}};
    CHECK_EQ(async_reader.build(21).sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ReaderIO pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline() == 42);
}

TEST_CASE("ReaderIO build traits validate exact action receiver categories")
{
    using traits_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;
    static_assert(traits_type::validate_build(^^StateRefAction &).empty());
    static_assert(traits_type::validate_build(^^StateRefAction const &).empty());
    static_assert(traits_type::validate_build(^^StateRefAction &&).empty());
    static_assert(traits_type::validate_build(^^StateRefAction const &&).empty());
    static_assert(traits_type::validate_build(^^MoveOnlyAction &&).empty());
    static_assert(
        traits_type::validate_build(^^MoveOnlyAction &) ==
        "stored action cannot be copied or moved from the build receiver");
    static_assert(
        traits_type::validate_build(^^MoveOnlyAction const &) ==
        "stored action cannot be copied or moved from the build receiver");
    static_assert(
        traits_type::validate_build(^^MoveOnlyAction const &&) ==
        "stored action cannot be copied or moved from the build receiver");
}

TEST_CASE("ReaderIO injected build traits retain the source proxy traits")
{
    fnfelt::monad::reader_io::ReaderIO<int, StateRefAction> const source{StateRefAction{}};
    auto bound = source.build<BuildOnlyTraits>(42);
    static_assert(std::is_same_v<
                  decltype(bound),
                  fnfelt::monad::reader_io::ReaderRunProxy<
                      int,
                      StateRefAction,
                      fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>>>);
    CHECK_EQ(std::move(bound).sync_wait(), 42);
    CHECK_EQ(std::move(source).build(42).sync_wait(), 42);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
