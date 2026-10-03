// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/IO.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>
#include <utility>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Free function used to test construction from a plain function pointer.
int free_action()
{
    return 42;
}

/// Callable struct action with a const, no-argument, value-returning call operator.
struct CallableStruct
{
    int operator()() const
    {
        return 7;
    }
};

/// Move-only kleisli that reads through a unique_ptr as a const lvalue - passes validation.
struct MoveOnlyKleisli
{
    std::unique_ptr<int> ptr{std::make_unique<int>(2)};

    auto operator()(int value) const
    {
        return fnfelt::monad::io::IO{[this, value] { return value + *ptr; }};
    }
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;
}  // namespace

TEST_CASE("IO can be constructed with a capturing lambda returning a value")
{
    using fnfelt::monad::io::IO;

    using action_t = decltype([value = 123] { return value; });
    static_assert(std::is_same_v<decltype(IO(std::declval<action_t>()))::action, action_t>);
}

TEST_CASE("IO can be constructed with a non-capturing lambda returning a value")
{
    using fnfelt::monad::io::IO;

    using action_t = decltype([] { return 456; });
    static_assert(std::is_same_v<decltype(IO(std::declval<action_t>()))::action, action_t>);
}

TEST_CASE("IO can be constructed with a function pointer returning a value")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<decltype(IO(&free_action))::action, int (*)()>);
}

TEST_CASE("IO can be constructed with a callable struct returning a value")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<decltype(IO(CallableStruct{}))::action, CallableStruct>);
}

TEST_CASE("IO uses IOTraits as the default traits")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    static_assert(std::is_same_v<IO<int (*)()>::traits, IOTraits>);
}

TEST_CASE("IO can be instantiated with custom IOTraits")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<IO<int (*)(), CustomIOTraits>::traits, CustomIOTraits>);
}

TEST_CASE("IO exposes the value type its action produces")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<IO<decltype([] { return 456; })>::value, int>);
    static_assert(std::is_same_v<IO<CallableStruct>::value, int>);
    static_assert(std::is_same_v<IO<int (*)()>::value, int>);
}

TEST_CASE("IO operator() runs the action and returns its value")
{
    using fnfelt::monad::io::IO;

    CHECK_EQ(IO{[value = 123] { return value; }}(), 123);
    CHECK_EQ(IO{CallableStruct{}}(), 7);
    CHECK_EQ(IO{&free_action}(), 42);
}

TEST_CASE("IO bind accepts a direct kleisli and runs source then continuation")
{
    using fnfelt::monad::io::IO;

    auto const bound = IO{[] { return 1; }}.bind([](int x) { return IO{[x] { return x + 1; }}; });
    CHECK_EQ(bound(), 2);
}

TEST_CASE("IO bind spreads pair and tuple values into the kleisli")
{
    using fnfelt::monad::io::IO;

    auto const bound_pair = IO{[] { return std::pair{1, 2}; }}.bind(
        [](int x, int y) { return IO{[x, y] { return x + y; }}; });
    CHECK_EQ(bound_pair(), 3);

    auto const bound_tuple = IO{[] { return std::tuple{3, 4}; }}.bind(
        [](int x, int y) { return IO{[x, y] { return x * y; }}; });
    CHECK_EQ(bound_tuple(), 12);
}

TEST_CASE("IO bind prefers direct invocation over spreading")
{
    using fnfelt::monad::io::IO;

    // The kleisli accepts the whole pair, so it must be invoked directly even though the value
    // could also be spread.
    auto const bound = IO{[] { return std::pair{1, 2}; }}.bind(
        [](std::pair<int, int> pair_value)
        { return IO{[pair_value] { return pair_value.first * 10 + pair_value.second; }}; });
    CHECK_EQ(bound(), 12);
}

TEST_CASE("IO bind chains through successive continuations")
{
    using fnfelt::monad::io::IO;

    auto const bound = IO{[] { return 1; }}
                           .bind([](int x) { return IO{[x] { return x + 1; }}; })
                           .bind([](int x) { return IO{[x] { return x * 10; }}; });
    CHECK_EQ(bound(), 20);
}

TEST_CASE("IO bind accepts an lvalue kleisli and copies it")
{
    using fnfelt::monad::io::IO;

    // A named copyable kleisli must be accepted (copied by value), not deduced as a reference
    // type and rejected.
    struct IdentityKleisli
    {
        int offset;

        auto operator()(int value) const
        {
            return IO{[value, offset = offset] { return value + offset; }};
        }
    };

    IdentityKleisli kleisli_lvalue{10};
    auto const bound = IO{[] { return 1; }}.bind(kleisli_lvalue);
    CHECK_EQ(bound(), 11);

    // The lvalue is untouched, so it remains usable.
    CHECK_EQ(kleisli_lvalue(2)(), 12);
}

TEST_CASE("IO bind works on a const IO and with a move-only kleisli")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 5; }};
    auto const bound = source.bind(MoveOnlyKleisli{});
    CHECK_EQ(bound(), 7);
}

TEST_CASE("IO with custom traits binds and runs end-to-end")
{
    using fnfelt::monad::io::IO;

    struct CustomAction
    {
        int value;

        int operator()() const
        {
            return value;
        }
    };

    IO const source{[] { return 1; }};
    auto const bound =
        source.bind([](int x) { return IO<CustomAction, CustomIOTraits>{CustomAction{x + 1}}; });
    CHECK_EQ(bound(), 2);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
