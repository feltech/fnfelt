// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <expected>
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
#include <fnfelt/monad/io/detail/transform.hpp>

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

/// Transformer accepting a source value directly and returning a plain value - passes validation.
struct ValidTransformer
{
    int operator()(int) const
    {
        return 1;
    }
};

/// Ref-qualified transformer: the rvalue overload returns a fundamental `long`, so the rvalue
/// receiver contract and result inference must agree on the `&&` overload.
struct RefQualifiedTransformer
{
    int operator()([[maybe_unused]] int value) const &
    {
        return 1;
    }

    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    long operator()([[maybe_unused]] int value) &&
    {
        return 2;
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
        using action_t = decltype([] { return 1; });
        return fnfelt::monad::io::IO<action_t>{action_t{}};
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

/// Stateless coroutine entry for the executable test proxies; produces its stored argument.
template <class TValue>
struct EchoFn
{
    auto operator()(auto /*self*/, TValue value) const -> lf::task<TValue>
    {
        co_return value;
    }
};

/// Async proxy deriving from the tag, exposing a `value_type` alias and the executable protocol.
template <class TValue>
struct AsyncProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = TValue;
    std::tuple<TValue> arg;
    static constexpr EchoFn<TValue> fn{};
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

/// First base carrying an inherited `value_type` alias, for ambiguity checks.
template <class TValue>
struct AliasBaseA
{
    using value_type = TValue;
};

/// Second base carrying an inherited `value_type` alias, for ambiguity checks.
template <class TValue>
struct AliasBaseB
{
    using value_type = TValue;
};

/// Proxy inheriting identical `value_type` aliases - not ambiguous, and executable.
struct SameAliasProxy : AliasBaseA<int>, AliasBaseB<int>, fnfelt::monad::io::AsyncProxyTag
{
    std::tuple<int> arg;
    static constexpr EchoFn<int> fn{};
};

/// Proxy inheriting differing `value_type` aliases - must fail validation.
struct AmbiguousProxy : AliasBaseA<int>, AliasBaseB<double>, fnfelt::monad::io::AsyncProxyTag
{
};

/// Proxy without the stored `arg` member - must fail validation.
struct MissingArgProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = int;
};

/// Proxy whose `arg` member is not a `std::tuple` - must fail validation.
struct NonTupleArgProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = int;
    int arg = 0;
};

/// Proxy without the static `fn` entry - must fail validation.
struct MissingFnProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = int;
    std::tuple<int> arg;
};

/// Stateless entry returning a plain value rather than an `lf::task`.
struct PlainIntFn
{
    int operator()(auto /*self*/, int value) const
    {
        return value;
    }
};

/// Proxy whose `fn` does not return an `lf::task` - must fail validation.
struct NonTaskFnProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = int;
    std::tuple<int> arg;
    static constexpr PlainIntFn fn{};
};

/// Proxy whose `value_type` is a reference - must fail validation.
struct ReferenceValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = int &;
};

/// Proxy whose `value_type` is an IO - must fail validation.
struct IOValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = fnfelt::monad::io::IO<int (*)()>;
};

/// Proxy whose `value_type` is incomplete - must fail validation.
struct IncompleteValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = Incomplete;
};

/// Proxy whose `value_type` is not movable - must fail validation.
struct NonMovableValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = NonMovable;
};

/// Stepper returning an expected - passes repeat_until validation.
struct RepeatStepper
{
    std::expected<int, int> operator()(int state, int value) const
    {
        if (state == 2)
        {
            return std::expected<int, int>{value + value};
        }
        return std::unexpected{state + 1};
    }
};

/// Two-argument stepper as a function pointer - passes repeat_until validation.
using RepeatStepperPointer = std::expected<int, int> (*)(int, int);

/// Mismatching unexpected type - must fail repeat_until validation.
// NOLINTNEXTLINE(runtime/int): the mismatching unexpected type is the fundamental `long`.
using RepeatMismatchedUnexpected = std::expected<int, long>;

