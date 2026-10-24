// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/just.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/lift.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// State that cannot be copied (only moved) - just values do not depend on it.
struct NonCopyableState  // NOLINT(*-special-member-functions)
{
    NonCopyableState() = default;
    NonCopyableState(NonCopyableState &&) = default;
    NonCopyableState & operator=(NonCopyableState &&) = delete;
};

/// Incomplete type - must fail the just validator.
struct Incomplete;

inline constexpr char custom_reader_io_name[] = "CustomReader";
using CustomJustReaderIOTraits =
    fnfelt::monad::reader_io::detail::JustReaderIOTraits<custom_reader_io_name>;

/// Constant-evaluated pipeline via the default-traits just overload.
consteval int constexpr_pipeline()
{
    constexpr auto reader = fnfelt::monad::reader_io::just<int>(7);
    return reader.build(123).sync_wait();
}

/// Constant-evaluated pipeline via the named just overload.
consteval int constexpr_pipeline_named()
{
    using fnfelt::literals::operator""_ss;
    constexpr auto reader = fnfelt::monad::reader_io::just<"X"_ss, int>(9);
    return reader.build(456).sync_wait();
}
}  // namespace

TEST_CASE("just yields the value for any state")
{
    auto reader = fnfelt::monad::reader_io::just<int>(5);
    // Any environment - even a non-copyable one - yields the value.
    CHECK_EQ(reader.build(1).sync_wait(), 5);
    CHECK_EQ(reader.build(2).sync_wait(), 5);
    NonCopyableState state;
    auto move_state_reader = fnfelt::monad::reader_io::just<NonCopyableState>(5);
    CHECK_EQ(std::move(move_state_reader).build(std::move(state)).sync_wait(), 5);
}

TEST_CASE("just is equivalent to lifting fnfelt::monad::io::just of the value")
{
    auto lifted = fnfelt::monad::reader_io::lift<int>(fnfelt::monad::io::just(5));
    auto jit = fnfelt::monad::reader_io::just<int>(5);
    static_assert(std::is_same_v<decltype(jit)::action_type, decltype(lifted)::action_type>);
    CHECK_EQ(lifted.build(1).sync_wait(), 5);
}

TEST_CASE("just takes a name and explicit traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto named = fnfelt::monad::reader_io::just<"MyReader"_ss, int>(9);
    static_assert(decltype(named)::traits::name == "MyReader");
    CHECK_EQ(named.build(1).sync_wait(), 9);

    auto custom = fnfelt::monad::reader_io::just<int, CustomJustReaderIOTraits>(9);
    static_assert(decltype(custom)::traits::name == "CustomReader");
    static_assert(std::is_same_v<decltype(custom)::traits, CustomJustReaderIOTraits>);
    static_assert(std::is_base_of_v<
                  fnfelt::monad::reader_io::detail::LiftReaderIOTraits<custom_reader_io_name>,
                  CustomJustReaderIOTraits>);
    CHECK_EQ(custom.build(1).sync_wait(), 9);
}

TEST_CASE("JustReaderIOTraits::validate_just delegates to the io traits validator")
{
    using JustTraits = fnfelt::monad::reader_io::detail::JustReaderIOTraits<>;

    static_assert(JustTraits::validate_just(^^int).empty());
    // The delegated validator's exact reasons.
    static_assert(
        JustTraits::validate_just(^^std::unique_ptr<int>) ==
        "provided value must be copy constructible");
    static_assert(
        JustTraits::validate_just(^^int &) == "provided value must not be a reference type");
    static_assert(
        JustTraits::validate_just(^^Incomplete) == "provided value is not a complete type");
}

TEST_CASE("just pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline() == 7);
    static_assert(constexpr_pipeline_named() == 9);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
