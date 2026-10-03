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
#include <fnfelt/monad/io/detail/and_then.hpp>
#include <fnfelt/monad/io/detail/ap.hpp>
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

/// Continuation accepting a source value directly - passes validation.
struct ValidContinuation
{
    auto operator()(int) const
    {
        using action_t = decltype([] { return 1; });
        return fnfelt::monad::io::IO<action_t>{action_t{}};
    }
};

/// Ref-qualified continuation: the rvalue overload returns a long-valued IO, so the rvalue receiver
/// contract and result inference must agree on the `&&` overload.
struct RefQualifiedContinuation
{
    auto operator()([[maybe_unused]] int value) const &
    {
        using action_t = decltype([] { return 1; });
        return fnfelt::monad::io::IO<action_t>{action_t{}};
    }

    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    auto operator()([[maybe_unused]] int value) &&
    {
        using action_t = decltype([] { return 2L; });
        return fnfelt::monad::io::IO<action_t>{action_t{}};
    }
};

/// Continuation that is not move constructible - must fail validation.
struct NonMovableContinuation  // NOLINT(*-special-member-functions)
{
    NonMovableContinuation() = default;
    NonMovableContinuation(NonMovableContinuation &&) = delete;
    auto operator()(int) const
    {
        using action_t = decltype([] { return 1; });
        return fnfelt::monad::io::IO<action_t>{action_t{}};
    }
};

/// Inner IO as an action - must fail validation.
// NOLINTNEXTLINE(*-statically-constructed-objects)
constexpr auto inner_io_action = []
{
    using action_t = decltype([] { return false; });
    return fnfelt::monad::io::IO<action_t>{action_t{}};
};

/// Action returning a callable taking an int - a valid ap function IO action.
constexpr auto int_fn_action = [] { return [](int) { return 1; }; };

/// Action returning a callable taking a std::string - incompatible with an int value.
constexpr auto string_fn_action = [] { return [](std::string) { return 1; }; };

/// Action returning a callable taking two ints - spread-fed by ap.
constexpr auto two_arg_fn_action = [] { return [](int, int) { return 1; }; };

/// Action returning a callable taking a pair as a single argument - compatible via ap.
constexpr auto pair_fn_action = []
{ return [](std::pair<int, int> pair_value) { return pair_value.first + pair_value.second; }; };

/// Action returning a callable taking an int and returning void - must fail ap validation.
constexpr auto void_fn_action = [] { return [](int) {}; };

/// Action returning a callable taking two ints and returning void - spread-fed but must fail ap.
constexpr auto two_arg_void_fn_action = [] { return [](int, int) {}; };

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

TEST_CASE("AndThenIOTraits::validate_and_then accepts direct and spread continuation types")
{
    using fnfelt::monad::io::IO;
    using AndThenIOTraits = fnfelt::monad::io::AndThenIOTraits<>;

    // Direct: the continuation takes the value as a single argument.
    static_assert(AndThenIOTraits::validate_and_then(
                      ^^IO<int (*)()>, ^^decltype([](int) {
        using action_t = decltype([] { return 1; });
        return IO<action_t>{action_t{}};
    }))
                      .empty());
    // Spread over pair.
    static_assert(AndThenIOTraits::validate_and_then(
                      ^^IO<std::pair<int, int> (*)()>,
                      ^^decltype([](int, int) {
        using action_t = decltype([] { return 1; });
        return IO<action_t>{action_t{}};
    }))
                      .empty());
    // Spread over tuple.
    static_assert(AndThenIOTraits::validate_and_then(
                      ^^IO<std::tuple<int, int> (*)()>,
                      ^^decltype([](int, int) {
        using action_t = decltype([] { return 1; });
        return IO<action_t>{action_t{}};
    }))
                      .empty());
    // Callable struct and function pointer continuation.
    static_assert(AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^ValidContinuation).empty());
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^IO<int (*)()> (*)(int)).empty());
    // Source IO with custom traits is still accepted.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)(), CustomIOTraits>, ^^ValidContinuation)
            .empty());
}

TEST_CASE("AndThenIOTraits::validate_and_then models the continuation's rvalue receiver")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::io_value_meta;
    using AndThenIOTraits = fnfelt::monad::io::AndThenIOTraits<>;

    // Accepted, and the value of the IO the `&&` overload returns agrees with the contract.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^RefQualifiedContinuation).empty());
    constexpr std::meta::info continuation_io_meta =
        invoke_result(^^RefQualifiedContinuation, {^^int});
    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    static_assert(is_same_type(io_value_meta(continuation_io_meta), ^^long));
}