/// Non-move-constructible stepper - must fail validation.
struct RepeatNonMovableStepper  // NOLINT(*-special-member-functions)
{
    RepeatNonMovableStepper() = default;
    RepeatNonMovableStepper(RepeatNonMovableStepper &&) = delete;
    std::expected<int, int> operator()(int, int) const
    {
        return {};
    }
};

/// Stepper callable only as an rvalue - the lvalue invocation must be rejected.
struct RepeatRvalueOnlyStepper
{
    std::expected<int, int> operator()(int state, int value) &&
    {
        if (state == 2)
        {
            return std::expected<int, int>{value};
        }
        return std::unexpected{state + 1};
    }
};

/// Action returning the proxy exposing an incomplete `value_type`; its IO is reflected as
/// producing an incomplete value. A plain action with an incomplete return type is not even
/// invocable on gcc 16.2 (its reflected value degrades to the `void` diagnostic placeholder).
struct IncompleteValueAction
{
    IncompleteValueProxy operator()() const;
};

/// Source action that cannot be copied - must fail repeat_until validation (the loop re-runs the
/// action by copy).
struct RepeatMoveOnlyAction  // NOLINT(*-special-member-functions)
{
    RepeatMoveOnlyAction() = default;
    RepeatMoveOnlyAction(RepeatMoveOnlyAction &&) = delete;
    int operator()() const
    {
        return 1;
    }
};

/// State that cannot be moved - must fail validation.
struct RepeatNonMovableState  // NOLINT(*-special-member-functions)
{
    RepeatNonMovableState() = default;
    RepeatNonMovableState(RepeatNonMovableState &&) = delete;
    RepeatNonMovableState & operator=(RepeatNonMovableState &&) = delete;
};

/// State that is move constructible but not move assignable - must fail validation.
struct RepeatNoAssignState  // NOLINT(*-special-member-functions)
{
    RepeatNoAssignState() = default;
    RepeatNoAssignState(RepeatNoAssignState &&) = default;
    RepeatNoAssignState & operator=(RepeatNoAssignState &&) = delete;
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::detail::CreateIOTraits<custom_io_name>;

/// Alias to a move-only value - dealias must resolve it before validation.
using UniquePtrAlias = std::unique_ptr<int>;
}  // namespace

TEST_CASE("CreateIOTraits exposes its name")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(CreateIOTraits::name == "IO");
    static_assert(CustomIOTraits::name == "CustomIO");
}

TEST_CASE("CreateIOTraits accepts valid action types")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(CreateIOTraits::validate_action(^^int (*)()).empty());
    static_assert(CreateIOTraits::validate_action(^^CallableStruct).empty());
    static_assert(CreateIOTraits::validate_action(^^decltype([] { return 456; })).empty());
}

TEST_CASE("CreateIOTraits rejects incomplete action types")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^Incomplete) == "provided action is not a complete type");
}

TEST_CASE("CreateIOTraits rejects actions returning void")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^void (*)()) == "provided action does not return a value");
    static_assert(
        CreateIOTraits::validate_action(^^CallableVoid) ==
        "provided action does not return a value");
}

TEST_CASE("CreateIOTraits rejects actions not callable with no arguments")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^int (*)(int)) ==
        "provided action is not callable with no arguments");
    static_assert(
        CreateIOTraits::validate_action(^^CallableArg) ==
        "provided action is not callable with no arguments");
}

TEST_CASE("CreateIOTraits rejects actions returning an IO")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(
        CreateIOTraits::validate_action(^^decltype(inner_io_action)) ==
        "provided action should not return an IO");
}

