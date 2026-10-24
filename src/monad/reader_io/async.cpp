// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/async.hpp>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/async.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// State passed to the coroutine.
struct State
{
    int value = 0;
};

/// Stateless coroutine entry computing the state's value plus a bound addend.
constexpr auto echo_add = [](auto /*self*/, State const & state, int addend) -> lf::task<int>
{ co_return state.value + addend; };

/// Stateless coroutine entry computing the state's value plus two bound addends.
constexpr auto echo_add2 =
    [](auto /*self*/, State const & state, int first_addend, int second_addend) -> lf::task<int>
{ co_return state.value + first_addend + second_addend; };

/// Stateless coroutine entry echoing the state's value.
constexpr auto echo_state = [](auto /*self*/, State const & state) -> lf::task<int>
{ co_return state.value; };

inline constexpr char custom_reader_io_name[] = "CustomReader";
using CustomCreateAsyncReaderIOTraits =
    fnfelt::monad::reader_io::detail::CreateAsyncReaderIOTraits<custom_reader_io_name>;
}  // namespace

TEST_CASE("create_async binds the state as the coroutine's first caller argument")
{
    auto async_reader = fnfelt::monad::reader_io::create_async<State>(echo_add, 20);
    // The bound state's value (5) reaches the coroutine, plus the stored addend (20).
    CHECK_EQ(async_reader.build(State{5}).sync_wait(lf::lazy_pool{2}), 25);
    // The state type is part of the ReaderIO's type.
    static_assert(std::is_same_v<decltype(async_reader)::state_type, State>);
    static_assert(std::is_same_v<decltype(async_reader)::value_type, int>);
}

TEST_CASE("CreateAsyncReaderIOTraits validates the state-first coroutine contract")
{
    using traits_type = fnfelt::monad::reader_io::detail::CreateAsyncReaderIOTraits<>;
    using args_type = std::tuple<State, int>;
    using wrong_args_type = std::tuple<int, int>;
    int const addend = 1;
    auto captured_fn = [addend](auto, State const & state, int value) -> lf::task<int>
    { co_return state.value + value + addend; };

    static_assert(traits_type::validate_create_async(^^decltype(echo_add), ^^args_type).empty());
    static_assert(
        traits_type::validate_create_async(^^decltype(echo_add), ^^wrong_args_type) ==
        "provided async function is not invocable with the given arguments");
    static_assert(
        traits_type::validate_create_async(^^decltype(captured_fn), ^^args_type) ==
        "provided async function must be stateless");
}

TEST_CASE("create_async spreads multiple bound arguments after the state")
{
    auto async_reader = fnfelt::monad::reader_io::create_async<State>(echo_add2, 10, 2);
    // State's value (5) + first bound addend (10) + second (2).
    CHECK_EQ(async_reader.build(State{5}).sync_wait(lf::lazy_pool{2}), 17);
}

TEST_CASE("create_async takes a name and explicit traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto named = fnfelt::monad::reader_io::create_async<"AsyncReader"_ss, State>(echo_add, 1);
    static_assert(decltype(named)::traits::name == "AsyncReader");
    CHECK_EQ(named.build(State{2}).sync_wait(lf::lazy_pool{2}), 3);

    auto custom =
        fnfelt::monad::reader_io::create_async<State, CustomCreateAsyncReaderIOTraits>(echo_add, 5);
    static_assert(decltype(custom)::traits::name == "CustomReader");
    CHECK_EQ(custom.build(State{2}).sync_wait(lf::lazy_pool{2}), 7);
}

TEST_CASE("create_async accepts the state as const& and by value in the coroutine signature")
{
    auto const_ref_reader = fnfelt::monad::reader_io::create_async<State>(echo_state);
    CHECK_EQ(const_ref_reader.build(State{9}).sync_wait(lf::lazy_pool{2}), 9);

    constexpr auto echo_by_value = [](auto /*self*/, State state) -> lf::task<int>
    { co_return state.value; };
    auto by_value_reader = fnfelt::monad::reader_io::create_async<State>(echo_by_value);
    CHECK_EQ(by_value_reader.build(State{11}).sync_wait(lf::lazy_pool{2}), 11);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
