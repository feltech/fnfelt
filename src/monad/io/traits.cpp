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

/// Continuation accepting a source value directly - passes validation.
struct ValidContinuation
{
    auto operator()(int) const
    {
        return fnfelt::monad::io::IO{[] { return 1; }};
    }
};

/// Continuation that is not move constructible - must fail validation.
struct NonMovableContinuation  // NOLINT(*-special-member-functions)
{
    NonMovableContinuation() = default;
    NonMovableContinuation(NonMovableContinuation &&) = delete;
    auto operator()(int) const
    {
        return fnfelt::monad::io::IO{[] { return 1; }};
    }
};

/// Transformer accepting a source value directly and returning a plain value - passes validation.
struct ValidTransformer
{
    int operator()(int) const
    {
        return 1;
    }
};

/// Transformer that is not move constructible - must fail validation.
struct NonMovableTransformer  // NOLINT(*-special-member-functions)
{
    NonMovableTransformer() = default;
    NonMovableTransformer(NonMovableTransformer &&) = delete;
    int operator()(int) const
    {
        return 1;
    }
};

/// Transformer returning a reference - must fail validation.
struct TransformerReturnsReference
{
    int & operator()(int) const
    {
        static int value = 0;
        return value;
    }
};

/// Transformer returning an IO - must fail validation.
struct TransformerReturnsIO
{
    auto operator()(int) const
    {
        return fnfelt::monad::io::IO{[] { return 1; }};
    }
};

/// Transformer returning a non-movable type - must fail validation.
struct TransformerReturnsNonMovable
{
    NonMovable operator()(int) const
    {
        return {};
    }
};

/// Two-argument transformer returning void when spread - must fail validation.
struct SpreadTransformerReturnsVoid
{
    void operator()(int, int) const {}
};

/// Two-argument transformer returning an IO when spread - must fail validation.
struct SpreadTransformerReturnsIO
{
    auto operator()(int, int) const
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

/// Action returning a callable taking two ints - spread-fed by ap.
constexpr auto two_arg_fn_action = [] { return [](int, int) { return 1; }; };

/// Action returning a callable taking a pair as a single argument - compatible via ap.
constexpr auto pair_fn_action = []
{ return [](std::pair<int, int> pair_value) { return pair_value.first + pair_value.second; }; };

/// Action returning a callable taking an int and returning void - must fail ap validation.
constexpr auto void_fn_action = [] { return [](int) {}; };

/// Action returning a callable taking two ints and returning void - spread-fed but must fail ap.
constexpr auto two_arg_void_fn_action = [] { return [](int, int) {}; };

/// Async proxy deriving from the tag, exposing a `value_type` alias - passes action validation.
template <class TValue>
struct AsyncProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = TValue;
};

/// Action returning a valid async proxy.
struct AsyncAction
{
    constexpr AsyncProxy<int> operator()() const
    {
        return {};
    }
};

/// Async proxy without a `value_type` alias - must fail action validation.
struct MissingValueProxy : fnfelt::monad::io::AsyncProxyTag
{
};

/// Action returning an async proxy without a `value_type` alias.
struct MissingValueAction
{
    constexpr MissingValueProxy operator()() const
    {
        return {};
    }
};

/// Async proxy whose `value_type` alias is void - must fail action validation.
struct VoidValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = void;
};

/// Action returning an async proxy whose `value_type` alias is void.
struct VoidValueAction
{
    constexpr VoidValueProxy operator()() const
    {
        return {};
    }
};

/// Action returning a reference to a valid async proxy - must fail action validation.
struct ReferenceAsyncAction
{
    AsyncProxy<int> & operator()() const
    {
        static AsyncProxy<int> proxy;
        return proxy;
    }
};

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

    static_assert(
        IOTraits::validate_action(^^Incomplete) == "provided action is not a complete type");
}

TEST_CASE("IOTraits rejects actions returning void")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^void (*)()) == "provided action does not return a value");
    static_assert(
        IOTraits::validate_action(^^CallableVoid) == "provided action does not return a value");
}

TEST_CASE("IOTraits rejects actions not callable with no arguments")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^int (*)(int)) ==
        "provided action is not callable with no arguments");
    static_assert(
        IOTraits::validate_action(^^CallableArg) ==
        "provided action is not callable with no arguments");
}

TEST_CASE("IOTraits rejects actions returning an IO")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^decltype(inner_io_action)) ==
        "provided action should not return an IO");
}