TEST_CASE("CreateIOTraits rejects wrongly shaped action types")
{
    using fnfelt::monad::io::IO;
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

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

TEST_CASE("CreateIOTraits rejects IO-wrapped actions before checking their shape")
{
    using fnfelt::monad::io::IO;
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    // The IO check is hoisted before the callable shape checks, so an IO over an action that would
    // fail the shape checks alone is rejected as an IO first, without inspecting the wrapped
    // action's shape. The type is named but never constructed.
    static_assert(
        CreateIOTraits::validate_action(
            ^^IO<NonMovable, fnfelt::monad::io::detail::CreateIOTraits<>>) ==
        "provided action is already an IO, pass its action instead");
    // A reference to an IO is also rejected as an IO first: the hoisted check delegates to is_io,
    // which strips cv-ref itself, so it wins over the shape checks' reference rejection. The type
    // is named but never constructed.
    static_assert(
        CreateIOTraits::validate_action(
            ^^IO<NonMovable, fnfelt::monad::io::detail::CreateIOTraits<>> &) ==
        "provided action is already an IO, pass its action instead");
}

TEST_CASE("CreateIOTraits accepts actions callable as an rvalue but not as a const lvalue")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    // The stored action is invoked as an rvalue, so a mutable lambda (non-const operator()) is
    // accepted rather than rejected for lacking a const lvalue call operator.
    static_assert(
        CreateIOTraits::validate_action(^^decltype([value = 1] mutable { return value; })).empty());
}

TEST_CASE("CreateIOTraits rejects invalid result types")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

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
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    static_assert(CreateIOTraits::is_io(^^IO<int (*)()>));
    static_assert(fnfelt::monad::io::detail::is_io(^^IO<int (*)()>));
    static_assert(!CreateIOTraits::is_io(^^int));
    static_assert(!fnfelt::monad::io::detail::is_io(^^int));
}

TEST_CASE("CreateIOTraits::validate_action accepts async proxies and rejects malformed ones")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    // A tag-deriving action whose proxy exposes a non-void `value_type` alias is valid.
    static_assert(CreateIOTraits::validate_action(^^AsyncAction).empty());
    // A proxy without a `value_type` alias is rejected with a specific reason.
    static_assert(
        CreateIOTraits::validate_action(^^MissingValueAction) ==
        "provided action's async proxy does not expose a value_type alias");
    // A proxy whose `value_type` alias is void is rejected with a specific reason.
    static_assert(
        CreateIOTraits::validate_action(^^VoidValueAction) ==
        "provided action's async proxy must not produce void");
    // A reference to a valid async proxy is still rejected: the reference check applies to both the
    // synchronous and asynchronous paths.
    static_assert(
        CreateIOTraits::validate_action(^^ReferenceAsyncAction) ==
        "provided action returns a reference, return by value instead");
}

TEST_CASE("CreateIOTraits::validate_async_proxy checks the executable proxy shape")
{
    using CreateIOTraits = fnfelt::monad::io::detail::CreateIOTraits<>;

    // An executable proxy and one with identical inherited `value_type` aliases are accepted.
    static_assert(CreateIOTraits::validate_async_proxy(^^AsyncProxy<int>).empty());
    static_assert(CreateIOTraits::validate_async_proxy(^^SameAliasProxy).empty());

    // Two bases exposing different `value_type` alias targets are ambiguous.
    static_assert(
        CreateIOTraits::validate_async_proxy(^^AmbiguousProxy) ==
        "provided action's async proxy exposes ambiguous value_type aliases");
    // The stored arg member is required, and must be a std::tuple.
    static_assert(
        CreateIOTraits::validate_async_proxy(^^MissingArgProxy) ==
        "provided action's async proxy must expose an arg member");
    static_assert(
        CreateIOTraits::validate_async_proxy(^^NonTupleArgProxy) ==
        "provided action's async proxy arg member must be a std::tuple");
    // The static fn entry is required.
    static_assert(
        CreateIOTraits::validate_async_proxy(^^MissingFnProxy) ==
        "provided action's async proxy must expose a static fn member");
    // fn must return an lf::task.
    static_assert(
        CreateIOTraits::validate_async_proxy(^^NonTaskFnProxy) ==
        "provided action's async proxy fn must return an lf::task");
    // The unwrapped value is subject to the plain-result rules.
    static_assert(
        CreateIOTraits::validate_async_proxy(^^ReferenceValueProxy) ==
        "provided action's async proxy value must not be a reference type");
    static_assert(
        CreateIOTraits::validate_async_proxy(^^IOValueProxy) ==
        "provided action's async proxy value should not be an IO");
    static_assert(
        CreateIOTraits::validate_async_proxy(^^IncompleteValueProxy) ==
        "provided action's async proxy value is not a complete type");
    static_assert(
        CreateIOTraits::validate_async_proxy(^^NonMovableValueProxy) ==
        "provided action's async proxy value must be movable");
}

