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

/// Action returning a callable taking an int - a valid ap function IO action.
constexpr auto int_fn_action = [] { return [](int) { return 1; }; };

/// Action returning a callable taking a std::string - incompatible with an int value.
constexpr auto string_fn_action = [] { return [](std::string) { return 1; }; };

/// Action returning a callable taking two ints - must not be spread-fed by ap.
constexpr auto two_arg_fn_action = [] { return [](int, int) { return 1; }; };

/// Action returning a callable taking a pair as a single argument - compatible via ap.
constexpr auto pair_fn_action = []
{ return [](std::pair<int, int> pair_value) { return pair_value.first + pair_value.second; }; };

/// Action returning a callable taking an int and returning void - must fail ap validation.
constexpr auto void_fn_action = [] { return [](int) {}; };

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

TEST_CASE("IOTraits::validate_bind accepts direct and spread kleisli types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Direct: kleisli takes the value as a single argument.
    static_assert(IOTraits::validate_bind(
                      ^^IO<int (*)()>, ^^decltype([](int) { return IO{[] { return 1; }}; }))
                      .empty());
    // Spread over pair.
    static_assert(IOTraits::validate_bind(
                      ^^IO<std::pair<int, int> (*)()>,
                      ^^decltype([](int, int) { return IO{[] { return 1; }}; }))
                      .empty());
    // Spread over tuple.
    static_assert(IOTraits::validate_bind(
                      ^^IO<std::tuple<int, int> (*)()>,
                      ^^decltype([](int, int) { return IO{[] { return 1; }}; }))
                      .empty());
    // Callable struct and function pointer kleisli.
    static_assert(IOTraits::validate_bind(^^IO<int (*)()>, ^^ValidKleisli).empty());
    static_assert(IOTraits::validate_bind(^^IO<int (*)()>, ^^IO<int (*)()> (*)(int)).empty());
    // Source IO with custom traits is still accepted.
    static_assert(IOTraits::validate_bind(^^IO<int (*)(), CustomIOTraits>, ^^ValidKleisli).empty());
}

TEST_CASE("IOTraits::validate_bind rejects non-IO sources with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_bind(^^int, ^^ValidKleisli) == "is not an IO");
}

TEST_CASE("IOTraits::validate_bind rejects kleisli types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Void source value: an IO whose action produces no value cannot feed a continuation. The
    // reflection does not instantiate the IO's class-scope action validation.
    static_assert(
        IOTraits::validate_bind(^^IO<void (*)()>, ^^decltype([](int) { return 1; })) ==
        "source produces no value");
    // Incomplete kleisli.
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^Incomplete) == "is not a complete type");
    // Reference kleisli.
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^decltype([](int) { return 1; }) &) ==
        "must not be a reference type");
    // Non-class, non-function-pointer kleisli.
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^int) ==
        "is not a class or function pointer type");
    // Non-move-constructible kleisli.
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^NonMovableKleisli) ==
        "must be move constructible");
    // Not callable with the value.
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^decltype([](std::string) { return 1; })) ==
        "is not callable with the value");
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "is not callable with the value");
    // Callable but returns a non-IO.
    static_assert(
        IOTraits::validate_bind(^^IO<int (*)()>, ^^decltype([](int) { return 1; })) ==
        "must return an IO");
    // Spread-callable but returns a non-IO.
    static_assert(
        IOTraits::validate_bind(
            ^^IO<std::pair<int, int> (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "must return an IO");
}

TEST_CASE("IOTraits::validate_ap accepts compatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Function IO's value is callable with the value IO's value.
    static_assert(IOTraits::validate_ap(^^IO<decltype(int_fn_action)>, ^^IO<int (*)()>).empty());
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(int_fn_action), CustomIOTraits>, ^^IO<int (*)()>)
            .empty());
}

TEST_CASE("IOTraits::validate_ap rejects non-IO arguments with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(IOTraits::validate_ap(^^int, ^^IO<int (*)()>) == "is not an IO");
    static_assert(IOTraits::validate_ap(^^IO<int (*)()>, ^^int) == "is not an IO");
    static_assert(IOTraits::validate_ap(^^int, ^^int) == "is not an IO");
}

TEST_CASE("IOTraits::validate_ap rejects incompatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // The function IO's value is not callable with the value IO's value.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(string_fn_action)>, ^^IO<int (*)()>) ==
        "function IO's value is not callable with the value IO's value");
}

TEST_CASE("IOTraits::validate_ap rejects a function IO whose callable returns void")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // The application itself produces no value.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(void_fn_action)>, ^^IO<int (*)()>) ==
        "does not return a value");
}

TEST_CASE("IOTraits::validate_ap does not spread tuple-like values")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // ap applies the callable to the whole value, so a two-argument callable is incompatible
    // with a pair value (unlike bind, which would spread the pair into it).
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<int (*)()>) ==
        "function IO's value is not callable with the value IO's value");
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<std::pair<int, int> (*)()>) ==
        "function IO's value is not callable with the value IO's value");
    // A callable taking the whole pair as a single argument is compatible.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(pair_fn_action)>, ^^IO<std::pair<int, int> (*)()>)
            .empty());
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
