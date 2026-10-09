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

/// Move-only action, so an IO wrapping it cannot be copied.
struct MoveOnlyCallable
{
    std::unique_ptr<int> ptr{std::make_unique<int>(9)};

    MoveOnlyCallable() = default;
    MoveOnlyCallable(MoveOnlyCallable &&) = default;
    MoveOnlyCallable & operator=(MoveOnlyCallable &&) = delete;

    int operator()() const
    {
        return *ptr;
    }
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::CreateIOTraits<custom_io_name>;
}  // namespace

TEST_CASE("IO can be constructed with a capturing lambda returning a value")
{
    using fnfelt::monad::io::IO;

    using action_t = decltype([value = 123] { return value; });
    static_assert(
        std::is_same_v<decltype(IO<action_t>(std::declval<action_t>()))::action_type, action_t>);
}

TEST_CASE("IO can be constructed with a non-capturing lambda returning a value")
{
    using fnfelt::monad::io::IO;

    using action_t = decltype([] { return 456; });
    static_assert(
        std::is_same_v<decltype(IO<action_t>(std::declval<action_t>()))::action_type, action_t>);
}

TEST_CASE("IO can be constructed with a function pointer returning a value")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<decltype(IO<int (*)()>(free_action))::action_type, int (*)()>);
}

TEST_CASE("IO can be constructed with a callable struct returning a value")
{
    using fnfelt::monad::io::IO;

    static_assert(
        std::
            is_same_v<decltype(IO<CallableStruct>(CallableStruct{}))::action_type, CallableStruct>);
}

TEST_CASE("IO uses CreateIOTraits as the default traits")
{
    using fnfelt::monad::io::IO;
    using CreateIOTraits = fnfelt::monad::io::CreateIOTraits<>;

    static_assert(std::is_same_v<IO<int (*)()>::traits, CreateIOTraits>);
}

TEST_CASE("IO can be instantiated with custom traits")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<IO<int (*)(), CustomIOTraits>::traits, CustomIOTraits>);
}

TEST_CASE("IO exposes the value type its action produces")
{
    using fnfelt::monad::io::IO;

    static_assert(std::is_same_v<IO<decltype([] { return 456; })>::value_type, int>);
    static_assert(std::is_same_v<IO<CallableStruct>::value_type, int>);
    static_assert(std::is_same_v<IO<int (*)()>::value_type, int>);
}

TEST_CASE("IO build() yields a run proxy whose sync_wait runs the action")
{
    using fnfelt::monad::io::IO;

    auto capturing_action = [value = 123] { return value; };
    CHECK_EQ(IO<decltype(capturing_action)>{capturing_action}.build().sync_wait(), 123);
    CHECK_EQ(IO<CallableStruct>{CallableStruct{}}.build().sync_wait(), 7);
    CHECK_EQ(IO<int (*)()>{&free_action}.build().sync_wait(), 42);
}

TEST_CASE("IO is copy and move constructible")
{
    using fnfelt::monad::io::IO;

    using io_t = IO<CallableStruct>;
    static_assert(std::is_copy_constructible_v<io_t>);
    static_assert(std::is_move_constructible_v<io_t>);
    // The converting constructor excludes IO itself, so an IO lvalue is a copy-constructible
    // argument rather than a converting-constructor one.
    static_assert(std::is_constructible_v<io_t, io_t &>);
    static_assert(std::is_constructible_v<io_t, io_t const &>);
}

TEST_CASE("IO copies from a non-const lvalue IO")
{
    using fnfelt::monad::io::IO;

    using io_t = IO<CallableStruct>;
    auto original = io_t{CallableStruct{}};

    // Copy-initialisation must pick the copy constructor, not the converting constructor.
    auto copy = original;
    // CTAD from an lvalue IO.
    auto deduced{original};
    // Direct-initialisation naming the type.
    io_t named{original};

    CHECK_EQ(copy.build().sync_wait(), 7);
    CHECK_EQ(deduced.build().sync_wait(), 7);
    CHECK_EQ(named.build().sync_wait(), 7);
    // The source is copied, not moved from, so it still runs.
    CHECK_EQ(original.build().sync_wait(), 7);
}

TEST_CASE("IO moves from an rvalue IO")
{
    using fnfelt::monad::io::IO;

    using io_t = IO<MoveOnlyCallable>;
    static_assert(!std::is_copy_constructible_v<io_t>);
    static_assert(std::is_move_constructible_v<io_t>);

    // Copy-initialisation from an xvalue picks the move constructor. The move-only action is copied
    // out of an lvalue IO, so each run proxy needs an rvalue.
    auto source = io_t{MoveOnlyCallable{}};
    auto moved = std::move(source);
    CHECK_EQ(std::move(moved).build().sync_wait(), 9);

    // Direct-initialisation naming the type, from an xvalue.
    auto named_source = io_t{MoveOnlyCallable{}};
    io_t named{std::move(named_source)};
    CHECK_EQ(std::move(named).build().sync_wait(), 9);

    // Direct-initialisation from a prvalue IO.
    io_t prvalue{io_t{MoveOnlyCallable{}}};
    CHECK_EQ(std::move(prvalue).build().sync_wait(), 9);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