TEST_CASE("AndThenIOTraits::validate_and_then accepts direct and spread continuation types")
{
    using fnfelt::monad::io::IO;
    using AndThenIOTraits = fnfelt::monad::io::detail::AndThenIOTraits<>;

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
    using AndThenIOTraits = fnfelt::monad::io::detail::AndThenIOTraits<>;

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
    using AndThenIOTraits = fnfelt::monad::io::detail::AndThenIOTraits<>;

    static_assert(
        AndThenIOTraits::validate_and_then(^^int, ^^ValidContinuation) == "source IO is not an IO");
}

TEST_CASE("AndThenIOTraits::validate_and_then rejects continuation types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using AndThenIOTraits = fnfelt::monad::io::detail::AndThenIOTraits<>;

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

TEST_CASE("TransformIOTraits::validate_transform accepts direct and spread transformer types")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;

    // Direct: the transformer takes the value as a single argument.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](int) { return 1; }))
            .empty());
    // Spread over pair.
    static_assert(TransformIOTraits::validate_transform(
                      ^^IO<std::pair<int, int> (*)()>, ^^decltype([](int, int) { return 1; }))
                      .empty());
    // Spread over tuple.
    static_assert(TransformIOTraits::validate_transform(
                      ^^IO<std::tuple<int, int> (*)()>, ^^decltype([](int, int) { return 1; }))
                      .empty());
    // Callable struct and function-pointer transformer.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^ValidTransformer).empty());
    static_assert(TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^int (*)(int)).empty());
    // Source IO with custom traits is still accepted.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)(), CustomIOTraits>, ^^ValidTransformer)
            .empty());
}

TEST_CASE("TransformIOTraits::validate_transform models the transformer's rvalue receiver")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;

    // Accepted, and the inferred result agrees with the `&&` overload the rvalue contract selects.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^RefQualifiedTransformer).empty());
    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    static_assert(is_same_type(invoke_result(^^RefQualifiedTransformer, {^^int}), ^^long));
}

TEST_CASE("TransformIOTraits::validate_transform rejects non-IO sources with the exact reason")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;

    static_assert(
        TransformIOTraits::validate_transform(^^int, ^^ValidTransformer) ==
        "source IO is not an IO");
}

TEST_CASE("TransformIOTraits::validate_transform rejects transformer types with the exact reason")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;

    // Void source value: an IO whose action produces no value cannot feed a transformer. The
    // reflection does not instantiate the IO's class-scope action validation.
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<void (*)()>, ^^decltype([](int) { return 1; })) == "source IO produces no value");
    // Incomplete transformer.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^Incomplete) ==
        "transformer function is not a complete type");
    // Reference transformer.
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<int (*)()>, ^^decltype([](int) { return 1; }) &) ==
        "transformer function must not be a reference type");
    // Non-class, non-function-pointer transformer.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^int) ==
        "transformer function is not a class or function pointer type");
    // Non-move-constructible transformer.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^NonMovableTransformer) ==
        "transformer function must be move constructible");
    // Does not accept the source IO's value.
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<int (*)()>, ^^decltype([](std::string) { return 1; })) ==
        "transformer function does not accept the source IO's value");
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<int (*)()>, ^^decltype([](int, int) { return 1; })) ==
        "transformer function does not accept the source IO's value");
    // A non-tuple source value with all-type template arguments (e.g. std::optional<int>) is not
    // spread, even though a callable taking its single element would be structurally invocable.
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<std::optional<int> (*)()>, ^^decltype([](int) { return 1; })) ==
        "transformer function does not accept the source IO's value");
}

