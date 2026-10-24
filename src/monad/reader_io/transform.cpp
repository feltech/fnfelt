// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/transform.hpp>

#include <doctest/doctest.h>

#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/transform.hpp>
#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Source action producing the state's value.
struct StateAction
{
    constexpr auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

/// Transformer adding a fixed offset to the produced value.
struct OffsetTransformer
{
    constexpr auto operator()(int const & value) const
    {
        return value + 3;
    }
};

struct NonMovableTransformer
{
    NonMovableTransformer() = default;
    NonMovableTransformer(NonMovableTransformer &&) = delete;
    int operator()(int) const;
};

/// Stateful transformer carrying its payload in a member: a move-out on an lvalue run would
/// consume it and corrupt a following run.
struct PrefixingTransformer
{
    std::string prefix;

    explicit PrefixingTransformer(std::string prefix_in) : prefix{std::move(prefix_in)} {}
    auto operator()(int const & value) const
    {
        return prefix + std::to_string(value);
    }
};

/// Transformer taking a spread pair of values.
struct TwoArgSumTransformer
{
    constexpr auto operator()(int const & first_value, int const & second_value) const
    {
        return first_value + second_value;
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

/// State counting its copies.
struct CountingState
{
    explicit CountingState(int value_in = 0) : value{value_in} {}
    CountingState(CountingState const & other) : value{other.value}
    {
        ++copies;
    }
    CountingState(CountingState &&) = default;

    static inline int copies = 0;
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

/// Constant-evaluated pipeline: transform a state-consuming source, bind a state, run.
consteval int constexpr_pipeline()
{
    auto transformed = fnfelt::monad::reader_io::transform(
        fnfelt::monad::reader_io::create<int>(StateAction{}), OffsetTransformer{});
    return transformed.build(5).sync_wait();
}
}  // namespace

TEST_CASE("transform maps the produced value with the transformer")
{
    auto transformed = fnfelt::monad::reader_io::transform(
        fnfelt::monad::reader_io::create<int>(StateAction{}), OffsetTransformer{});
    // The source reads the state (5) and the transformer adds its offset.
    CHECK_EQ(transformed.build(5).sync_wait(), 8);
}

TEST_CASE("TransformReaderIOTraits validates transformer contracts")
{
    using traits_type = fnfelt::monad::reader_io::detail::TransformReaderIOTraits<>;
    using source_type = decltype(fnfelt::monad::reader_io::create<int>(StateAction{}));
    using void_transformer_type = decltype([](int) {});
    using io_transformer_type = decltype([](int value) { return fnfelt::monad::io::just(value); });

    static_assert(traits_type::validate_transform(^^source_type, ^^OffsetTransformer).empty());
    static_assert(
        traits_type::validate_transform(^^source_type, ^^void_transformer_type) ==
        "transformer function does not return a value");
    static_assert(
        traits_type::validate_transform(^^source_type, ^^io_transformer_type) ==
        "transformer function should not return an IO");
    static_assert(
        traits_type::validate_transform(^^source_type, ^^NonMovableTransformer) ==
        "transformer function must be move constructible");
}

TEST_CASE("transform forwards the state unchanged with no internal copy")
{
    CountingState::copies = 0;
    auto transformed = fnfelt::monad::reader_io::transform(
        fnfelt::monad::reader_io::create<CountingState>(CountingSourceAction{}),
        OffsetTransformer{});
    CHECK_EQ(CountingState::copies, 0);

    // The composed action adds no copy: exactly one copy for the lvalue build's by-value bind,
    // none for the rvalue build.
    CountingState lvalue_state{7};
    CHECK_EQ(transformed.build(lvalue_state).sync_wait(), 10);
    CHECK_EQ(CountingState::copies, 1);
    CHECK_EQ(transformed.build(CountingState{7}).sync_wait(), 10);
    CHECK_EQ(CountingState::copies, 1);
}

TEST_CASE("transform replays a stateful transformer without consuming it")
{
    PrefixingTransformer const transformer{"v="};
    auto transformed = fnfelt::monad::reader_io::transform(
        fnfelt::monad::reader_io::create<int>(StateAction{}), transformer);

    // Both lvalue runs see the transformer's full state, so the member was copied per run, never
    // moved out; the caller's transformer is untouched.
    CHECK_EQ(transformed.build(5).sync_wait(), std::string{"v=5"});
    CHECK_EQ(transformed.build(6).sync_wait(), std::string{"v=6"});
    CHECK_EQ(transformer.prefix, std::string{"v="});
}

TEST_CASE("transform spreads a tuple source value into a two-argument transformer")
{
    auto transformed = fnfelt::monad::reader_io::transform(
        fnfelt::monad::reader_io::create<int>(TupleSourceAction{}), TwoArgSumTransformer{});
    // The tuple {1, 2} is spread into the transformer.
    CHECK_EQ(transformed.build(1).sync_wait(), 3);
}

TEST_CASE("transform composes async sources")
{
    auto transformed = fnfelt::monad::reader_io::transform(
        fnfelt::monad::reader_io::create<int>(AsyncDoubleSourceAction{}), OffsetTransformer{});
    // The async source doubles the state (6 -> 12) and the transformer adds its offset.
    CHECK_EQ(transformed.build(6).sync_wait(lf::lazy_pool{2}), 15);
}

TEST_CASE("transform keeps the reader traits and does not inherit them into the inner IO")
{
    using namespace fnfelt::literals;  // NOLINT

    auto source_reader = fnfelt::monad::reader_io::create<int>(StateAction{});

    auto default_transform =
        fnfelt::monad::reader_io::transform(source_reader, OffsetTransformer{});
    static_assert(decltype(default_transform)::traits::name == "ReaderIO");
    static_assert(std::is_same_v<decltype(default_transform)::state_type, int>);
    static_assert(std::is_same_v<decltype(default_transform)::value_type, int>);
    // The inner mapping carries io's own transform traits, not the reader's.
    static_assert(decltype(default_transform.build(0).io())::traits::name == "IO");
    CHECK_EQ(default_transform.build(5).sync_wait(), 8);

    auto named_transform =
        fnfelt::monad::reader_io::transform<"MyReader"_ss>(source_reader, OffsetTransformer{});
    static_assert(decltype(named_transform)::traits::name == "MyReader");
    CHECK_EQ(named_transform.build(5).sync_wait(), 8);
}

TEST_CASE("transform pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline() == 8);
}

TEST_CASE("named transform composes a lifted source")
{
    using fnfelt::literals::operator""_ss;
    auto transformed = fnfelt::monad::reader_io::transform<"lifted transform"_ss>(
        fnfelt::monad::reader_io::just<int>(3), [](int value) { return value + 1; });
    CHECK_EQ(transformed.build(0).sync_wait(), 4);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
