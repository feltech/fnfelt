// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/IO.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

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

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