TEST_CASE("TransformIOTraits::validate_transform rejects invalid transformer result types")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;

    // Direct: void result.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^decltype([](int) {})) ==
        "transformer function does not return a value");
    // Direct: reference result.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^TransformerReturnsReference) ==
        "transformer function returns a reference, return by value instead");
    // Direct: IO result.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^TransformerReturnsIO) ==
        "transformer function should not return an IO");
    // Direct: non-movable result.
    static_assert(
        TransformIOTraits::validate_transform(^^IO<int (*)()>, ^^TransformerReturnsNonMovable) ==
        "transformer function returns a non-movable type");
    // Spread: void result.
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<std::pair<int, int> (*)()>, ^^SpreadTransformerReturnsVoid) ==
        "transformer function does not return a value");
    // Spread: IO result.
    static_assert(
        TransformIOTraits::validate_transform(
            ^^IO<std::pair<int, int> (*)()>, ^^SpreadTransformerReturnsIO) ==
        "transformer function should not return an IO");
}

TEST_CASE("ApIOTraits::validate_ap accepts compatible function and value IOs")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::detail::ApIOTraits<>;

    // The IO-wrapped function's callable accepts the IO-wrapped value's value.
    static_assert(ApIOTraits::validate_ap(^^IO<decltype(int_fn_action)>, ^^IO<int (*)()>).empty());
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(int_fn_action), CustomIOTraits>, ^^IO<int (*)()>)
            .empty());
}

TEST_CASE("ApIOTraits::validate_ap rejects non-IO arguments with the exact reason")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::detail::ApIOTraits<>;

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
    using ApIOTraits = fnfelt::monad::io::detail::ApIOTraits<>;

    // The IO-wrapped function's callable does not accept the IO-wrapped value's value.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(string_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
}

TEST_CASE("ApIOTraits::validate_ap rejects a function IO whose callable returns void")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::detail::ApIOTraits<>;

    // The application itself produces no value.
    static_assert(
        ApIOTraits::validate_ap(^^IO<decltype(void_fn_action)>, ^^IO<int (*)()>) ==
        "IO-wrapped function's callable does not return a value");
}

TEST_CASE("ApIOTraits::validate_ap accepts direct and spread callable types")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::detail::ApIOTraits<>;

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
    using JustIOTraits = fnfelt::monad::io::detail::JustIOTraits<>;

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

namespace
{
/// Stateless coroutine functor whose default constructor is not constexpr - cannot be stored as
/// the proxy's `static constexpr TFn`.
struct NonConstexprDefaultCtorFn
{
    NonConstexprDefaultCtorFn();
    auto operator()(auto, int value) const -> lf::task<int>;
};

/// Stateless coroutine functor callable only as an lvalue - incompatible with libfork's rvalue
/// entry invocation.
struct LvalueOnlyFn
{
    auto operator()(auto, int value) & -> lf::task<int>;
};
}  // namespace

TEST_CASE("CreateAsyncIOTraits::validate_create_async checks constant-expression usability")
{
    using CreateAsyncIOTraits = fnfelt::monad::io::detail::CreateAsyncIOTraits<>;

    // `static constexpr TFn fn{}` requires the default constructor to be usable in constant
    // expressions, which a plain default-constructible type with a non-constexpr constructor is
    // not.
    static_assert(
        CreateAsyncIOTraits::validate_create_async(
            ^^NonConstexprDefaultCtorFn, ^^std::tuple<int>) ==
        "provided async function must be usable in constant expressions");
    // libfork invokes the stored function as `std::move(fun)(self, args...)`, so an lvalue-only
    // call operator is rejected by the bare-type (rvalue receiver) invocability check.
    static_assert(
        CreateAsyncIOTraits::validate_create_async(^^LvalueOnlyFn, ^^std::tuple<int>) ==
        "provided async function is not invocable with the given arguments");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