TEST_CASE("IOTraits rejects wrongly shaped action types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^int (&)()) == "provided action must not be a reference type");
    static_assert(
        IOTraits::validate_action(^^CallableStruct &&) ==
        "provided action must not be a reference type");
    static_assert(
        IOTraits::validate_action(^^IO<int (*)()>) ==
        "provided action is already an IO, pass its action instead");
    static_assert(
        IOTraits::validate_action(^^int) ==
        "provided action is not a class or function pointer type");
    static_assert(
        IOTraits::validate_action(^^int()) ==
        "provided action is not a class or function pointer type");
    static_assert(
        IOTraits::validate_action(^^NonMovable) == "provided action must be move constructible");
}

TEST_CASE("IOTraits rejects actions only callable when non-const")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^decltype([value = 1] mutable { return value; })) ==
        "provided action is only callable when non-const");
}

TEST_CASE("IOTraits rejects invalid result types")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_action(^^ReturnsReference) ==
        "provided action returns a reference, return by value instead");
    static_assert(
        IOTraits::validate_action(^^ReturnsNonMovable) ==
        "provided action returns a non-movable type");
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

TEST_CASE("IOTraits::validate_and_then accepts direct and spread continuation types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Direct: the continuation takes the value as a single argument.
    static_assert(IOTraits::validate_and_then(
                      ^^IO<int (*)()>, ^^decltype([](int) { return IO{[] { return 1; }}; }))
                      .empty());
    // Spread over pair.
    static_assert(IOTraits::validate_and_then(
                      ^^IO<std::pair<int, int> (*)()>,
                      ^^decltype([](int, int) { return IO{[] { return 1; }}; }))
                      .empty());
    // Spread over tuple.
    static_assert(IOTraits::validate_and_then(
                      ^^IO<std::tuple<int, int> (*)()>,
                      ^^decltype([](int, int) { return IO{[] { return 1; }}; }))
                      .empty());
    // Callable struct and function pointer continuation.
    static_assert(IOTraits::validate_and_then(^^IO<int (*)()>, ^^ValidContinuation).empty());
    static_assert(IOTraits::validate_and_then(^^IO<int (*)()>, ^^IO<int (*)()> (*)(int)).empty());
    // Source IO with custom traits is still accepted.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)(), CustomIOTraits>, ^^ValidContinuation).empty());
}

TEST_CASE("IOTraits::validate_and_then rejects non-IO sources with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_and_then(^^int, ^^ValidContinuation) == "source IO is not an IO");
}

TEST_CASE("IOTraits::validate_and_then rejects continuation types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Void source value: an IO whose action produces no value cannot feed a continuation. The
    // reflection does not instantiate the IO's class-scope action validation.
    static_assert(
        IOTraits::validate_and_then(^^IO<void (*)()>, ^^decltype([](int) { return 1; })) ==
        "source IO produces no value");
    // Incomplete continuation.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^Incomplete) ==
        "continuation function is not a complete type");
    // Reference continuation.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^decltype([](int) { return 1; }) &) ==
        "continuation function must not be a reference type");
    // Non-class, non-function-pointer continuation.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^int) ==
        "continuation function is not a class or function pointer type");
    // Non-move-constructible continuation.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^NonMovableContinuation) ==
        "continuation function must be move constructible");
    // Does not accept the source IO's value.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^decltype([](std::string) { return 1; })) ==
        "continuation function does not accept the source IO's value");
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "continuation function does not accept the source IO's value");
    // Callable but returns a non-IO.
    static_assert(
        IOTraits::validate_and_then(^^IO<int (*)()>, ^^decltype([](int) { return 1; })) ==
        "continuation function must return an IO");
    // Spread-callable but returns a non-IO.
    static_assert(
        IOTraits::validate_and_then(
            ^^IO<std::pair<int, int> (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "continuation function must return an IO");
}

TEST_CASE("IOTraits::validate_transform accepts direct and spread transformer types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Direct: the transformer takes the value as a single argument.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](int) { return 1; })).empty());
    // Spread over pair.
    static_assert(IOTraits::validate_transform(
                      ^^IO<std::pair<int, int> (*)()>, ^^decltype([](int, int) { return 1; }))
                      .empty());
    // Spread over tuple.
    static_assert(IOTraits::validate_transform(
                      ^^IO<std::tuple<int, int> (*)()>, ^^decltype([](int, int) { return 1; }))
                      .empty());
    // Callable struct and function-pointer transformer.
    static_assert(IOTraits::validate_transform(^^IO<int (*)()>, ^^ValidTransformer).empty());
    static_assert(IOTraits::validate_transform(^^IO<int (*)()>, ^^int (*)(int)).empty());
    // Source IO with custom traits is still accepted.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)(), CustomIOTraits>, ^^ValidTransformer).empty());
}

TEST_CASE("IOTraits::validate_transform rejects non-IO sources with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_transform(^^int, ^^ValidTransformer) == "source IO is not an IO");
}

TEST_CASE("IOTraits::validate_transform rejects transformer types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Void source value: an IO whose action produces no value cannot feed a transformer. The
    // reflection does not instantiate the IO's class-scope action validation.
    static_assert(
        IOTraits::validate_transform(^^IO<void (*)()>, ^^decltype([](int) { return 1; })) ==
        "source IO produces no value");
    // Incomplete transformer.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^Incomplete) ==
        "transformer function is not a complete type");
    // Reference transformer.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](int) { return 1; }) &) ==
        "transformer function must not be a reference type");
    // Non-class, non-function-pointer transformer.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^int) ==
        "transformer function is not a class or function pointer type");
    // Non-move-constructible transformer.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^NonMovableTransformer) ==
        "transformer function must be move constructible");
    // Does not accept the source IO's value.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](std::string) { return 1; })) ==
        "transformer function does not accept the source IO's value");
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "transformer function does not accept the source IO's value");
}

