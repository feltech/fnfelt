// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/detail.hpp>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <fnfelt/monad/io/IO.hpp>

namespace
{
/// Incomplete type - has no meaningful invocation result.
struct Incomplete;

/// Directly callable only as a const lvalue (its rvalue overload is deleted).
struct ConstLvalueDirectFn
{
    int operator()(int value) const &
    {
        return value;
    }

    int operator()(int value) && = delete;
};

/// Directly callable only as an rvalue.
struct RvalueDirectFn
{
    int operator()(int value) &&
    {
        return value;
    }
};

/// Ref-qualified callable with distinct const-lvalue and rvalue return types: the rvalue contract
/// models the `&&` overloads.
struct RefQualified
{
    int operator()(int value) const &
    {
        return value;
    }

    // NOLINTNEXTLINE(runtime/int): the rvalue overload returns a distinct type.
    long operator()(int value) &&
    {
        return value;
    }

    int operator()(int lhs, int rhs) const &
    {
        return lhs + rhs;
    }

    // NOLINTNEXTLINE(runtime/int): the rvalue overload returns a distinct type.
    long operator()(int lhs, int rhs) &&
    {
        return lhs + rhs;
    }
};

/// Spread-callable only as a const lvalue (its rvalue overload is deleted).
struct ConstLvalueSpreadFn
{
    int operator()(int, int) const &
    {
        return 1;
    }

    int operator()(int, int) && = delete;
};

/// Spread-callable only as an rvalue.
struct RvalueSpreadFn
{
    int operator()(int, int) &&
    {
        return 1;
    }
};

/// User specialisation whose template arguments are all types but which is not tuple-like.
template <class TFirst, class TSecond>
struct TypeOnlyTemplate
{
};
}  // namespace

TEST_CASE("detail::action_value_meta reflects the action's result")
{
    using fnfelt::monad::io::detail::action_value_meta;

    static_assert(is_same_type(action_value_meta(^^int (*)()), ^^int));
    // Non-callable and incomplete actions have no meaningful result.
    static_assert(is_same_type(action_value_meta(^^int), ^^void));
    static_assert(is_same_type(action_value_meta(^^Incomplete), ^^void));
}

TEST_CASE("detail::is_directly_invocable checks single-argument rvalue invocability")
{
    using fnfelt::monad::io::detail::is_directly_invocable;
    using fnfelt::monad::io::detail::is_spread_invocable;

    // Direct: continuation takes the value as a single argument.
    static_assert(is_directly_invocable(^^decltype([](int) { return 1; }), ^^int));
    // A spread-only continuation is not directly invocable.
    static_assert(
        !is_directly_invocable(^^decltype([](int, int) { return 1; }), ^^std::pair<int, int>));
    // A directly invocable continuation is not spread invocable on the whole value.
    static_assert(!is_spread_invocable(^^decltype([](int) { return 1; }), ^^int));
    // The receiver is an rvalue: a callable whose rvalue overload is deleted is not invocable.
    static_assert(!is_directly_invocable(^^ConstLvalueDirectFn, ^^int));
    // An rvalue-only callable is directly invocable.
    static_assert(is_directly_invocable(^^RvalueDirectFn, ^^int));
    // A ref-qualified callable is dispatched to its `&&` overload, so the inferred result is long.
    static_assert(is_directly_invocable(^^RefQualified, ^^int));
    // NOLINTNEXTLINE(runtime/int) - the `&&` overload above returns the fundamental `long`.
    static_assert(is_same_type(invoke_result(^^RefQualified, {^^int}), ^^long));
}

TEST_CASE("detail::is_spread_invocable checks rvalue spread invocability")
{
    using fnfelt::monad::io::detail::is_spread_invocable;

    // Spread: the callable takes the value's elements as its parameter pack.
    static_assert(
        is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^std::pair<int, int>));
    static_assert(
        is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^std::tuple<int, int>));
    // A direct-only continuation is not spread invocable.
    static_assert(
        !is_spread_invocable(^^decltype([](std::pair<int, int>) {}), ^^std::pair<int, int>));
    // The receiver is an rvalue: a callable whose rvalue overload is deleted is not invocable.
    static_assert(!is_spread_invocable(^^ConstLvalueSpreadFn, ^^std::pair<int, int>));
    // A callable only invocable as an rvalue is accepted: the receiver is the bare prvalue.
    static_assert(is_spread_invocable(^^RvalueSpreadFn, ^^std::pair<int, int>));
    // A ref-qualified callable is dispatched to its `&&` overload, selecting the long result.
    static_assert(is_spread_invocable(^^RefQualified, ^^std::pair<int, int>));
    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    static_assert(is_same_type(invoke_result(^^RefQualified, {^^int, ^^int}), ^^long));
    // A non-spreadable value has no template arguments to spread.
    static_assert(!is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^int));
    // Only pair/tuple specialisations are spread. These have all-type template arguments that the
    // callable would accept structurally, but `std::apply` cannot unpack them.
    static_assert(!is_spread_invocable(^^decltype([](int) { return 1; }), ^^std::optional<int>));
    static_assert(!is_spread_invocable(
        ^^decltype([](int, std::allocator<int>) { return 1; }), ^^std::vector<int>));
    static_assert(
        !is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^std::array<int, 2>));
    static_assert(
        !is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^TypeOnlyTemplate<int, int>));
}

#endif  // DOCTEST_CONFIG_DISABLE
