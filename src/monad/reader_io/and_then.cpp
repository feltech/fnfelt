// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/and_then.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/and_then.hpp>
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

struct VoidIOAction
{
    fnfelt::monad::io::IO<void (*)()> operator()(int const &);
};

using void_reader_type = fnfelt::monad::reader_io::ReaderIO<int, VoidIOAction>;

struct VoidContinuation
{
    void_reader_type operator()(int const &) const;
};

/// Source action producing the state's value.
struct StateAction
{
    constexpr auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

/// Source action mutating its own state copy; the continuation must not see the mutation.
struct MutatingSourceAction
{
    auto operator()(State state) const
    {
        state.value = 99;
        return fnfelt::monad::io::just(state.value);
    }
};

/// Source action producing a two-element tuple of the state's value.
struct TupleSourceAction
{
    constexpr auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(std::tuple{state, state * 2});
    }
};

/// Source action materialising an async leaf doubling the state's value.
struct AsyncDoubleSourceAction
{
    constexpr auto operator()(int const & state) const
    {
        constexpr auto echo_double = [](auto /*self*/, int value) -> lf::task<int>
        { co_return value * 2; };
        return fnfelt::monad::io::create_async(echo_double, state);
    }
};

/// Continuation returning a ReaderIO that adds the state's value to the produced value.
struct AddStateContinuation
{
    constexpr auto operator()(int const & value) const
    {
        return fnfelt::monad::reader_io::create<int>(
            [value](int const & state) { return fnfelt::monad::io::just(value + state); });
    }
};

/// Continuation callable only as an rvalue.
struct RvalueOnlyContinuation
{
    constexpr auto operator()(int const & value) &&
    {
        return fnfelt::monad::reader_io::create<int>(
            [value](int const & state) { return fnfelt::monad::io::just(value + state); });
    }
};

/// Action counting its state copies and moves.
struct CountingState
{
    explicit CountingState(int value_in = 0) : value{value_in} {}
    CountingState(CountingState const & other) : value{other.value}
    {
        ++copies;
    }
    CountingState(CountingState && other) : value{other.value}
    {
        ++moves;
    }

    static inline int copies = 0;
    static inline int moves = 0;
    int value = 0;
};

/// Counting-state source action.
struct CountingSourceAction
{
    constexpr auto operator()(CountingState const & state) const
    {
        return fnfelt::monad::io::just(state.value);
    }
};

/// Counting-state continuation whose ReaderIO reads its own state copy.
struct CountingAddStateContinuation
{
    constexpr auto operator()(int const & value) const
    {
        return fnfelt::monad::reader_io::create<CountingState>(
            [value](CountingState const & state)
            { return fnfelt::monad::io::just(value + state.value); });
    }
};

/// Constant-evaluated pipeline: create, chain, bind a state, run.
consteval int constexpr_pipeline()
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<int>(StateAction{}), AddStateContinuation{});
    return chained.build(5).sync_wait();
}
}  // namespace

TEST_CASE("and_then feeds the produced value to the continuation over the same state")
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<int>(StateAction{}), AddStateContinuation{});
    // The source produces the state's value (5); the continuation adds the state's value again.
    CHECK_EQ(chained.build(5).sync_wait(), 10);
}

TEST_CASE("AndThenReaderIOTraits validates continuation contracts")
{
    using traits_type = fnfelt::monad::reader_io::detail::AndThenReaderIOTraits<>;
    using source_type = decltype(fnfelt::monad::reader_io::create<int>(StateAction{}));
    using different_state_type =
        decltype([](int) { return fnfelt::monad::reader_io::just<State>(1); });
    using wrong_argument_type =
        decltype([](State) { return fnfelt::monad::reader_io::just<int>(1); });
    using plain_result_type = decltype([](int) { return 1; });
    using move_state_type = std::unique_ptr<int>;
    using move_source_type = decltype(fnfelt::monad::reader_io::just<move_state_type>(1));

    static_assert(traits_type::validate_and_then(^^source_type, ^^AddStateContinuation).empty());
    static_assert(
        traits_type::validate_and_then(^^source_type, ^^different_state_type) ==
        "continuation function must return a ReaderIO with the same state type");
    static_assert(
        traits_type::validate_and_then(^^source_type, ^^wrong_argument_type) ==
        "continuation function does not accept the source ReaderIO's value");
    static_assert(
        traits_type::validate_and_then(^^source_type, ^^plain_result_type) ==
        "continuation function must return a ReaderIO");
    static_assert(
        traits_type::validate_and_then(^^source_type, ^^VoidContinuation) ==
        "continuation function must return a ReaderIO that produces a value");
    static_assert(
        traits_type::validate_and_then(^^move_source_type, ^^AddStateContinuation) ==
        "state must be copy constructible");
}

TEST_CASE("and_then isolates the source's state mutation from the continuation")
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<State>(MutatingSourceAction{}),
        [](int const & source_value)
        {
            return fnfelt::monad::reader_io::create<State>(
                [source_value](State const & state)
                { return fnfelt::monad::io::just(source_value + state.value); });
        });
    // The continuation's state copy is bound before the source runs, so it sees the original
    // value (5); the source's produced value (99) is fed to the continuation as well.
    State const bound_state{5};
    CHECK_EQ(chained.build(bound_state).sync_wait(), 104);
}

