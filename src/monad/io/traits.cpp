// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/detail/just.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Callable struct action with a const, no-argument, value-returning call operator.
struct CallableStruct
{
    int operator()() const
    {
        return 7;
    }
};

/// Plain copyable named struct lifted as a value - passes just validation.
struct CopyableValue
{
};

/// Callable struct action returning void - must fail validation.
struct CallableVoid
{
    void operator()() const {}
};

/// Callable struct action requiring an argument - must fail validation.
struct CallableArg
{
    int operator()(int) const  // NOLINT(*-named-parameter)
    {
        return 1;
    }
};

/// Incomplete type - must fail validation.
struct Incomplete;

/// Action that cannot be moved - must fail validation.
struct NonMovable  // NOLINT(*-special-member-functions)
{
    NonMovable() = default;
    NonMovable(NonMovable &&) = delete;
    int operator()() const
    {
        return 1;
    }
};

/// Action returning a non-movable type - must fail validation.
struct ReturnsNonMovable
{
    NonMovable operator()() const
    {
        return {};
    }
};

/// Action returning a reference - must fail validation.
struct ReturnsReference
{
    int & operator()() const
    {
        static int value = 0;
        return value;
    }
};

/// Inner IO as an action - must fail validation.
// NOLINTNEXTLINE(*-statically-constructed-objects)
constexpr auto inner_io_action = []
{
    using action_t = decltype([] { return false; });
    return fnfelt::monad::io::IO<action_t>{action_t{}};
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::CreateIOTraits<custom_io_name>;

/// Alias to a move-only value - dealias must resolve it before validation.
using UniquePtrAlias = std::unique_ptr<int>;
}  // namespace

TEST_CASE("CreateIOTraits exposes its name")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(CreateIOTraits::name == "IO");
    static_assert(CustomIOTraits::name == "CustomIO");
}

TEST_CASE("CreateIOTraits accepts valid action types")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(CreateIOTraits::validate_action(^^int (*)()).empty());
    static_assert(CreateIOTraits::validate_action(^^CallableStruct).empty());
    static_assert(CreateIOTraits::validate_action(^^decltype([] { return 456; })).empty());
}

TEST_CASE("CreateIOTraits rejects incomplete action types")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^Incomplete) == "provided action is not a complete type");
}

TEST_CASE("CreateIOTraits rejects actions returning void")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^void (*)()) == "provided action does not return a value");
    static_assert(
        CreateIOTraits::validate_action(^^CallableVoid) ==
        "provided action does not return a value");
}

TEST_CASE("CreateIOTraits rejects actions not callable with no arguments")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^int (*)(int)) ==
        "provided action is not callable with no arguments");
    static_assert(
        CreateIOTraits::validate_action(^^CallableArg) ==
        "provided action is not callable with no arguments");
}

TEST_CASE("CreateIOTraits rejects actions returning an IO")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^decltype(inner_io_action)) ==
        "provided action should not return an IO");
}

TEST_CASE("CreateIOTraits rejects wrongly shaped action types")
{
    using fnfelt::monad::io::IO;
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^int (&)()) ==
        "provided action must not be a reference type");
    static_assert(
        CreateIOTraits::validate_action(^^CallableStruct &&) ==
        "provided action must not be a reference type");
    static_assert(
        CreateIOTraits::validate_action(^^IO<int (*)()>) ==
        "provided action is already an IO, pass its action instead");
    static_assert(
        CreateIOTraits::validate_action(^^int) ==
        "provided action is not a class or function pointer type");
    static_assert(
        CreateIOTraits::validate_action(^^int()) ==
        "provided action is not a class or function pointer type");
    static_assert(
        CreateIOTraits::validate_action(^^NonMovable) ==
        "provided action must be move constructible");
}

TEST_CASE("CreateIOTraits accepts actions callable as an rvalue but not as a const lvalue")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    // The stored action is invoked as an rvalue, so a mutable lambda (non-const operator()) is
    // accepted rather than rejected for lacking a const lvalue call operator.
    static_assert(
        CreateIOTraits::validate_action(^^decltype([value = 1] mutable { return value; })).empty());
}

TEST_CASE("CreateIOTraits rejects invalid result types")
{
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^ReturnsReference) ==
        "provided action returns a reference, return by value instead");
    static_assert(
        CreateIOTraits::validate_action(^^ReturnsNonMovable) ==
        "provided action returns a non-movable type");
}

TEST_CASE("CreateIOTraits::is_io delegates to the detail::is_io free helper")
{
    using fnfelt::monad::io::IO;
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(CreateIOTraits::is_io(^^IO<int (*)()>));
    static_assert(fnfelt::monad::io::detail::is_io(^^IO<int (*)()>));
    static_assert(!CreateIOTraits::is_io(^^int));
    static_assert(!fnfelt::monad::io::detail::is_io(^^int));
}

TEST_CASE("JustIOTraits::validate_just checks the lifted value")
{
    using fnfelt::monad::io::IO;
    using JustIOTraits = fnfelt::monad::io::JustIOTraits<>;

    // Complete, non-reference, non-IO, copy-constructible values are accepted.
    static_assert(JustIOTraits::validate_just(^^int).empty());
    static_assert(JustIOTraits::validate_just(^^CopyableValue).empty());
    static_assert(JustIOTraits::validate_just(^^std::string).empty());
    // A move-only value cannot be copied out on lvalue runs.
    static_assert(
        JustIOTraits::validate_just(^^std::unique_ptr<int>) ==
        "provided value must be copy constructible");
    // A reference input is rejected before the copy-constructibility check.
    static_assert(
        JustIOTraits::validate_just(^^int &) == "provided value must not be a reference type");
    // An incomplete type cannot be stored by value.
    static_assert(
        JustIOTraits::validate_just(^^Incomplete) == "provided value is not a complete type");
    // An IO is not a valid lifted value.
    static_assert(
        JustIOTraits::validate_just(^^IO<int (*)()>) == "provided value should not be an IO");
    // An alias to a move-only value is resolved before the copy-constructibility check.
    static_assert(
        JustIOTraits::validate_just(^^UniquePtrAlias) ==
        "provided value must be copy constructible");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
