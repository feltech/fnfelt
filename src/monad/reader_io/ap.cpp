// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/ap.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/ap.hpp>
#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
struct State
{
    int value = 0;
};

/// Function source action producing a callable that adds a fixed addend.
struct AdderFnSourceAction
{
    constexpr auto operator()(State const & state) const
    {
        return fnfelt::monad::io::just([addend = state.value](int const & value)
                                       { return value + addend; });
    }
};

/// Value source action producing the state's value.
struct ValueSourceAction
{
    constexpr auto operator()(State const & state) const
    {
        return fnfelt::monad::io::just(state.value);
    }
};

/// Function source action mutating its own state copy; the value source must not see it.
struct MutatingFnSourceAction
{
    auto operator()(State state) const
    {
        state.value = 40;
        return fnfelt::monad::io::just([addend = state.value](int const & value)
                                       { return value + addend; });
    }
};

/// Movable callable produced by the async function source.
struct Adder
{
    int addend;
    int operator()(int value) const
    {
        return value + addend;
    }
};

/// Stateless coroutine entry producing an Adder of the state's value.
constexpr auto async_make_adder = [](auto /*self*/, int addend) -> lf::task<Adder>
{ co_return Adder{addend}; };

/// Stateless coroutine leaf echoing its stored value.
constexpr auto echo_fn = [](auto /*self*/, int value) -> lf::task<int> { co_return value; };

/// State counting its copies.
struct CountingState
{
    explicit CountingState(int value_in = 0) : value{value_in} {}
    CountingState(CountingState const & other) : value{other.value}
    {
        ++copies;
    }
    CountingState(CountingState &&) = default;

    static inline std::atomic<int> copies{0};
    int value = 0;
};

/// Counting-state function source action.
struct CountingFnSourceAction
{
    constexpr auto operator()(CountingState const & state) const
    {
        return fnfelt::monad::io::just([addend = state.value](int const & value)
                                       { return value + addend; });
    }
};

/// Constant-evaluated pipeline: apply the function source to the value source, bind, run.
consteval int constexpr_pipeline()
{
    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<State>(AdderFnSourceAction{}),
        fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    return applied.build(State{5}).sync_wait();
}
}  // namespace

TEST_CASE("ap applies the produced callable to the produced value")
{
    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<State>(AdderFnSourceAction{}),
        fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    // Both sources see the same state value (5): the produced callable adds it to the value's
    // value.
    CHECK_EQ(applied.build(State{5}).sync_wait(), 10);
    // The result keeps the sources' state type.
    static_assert(std::is_same_v<decltype(applied)::state_type, State>);
    static_assert(std::is_same_v<decltype(applied)::value_type, int>);
}

TEST_CASE("ApReaderIOTraits validates source contracts")
{
    using traits_type = fnfelt::monad::reader_io::detail::ApReaderIOTraits<>;
    using fn_type = decltype(fnfelt::monad::reader_io::create<State>(AdderFnSourceAction{}));
    using value_type = decltype(fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    using other_state_type = decltype(fnfelt::monad::reader_io::just<int>(1));
    using move_state_type = std::unique_ptr<int>;
    using move_reader_type = decltype(fnfelt::monad::reader_io::just<move_state_type>(1));

    static_assert(traits_type::validate_ap(^^fn_type, ^^value_type).empty());
    static_assert(
        traits_type::validate_ap(^^fn_type, ^^other_state_type) ==
        "function source and value source must have the same state type");
    static_assert(
        traits_type::validate_ap(^^value_type, ^^value_type) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
    static_assert(
        traits_type::validate_ap(^^int, ^^value_type) == "function source is not a ReaderIO");
    static_assert(traits_type::validate_ap(^^fn_type, ^^int) == "value source is not a ReaderIO");
    static_assert(
        traits_type::validate_ap(^^move_reader_type, ^^move_reader_type) ==
        "state must be copy constructible");
}

TEST_CASE("ap isolates the function source's state mutation from the value source")
{
    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<State>(MutatingFnSourceAction{}),
        fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    // The function source's own state copy carries 40; the value source sees the original 4.
    CHECK_EQ(applied.build(State{4}).sync_wait(), 44);
}

TEST_CASE("ap copies the state exactly once internally, on top of the build's bind")
{
    CountingState::copies = 0;
    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<CountingState>(CountingFnSourceAction{}),
        fnfelt::monad::reader_io::create<CountingState>(
            [](CountingState const & state) { return fnfelt::monad::io::just(state.value); }));
    CHECK_EQ(CountingState::copies.load(), 0);

    // LVALUE caller: the build's by-value bind (1) plus the function source's own copy (1).
    CountingState lvalue_state{7};
    CHECK_EQ(applied.build(lvalue_state).sync_wait(), 14);
    CHECK_EQ(CountingState::copies.load(), 2);

    // RVALUE caller: the bind moves, so exactly one internal copy remains.
    CountingState::copies = 0;
    CHECK_EQ(applied.build(CountingState{7}).sync_wait(), 14);
    CHECK_EQ(CountingState::copies.load(), 1);
}

TEST_CASE("ap composes async sources concurrently")
{
    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<State>(
            [](State const & state)
            { return fnfelt::monad::io::create_async(async_make_adder, state.value); }),
        fnfelt::monad::reader_io::create<State>(
            [](State const & state)
            { return fnfelt::monad::io::create_async(echo_fn, state.value); }));
    // The async fn source produces Adder{3}; the async value source produces 3; 3 + 3 = 6.
    CHECK_EQ(applied.build(State{3}).sync_wait(lf::lazy_pool{2}), 6);
}

TEST_CASE("ap replays from an lvalue ReaderIO")
{
    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<State>(AdderFnSourceAction{}),
        fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    // Replays copy the sources; both runs produce the same application.
    CHECK_EQ(applied.build(State{5}).sync_wait(), 10);
    CHECK_EQ(applied.build(State{6}).sync_wait(), 12);
}

TEST_CASE("ap takes a name and explicit traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto applied = fnfelt::monad::reader_io::ap(
        fnfelt::monad::reader_io::create<State>(AdderFnSourceAction{}),
        fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    static_assert(decltype(applied)::traits::name == "ReaderIO");
    // The inner application carries io's own ap traits, not the reader's.
    static_assert(decltype(applied.build(State{}).io())::traits::name == "IO");
    CHECK_EQ(applied.build(State{5}).sync_wait(), 10);

    auto named = fnfelt::monad::reader_io::ap<"MyAp"_ss>(
        fnfelt::monad::reader_io::create<State>(AdderFnSourceAction{}),
        fnfelt::monad::reader_io::create<State>(ValueSourceAction{}));
    static_assert(decltype(named)::traits::name == "MyAp");
    CHECK_EQ(named.build(State{5}).sync_wait(), 10);
}

TEST_CASE("ap pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline() == 10);
}

TEST_CASE("named ap composes lifted sources")
{
    using fnfelt::literals::operator""_ss;
    auto applied = fnfelt::monad::reader_io::ap<"lifted ap"_ss>(
        fnfelt::monad::reader_io::just<int>([](int value) { return value + 1; }),
        fnfelt::monad::reader_io::just<int>(3));
    CHECK_EQ(applied.build(0).sync_wait(), 4);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