TEST_CASE("IOTraits::validate_transform rejects invalid transformer result types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Direct: void result.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](int) {})) ==
        "transformer function does not return a value");
    // Direct: reference result.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^TransformerReturnsReference) ==
        "transformer function returns a reference, return by value instead");
    // Direct: IO result.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^TransformerReturnsIO) ==
        "transformer function should not return an IO");
    // Direct: non-movable result.
    static_assert(
        IOTraits::validate_transform(^^IO<int (*)()>, ^^TransformerReturnsNonMovable) ==
        "transformer function returns a non-movable type");
    // Spread: void result.
    static_assert(
        IOTraits::validate_transform(
            ^^IO<std::pair<int, int> (*)()>, ^^SpreadTransformerReturnsVoid) ==
        "transformer function does not return a value");
    // Spread: IO result.
    static_assert(
        IOTraits::validate_transform(
            ^^IO<std::pair<int, int> (*)()>, ^^SpreadTransformerReturnsIO) ==
        "transformer function should not return an IO");
}

TEST_CASE("IOTraits::validate_ap accepts compatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // The IO-wrapped function's callable accepts the IO-wrapped value's value.
    static_assert(IOTraits::validate_ap(^^IO<decltype(int_fn_action)>, ^^IO<int (*)()>).empty());
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(int_fn_action), CustomIOTraits>, ^^IO<int (*)()>)
            .empty());
}

TEST_CASE("IOTraits::validate_ap rejects non-IO arguments with the exact reason")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(
        IOTraits::validate_ap(^^int, ^^IO<int (*)()>) == "IO-wrapped function is not an IO");
    static_assert(IOTraits::validate_ap(^^IO<int (*)()>, ^^int) == "IO-wrapped value is not an IO");
    // Both non-IO: the function is checked first.
    static_assert(IOTraits::validate_ap(^^int, ^^int) == "IO-wrapped function is not an IO");
}

TEST_CASE("IOTraits::validate_ap rejects incompatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // The IO-wrapped function's callable does not accept the IO-wrapped value's value.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(string_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
}

TEST_CASE("IOTraits::validate_ap rejects a function IO whose callable returns void")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // The application itself produces no value.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(void_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not return a value");
}

TEST_CASE("IOTraits::validate_ap accepts direct and spread callable types")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Spread over pair.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<std::pair<int, int> (*)()>)
            .empty());
    // Spread over tuple.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<std::tuple<int, int> (*)()>)
            .empty());
    // Non-spreadable value: a two-argument callable is incompatible with an int value.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(two_arg_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
    // A callable taking the whole pair as a single argument is accepted directly.
    static_assert(
        IOTraits::validate_ap(^^IO<decltype(pair_fn_action)>, ^^IO<std::pair<int, int> (*)()>)
            .empty());
    // Spread-invocable but returns void.
    static_assert(
        IOTraits::validate_ap(
            ^^IO<decltype(two_arg_void_fn_action)>, ^^IO<std::pair<int, int> (*)()>) ==
        "IO-wrapped function's callable does not return a value");
}

TEST_CASE("IOTraits::validate_action accepts async proxies and rejects malformed ones")
{
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // A tag-deriving action whose proxy exposes a non-void `value_type` alias is valid.
    static_assert(IOTraits::validate_action(^^AsyncAction).empty());
    // A proxy without a `value_type` alias is rejected with a specific reason.
    static_assert(
        IOTraits::validate_action(^^MissingValueAction) ==
        "provided action's async proxy does not expose a value_type alias");
    // A proxy whose `value_type` alias is void is rejected with a specific reason.
    static_assert(
        IOTraits::validate_action(^^VoidValueAction) ==
        "provided action's async proxy must not produce void");
    // A reference to a valid async proxy is still rejected: the reference check applies to both the
    // synchronous and asynchronous paths.
    static_assert(
        IOTraits::validate_action(^^ReferenceAsyncAction) ==
        "provided action returns a reference, return by value instead");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
