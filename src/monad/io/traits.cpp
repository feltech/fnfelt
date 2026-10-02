// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/traits.hpp>

#include <doctest/doctest.h>

#include <string>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>

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

/// Kleisli accepting a source value directly - passes validation.
struct ValidKleisli
{
    auto operator()(int) const
    {
        return fnfelt::monad::io::IO{[] { return 1; }};
    }
};

/// Kleisli that is not move constructible - must fail validation.
struct NonMovableKleisli  // NOLINT(*-special-member-functions)
{
    NonMovableKleisli() = default;
    NonMovableKleisli(NonMovableKleisli &&) = delete;
    auto operator()(int) const
    {
        return fnfelt::monad::io::IO{[] { return 1; }};
    }
};

/// Inner IO as an action - must fail validation.
// NOLINTNEXTLINE(*-statically-constructed-objects)
constexpr auto inner_io_action = [] { return fnfelt::monad::io::IO{[] { return false; }}; };

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;
}  // namespace

TEST_CASE("IOTraits exposes its name")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::name == "IO");
    static_assert(CustomIOTraits::name == "CustomIO");
}

TEST_CASE("IOTraits accepts valid action types")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_action(^^int (*)()).empty());
    static_assert(IOTraits::validate_action(^^CallableStruct).empty());
    static_assert(IOTraits::validate_action(^^decltype([] { return 456; })).empty());
}

TEST_CASE("IOTraits rejects incomplete action types")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_action(^^Incomplete) == "is not a complete type");
}

TEST_CASE("IOTraits rejects actions returning void")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_action(^^void (*)()) == "does not return a value");
    static_assert(IOTraits::validate_action(^^CallableVoid) == "does not return a value");
}

TEST_CASE("IOTraits rejects actions not callable with no arguments")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_action(^^int (*)(int)) == "is not callable with no arguments");
    static_assert(IOTraits::validate_action(^^CallableArg) == "is not callable with no arguments");
}

TEST_CASE("IOTraits rejects actions returning an IO")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^decltype(inner_io_action)) == "should not return an IO");
}

TEST_CASE("IOTraits rejects wrongly shaped action types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_action(^^int (&)()) == "must not be a reference type");
    static_assert(IOTraits::validate_action(^^CallableStruct &&) == "must not be a reference type");
    static_assert(
        IOTraits::validate_action(^^IO<int (*)()>) == "is already an IO, pass its action instead");
    static_assert(IOTraits::validate_action(^^int) == "is not a class or function pointer type");
    static_assert(IOTraits::validate_action(^^int()) == "is not a class or function pointer type");
    static_assert(IOTraits::validate_action(^^NonMovable) == "must be move constructible");
}

TEST_CASE("IOTraits rejects actions only callable when non-const")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^decltype([value = 1] mutable { return value; })) ==
        "is only callable when non-const");
}

TEST_CASE("IOTraits rejects invalid result types")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^ReturnsReference) ==
        "returns a reference, return by value instead");
    static_assert(IOTraits::validate_action(^^ReturnsNonMovable) == "returns a non-movable type");
}

TEST_CASE("IOTraits::is_io delegates to the detail::is_io free helper")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::is_io(^^IO<int (*)()>));
    static_assert(fnfelt::monad::io::detail::is_io(^^IO<int (*)()>));
    static_assert(!IOTraits::is_io(^^int));
    static_assert(!fnfelt::monad::io::detail::is_io(^^int));
}

TEST_CASE("IOTraits::validate_kleisli accepts direct and spread kleisli types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Direct: kleisli takes the value as a single argument.
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^decltype([](int) { return IO{[] { return 1; }}; }))
            .empty());
    // Spread over pair.
    static_assert(
        IOTraits::validate_kleisli(
            ^^std::pair<int, int>, ^^decltype([](int, int) { return IO{[] { return 1; }}; }))
            .empty());
    // Spread over tuple.
    static_assert(
        IOTraits::validate_kleisli(
            ^^std::tuple<int, int>, ^^decltype([](int, int) { return IO{[] { return 1; }}; }))
            .empty());
    // Callable struct and function pointer kleisli.
    static_assert(IOTraits::validate_kleisli(^^int, ^^ValidKleisli).empty());
    static_assert(IOTraits::validate_kleisli(^^int, ^^IO<int (*)()> (*)(int)).empty());
}

TEST_CASE("IOTraits::validate_kleisli rejects kleisli types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Void source value.
    static_assert(
        IOTraits::validate_kleisli(^^void, ^^decltype([](int) { return 1; })) ==
        "source produces no value");
    // Incomplete kleisli.
    static_assert(IOTraits::validate_kleisli(^^int, ^^Incomplete) == "is not a complete type");
    // Reference kleisli.
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^decltype([](int) { return 1; }) &) ==
        "must not be a reference type");
    // Non-class, non-function-pointer kleisli.
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^int) == "is not a class or function pointer type");
    // Non-move-constructible kleisli.
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^NonMovableKleisli) == "must be move constructible");
    // Not callable with the value.
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^decltype([](std::string) { return 1; })) ==
        "is not callable with the value");
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^decltype([](int, int) { return 1; })) ==
        "is not callable with the value");
    // Callable but returns a non-IO.
    static_assert(
        IOTraits::validate_kleisli(^^int, ^^decltype([](int) { return 1; })) ==
        "must return an IO");
    // Spread-callable but returns a non-IO.
    static_assert(
        IOTraits::validate_kleisli(^^std::pair<int, int>, ^^decltype([](int, int) { return 1; })) ==
        "must return an IO");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
