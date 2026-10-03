// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/create.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

#include <fnfelt/static_string.hpp>

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

/// Move-only functor action - must be moved, not copied, into the IO.
struct MoveOnlyAction
{
    std::unique_ptr<int> ptr{std::make_unique<int>(11)};

    MoveOnlyAction() = default;
    MoveOnlyAction(MoveOnlyAction &&) = default;
    MoveOnlyAction & operator=(MoveOnlyAction &&) = delete;
    int operator()() const
    {
        return *ptr;
    }
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline via the named create overload.
consteval int constexpr_pipeline_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto io = fnfelt::monad::io::create<"X"_ss>([] { return 1; });
    return io();
}

/// Constant-evaluated IO pipeline via the default-traits create overload.
consteval int constexpr_pipeline_default()
{
    constexpr auto io = fnfelt::monad::io::create([] { return 1; });
    return io();
}

/// Constant-evaluated IO pipeline via the explicit-traits create overload.
consteval int constexpr_pipeline_traits()
{
    constexpr auto io = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    return io();
}
}  // namespace

TEST_CASE("create with a name wraps the action and names the IO")
{
    using namespace fnfelt::literals;  // NOLINT

    using action_t = decltype([] { return 1; });
    auto io = fnfelt::monad::io::create<"MyIO"_ss>(action_t{});
    static_assert(std::is_same_v<decltype(io)::action, action_t>);
    static_assert(std::is_same_v<decltype(io)::value, int>);
    static_assert(decltype(io)::traits::name == "MyIO");
    CHECK_EQ(io(), 1);
}

TEST_CASE("create defaults to traits naming the IO \"IO\"")
{
    auto io = fnfelt::monad::io::create([] { return 2; });
    static_assert(decltype(io)::traits::name == "IO");
    CHECK_EQ(io(), 2);
}

TEST_CASE("create with explicit IOTraits names the IO via the traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto io =
        fnfelt::monad::io::create<fnfelt::monad::io::IOTraits<"Explicit"_ss>>([] { return 3; });
    static_assert(decltype(io)::traits::name == "Explicit");
    CHECK_EQ(io(), 3);
}

TEST_CASE("create with custom traits uses those traits")
{
    auto io = fnfelt::monad::io::create<CustomIOTraits>([] { return 4; });
    static_assert(std::is_same_v<decltype(io)::traits, CustomIOTraits>);
    CHECK_EQ(io(), 4);
}

TEST_CASE("create decays function lvalues to function pointers")
{
    using namespace fnfelt::literals;  // NOLINT

    auto io = fnfelt::monad::io::create<"Ptr"_ss>(free_action);
    static_assert(std::is_same_v<decltype(io)::action, int (*)()>);
    CHECK_EQ(io(), 42);

    // The function-pointer form of the same free function is also accepted.
    auto io_ptr = fnfelt::monad::io::create<"Ptr"_ss>(&free_action);
    static_assert(std::is_same_v<decltype(io_ptr)::action, int (*)()>);
    CHECK_EQ(io_ptr(), 42);
}

TEST_CASE("create copies lvalue actions and moves rvalue actions")
{
    using namespace fnfelt::literals;  // NOLINT

    auto l = [] { return 5; };
    auto copied = fnfelt::monad::io::create<"L"_ss>(l);
    CHECK_EQ(copied(), 5);
    // The lvalue is untouched, so it remains usable.
    CHECK_EQ(l(), 5);

    auto moved = fnfelt::monad::io::create<"M"_ss>(MoveOnlyAction{});
    CHECK_EQ(moved(), 11);
    static_assert(!std::is_copy_constructible_v<decltype(moved)>);
}

TEST_CASE("create produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_named() == 1);
    static_assert(constexpr_pipeline_default() == 1);
    static_assert(constexpr_pipeline_traits() == 1);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