TEST_CASE("AndThenIOTraits::validate_and_then rejects non-IO sources with the exact reason")
{
    using fnfelt::monad::io::IO;
    using AndThenIOTraits = fnfelt::monad::io::AndThenIOTraits<>;

    static_assert(
        AndThenIOTraits::validate_and_then(^^int, ^^ValidContinuation) == "source IO is not an IO");
}

TEST_CASE("AndThenIOTraits::validate_and_then rejects continuation types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using AndThenIOTraits = fnfelt::monad::io::AndThenIOTraits<>;

    // Void source value: an IO whose action produces no value cannot feed a continuation. The
    // reflection does not instantiate the IO's class-scope action validation.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<void (*)()>, ^^decltype([](int) { return 1; })) ==
        "source IO produces no value");
    // Incomplete continuation.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^Incomplete) ==
        "continuation function is not a complete type");
    // Reference continuation.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^decltype([](int) { return 1; }) &) ==
        "continuation function must not be a reference type");
    // Non-class, non-function-pointer continuation.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^int) ==
        "continuation function is not a class or function pointer type");
    // Non-move-constructible continuation.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^NonMovableContinuation) ==
        "continuation function must be move constructible");
    // Does not accept the source IO's value.
    static_assert(
        AndThenIOTraits::validate_and_then(
            ^^IO<int (*)()>, ^^decltype([](std::string) { return 1; })) ==
        "continuation function does not accept the source IO's value");
    static_assert(
        AndThenIOTraits::validate_and_then(
            ^^IO<int (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "continuation function does not accept the source IO's value");
    // A non-tuple source value with all-type template arguments (e.g. std::optional<int>) is not
    // spread, even though a callable taking its single element would be structurally invocable.
    static_assert(
        AndThenIOTraits::validate_and_then(
            ^^IO<std::optional<int> (*)()>, ^^decltype([](int) {
        using action_t = decltype([] { return 1; });
        return IO<action_t>{action_t{}};
    })) ==
        "continuation function does not accept the source IO's value");
    // Callable but returns a non-IO.
    static_assert(
        AndThenIOTraits::validate_and_then(^^IO<int (*)()>, ^^decltype([](int) { return 1; })) ==
        "continuation function must return an IO");
    // Spread-callable but returns a non-IO.
    static_assert(
        AndThenIOTraits::validate_and_then(
            ^^IO<std::pair<int, int> (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "continuation function must return an IO");
}

TEST_CASE("ApIOTraits::validate_ap accepts compatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::ApIOTraits<>;

    // The IO-wrapped function's callable accepts the IO-wrapped value's value.
    static_assert(ApIOTraits::validate_ap(^^IO<decltype(int_fn_action)>, ^^IO<int (*)()>).empty());
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(int_fn_action), CustomIOTraits>, ^^IO<int (*)()>)
            .empty());
}

TEST_CASE("ApIOTraits::validate_ap rejects non-IO arguments with the exact reason")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::ApIOTraits<>;

    static_assert(
        ApIOTraits::validate_ap(^^int, ^^IO<int (*)()>) == "IO-wrapped function is not an IO");
    static_assert(
        ApIOTraits::validate_ap(^^IO<int (*)()>, ^^int) == "IO-wrapped value is not an IO");
    // Both non-IO: the function is checked first.
    static_assert(ApIOTraits::validate_ap(^^int, ^^int) == "IO-wrapped function is not an IO");
}

TEST_CASE("ApIOTraits::validate_ap rejects incompatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::ApIOTraits<>;

    // The IO-wrapped function's callable does not accept the IO-wrapped value's value.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(string_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
}

TEST_CASE("ApIOTraits::validate_ap rejects a function IO whose callable returns void")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::ApIOTraits<>;

    // The application itself produces no value.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(void_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not return a value");
}

TEST_CASE("ApIOTraits::validate_ap accepts direct and spread callable types")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::ApIOTraits<>;

    // Spread over pair.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<std::pair<int, int> (*)()>)
            .empty());
    // Spread over tuple.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<std::tuple<int, int> (*)()>)
            .empty());
    // Non-spreadable value: a two-argument callable is incompatible with an int value.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
    // A callable taking the whole pair as a single argument is accepted directly.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(pair_fn_action)>, ^^IO<std::pair<int, int> (*)()>)
            .empty());
    // A non-tuple value with all-type template arguments (e.g. std::optional<int>) is not spread,
    // even though the callable would accept its single element structurally.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(int_fn_action)>, ^^IO<std::optional<int> (*)()>) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
    // Spread-invocable but returns void.
    static_assert(
        ApIOTraits::validate_ap(
            ^^IO<decltype(two_arg_void_fn_action)>, ^^IO<std::pair<int, int> (*)()>) ==
        "IO-wrapped function's callable does not return a value");
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