TEST_CASE("and_then copies the state exactly twice for an lvalue run and once for an rvalue run")
{
    CountingState::copies = 0;
    CountingState::moves = 0;
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<CountingState>(CountingSourceAction{}),
        CountingAddStateContinuation{});
    // Composition alone performs no state copies.
    CHECK_EQ(CountingState::copies, 0);

    // LVALUE caller: exactly two copies - one for the build's by-value bind, one for the source
    // run; the continuation's state is moved from the now-dead parameter.
    CountingState lvalue_state{7};
    CHECK_EQ(chained.build(lvalue_state).sync_wait(), 14);
    CHECK_EQ(CountingState::copies, 2);

    // RVALUE caller: exactly one copy (the source run's); the bound state is moved throughout.
    CountingState::copies = 0;
    CHECK_EQ(chained.build(CountingState{7}).sync_wait(), 14);
    CHECK_EQ(CountingState::copies, 1);
}

TEST_CASE("and_then accepts a continuation returning a named just ReaderIO")
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<int>(StateAction{}),
        [](int) { return fnfelt::monad::reader_io::just<int>(6); });
    // The continuation ignores the produced value: just yields 6 for any state.
    CHECK_EQ(chained.build(5).sync_wait(), 6);
}

TEST_CASE("and_then spreads a tuple source value into a two-argument continuation")
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<int>(TupleSourceAction{}),
        [](int const & first_value, int const & second_value)
        {
            return fnfelt::monad::reader_io::create<int>(
                [first_value, second_value](int const & state)
                { return fnfelt::monad::io::just(first_value + second_value + state); });
        });
    // The tuple {1, 2} is spread into the continuation and the state's value is added.
    CHECK_EQ(chained.build(1).sync_wait(), 4);
}

TEST_CASE("and_then composes async sources and async continuations")
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<int>(AsyncDoubleSourceAction{}),
        [](int const & doubled_value)
        {
            constexpr auto echo_double = [](auto /*self*/, int value) -> lf::task<int>
            { co_return value * 2; };
            return fnfelt::monad::reader_io::create<int>(
                [doubled_value](int const &)
                { return fnfelt::monad::io::create_async(echo_double, doubled_value); });
        });
    // The async source doubles the state (1 -> 2) and the async continuation doubles that (-> 4).
    CHECK_EQ(chained.build(1).sync_wait(lf::lazy_pool{2}), 4);
}

TEST_CASE("and_then keeps the reader traits and does not inherit them into the inner IO")
{
    using namespace fnfelt::literals;  // NOLINT

    auto source_reader = fnfelt::monad::reader_io::create<int>(StateAction{});

    auto default_chain = fnfelt::monad::reader_io::and_then(source_reader, AddStateContinuation{});
    static_assert(decltype(default_chain)::traits::name == "ReaderIO");
    static_assert(std::is_same_v<decltype(default_chain)::state_type, int>);
    static_assert(std::is_same_v<decltype(default_chain)::value_type, int>);
    // The inner composition carries io's own and_then traits, not the reader's.
    static_assert(decltype(default_chain.build(0).io())::traits::name == "IO");
    CHECK_EQ(default_chain.build(5).sync_wait(), 10);

    auto named_chain =
        fnfelt::monad::reader_io::and_then<"MyReader"_ss>(source_reader, AddStateContinuation{});
    static_assert(decltype(named_chain)::traits::name == "MyReader");
    CHECK_EQ(named_chain.build(5).sync_wait(), 10);
}

TEST_CASE("and_then accepts an rvalue-only continuation and replays from an lvalue")
{
    auto chained = fnfelt::monad::reader_io::and_then(
        fnfelt::monad::reader_io::create<int>(StateAction{}), RvalueOnlyContinuation{});
    // The continuation is invoked as an rvalue, matching the receiver contract.
    CHECK_EQ(chained.build(7).sync_wait(), 14);
    CHECK_EQ(chained.build(8).sync_wait(), 16);
}

TEST_CASE("and_then pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline() == 10);
}

TEST_CASE("named and_then composes a lifted source")
{
    using fnfelt::literals::operator""_ss;
    auto chained = fnfelt::monad::reader_io::and_then<"lifted chain"_ss>(
        fnfelt::monad::reader_io::just<int>(3),
        [](int value) { return fnfelt::monad::reader_io::just<int>(value + 1); });
    CHECK_EQ(chained.build(0).sync_wait(), 4);
}

TEST_CASE("and_then consumes a move-only continuation through its bound callable")
{
    auto result = fnfelt::monad::reader_io::create<int>([](int state)
                                                        { return fnfelt::monad::io::just(state); })
                      .and_then(
                          [offset = std::make_unique<int>(2)](int value)
                          {
                              return fnfelt::monad::reader_io::create<int>(
                                  [value, amount = *offset](int state)
                                  { return fnfelt::monad::io::just(value + amount + state); });
                          });
    CHECK_EQ(std::move(result).build(20).sync_wait(), 42);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
