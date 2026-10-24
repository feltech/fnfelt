// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/ReaderIO.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
struct State
{
    int value = 0;
};

struct OtherState
{
    int value = 0;
};

/// Incomplete type - must fail as an action or a state.
struct Incomplete;

/// IO-returning action taking the state as a const reference.
struct IOStateAction
{
    auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

auto state_function(int const & state)
{
    return fnfelt::monad::io::just(state);
}

/// Action taking a State parameter (to mismatches with an int state).
struct StateIOAction
{
    auto operator()(State const & state) const
    {
        return fnfelt::monad::io::just(state.value);
    }
};

struct RvalueStateAction
{
    auto operator()(State && state) const
    {
        return fnfelt::monad::io::just(state.value);
    }
};

/// Action that cannot be moved.
struct NonMovableAction  // NOLINT(*-special-member-functions)
{
    NonMovableAction() = default;
    NonMovableAction(NonMovableAction &&) = delete;
    auto operator()(int const & state) const
    {
        return fnfelt::monad::io::just(state);
    }
};

/// Action returning a plain int.
struct IntReturningAction
{
    int operator()(int const &) const;  // NOLINT(*-named-parameter)
};

/// Action returning a reference to an int.
struct ReferenceReturningAction
{
    int & operator()(int const &) const;  // NOLINT(*-named-parameter)
};

/// Action returning a void-producing IO. Declared only: pure reflection, never called.
struct VoidIOAction
{
    fnfelt::monad::io::IO<void (*)()> operator()(int const &);  // NOLINT(*-named-parameter)
};

/// State that cannot be moved.
struct NonMovableState  // NOLINT(*-special-member-functions)
{
    NonMovableState() = default;
    NonMovableState(NonMovableState &&) = delete;
};

inline constexpr char custom_reader_io_name[] = "CustomReader";
using CustomReaderIOTraits =
    fnfelt::monad::reader_io::detail::CreateReaderIOTraits<custom_reader_io_name>;

using run_proxy_type = fnfelt::monad::reader_io::ReaderRunProxy<int, IntReturningAction>;
}  // namespace

TEST_CASE("CreateReaderIOTraits exposes its name")
{
    using traits_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;

    static_assert(traits_type::name == "ReaderIO");
    static_assert(CustomReaderIOTraits::name == "CustomReader");
}

TEST_CASE("CreateReaderIOTraits::validate_action accepts valid action/state pairs")
{
    using trait_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;

    static_assert(trait_type::validate_action(^^IOStateAction, ^^int).empty());
    static_assert(
        trait_type::validate_action(
            ^^decltype([](int const & state) { return fnfelt::monad::io::just(state); }), ^^int)
            .empty());
    static_assert(trait_type::validate_action(^^decltype(&state_function), ^^int).empty());
    // A cv-qualified state is modelled by value.
    static_assert(trait_type::validate_action(^^IOStateAction, ^^int const).empty());
    static_assert(trait_type::validate_action(^^RvalueStateAction, ^^State).empty());
    static_assert(
        trait_type::validate_action(^^RvalueStateAction, ^^State const) ==
        "provided action is not callable with the provided state");
}

TEST_CASE("CreateReaderIOTraits::validate_action rejects wrongly shaped action types")
{
    using trait_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;
    using reader_type = fnfelt::monad::reader_io::ReaderIO<int, IntReturningAction>;

    static_assert(
        trait_type::validate_action(^^Incomplete, ^^int) ==
        "provided action is not a complete type");
    static_assert(
        trait_type::validate_action(^^IOStateAction &, ^^int) ==
        "provided action must not be a reference type");
    static_assert(
        trait_type::validate_action(^^int, ^^int) ==
        "provided action is not a class or function pointer type");
    static_assert(
        trait_type::validate_action(^^NonMovableAction, ^^int) ==
        "provided action must be move constructible");
    // Identity checks run before the shape checks.
    static_assert(
        trait_type::validate_action(^^reader_type, ^^int) ==
        "provided action is already a ReaderIO, pass its action instead");
    static_assert(
        trait_type::validate_action(^^fnfelt::monad::io::IO<int (*)()>, ^^int) ==
        "provided action is already an IO, use lift to embed it");
}

TEST_CASE("CreateReaderIOTraits::validate_action rejects invalid state types")
{
    using trait_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;

    static_assert(
        trait_type::validate_action(^^IOStateAction, ^^int &) ==
        "provided state must not be a reference type");
    static_assert(
        trait_type::validate_action(^^IOStateAction, ^^Incomplete) ==
        "provided state is not a complete type");
    static_assert(
        trait_type::validate_action(^^IOStateAction, ^^NonMovableState) ==
        "provided state must be move constructible");
}

TEST_CASE("CreateReaderIOTraits::validate_action checks the action/state invocation contract")
{
    using trait_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;

    // The action does not accept the provided state.
    static_assert(
        trait_type::validate_action(^^StateIOAction, ^^int) ==
        "provided action is not callable with the provided state");
    // The invocation result contract.
    static_assert(
        trait_type::validate_action(^^IntReturningAction, ^^int) ==
        "provided action does not return an IO");
    static_assert(
        trait_type::validate_action(^^ReferenceReturningAction, ^^int) ==
        "provided action returns a reference, return by value instead");
    static_assert(
        trait_type::validate_action(^^VoidIOAction, ^^int) ==
        "provided action returns an IO that produces no value");
}

TEST_CASE("CreateReaderIOTraits::validate_sync_wait gates the run proxy consumption")
{
    using trait_type = fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>;
    constexpr std::string_view rvalue_message =
        "the ReaderIO run proxy must be consumed as an rvalue (e.g. std::move(proxy).sync_wait())";

    // Rvalue (and by-value) consumption is accepted.
    static_assert(trait_type::validate_sync_wait(^^run_proxy_type).empty());
    static_assert(trait_type::validate_sync_wait(^^run_proxy_type &&).empty());
    // Lvalue proxies are rejected.
    static_assert(trait_type::validate_sync_wait(^^run_proxy_type &) == rvalue_message);
    // Const-bound rvalues are rejected too (the members cannot be moved out).
    static_assert(trait_type::validate_sync_wait(^^run_proxy_type const &&) == rvalue_message);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
