// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/detail/invocability.hpp>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
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
    constexpr int operator()(int value) const &
    {
        return value;
    }

    // NOLINTNEXTLINE(runtime/int): the rvalue overload returns a distinct type.
    constexpr long operator()(int value) &&
    {
        return value;
    }

    constexpr int operator()(int lhs, int rhs) const &
    {
        return lhs + rhs;
    }

    // NOLINTNEXTLINE(runtime/int): the rvalue overload returns a distinct type.
    constexpr long operator()(int lhs, int rhs) &&
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

/// Functor overloaded on a tuple value and its two elements with distinguishable results: the
/// direct tuple overload wins over the spread one.
struct OverloadedDirectSpreadFn
{
    constexpr int operator()(std::tuple<int, int>) const
    {
        return 1;
    }

    // NOLINTNEXTLINE(runtime/int): the spread overload returns a distinct type.
    constexpr long operator()(int, int) const
    {
        return 2;
    }
};

/// Move-only value proving the dispatch perfect-forwards values into by-value parameters.
struct MoveOnlyValue
{
    MoveOnlyValue() = default;
    MoveOnlyValue(MoveOnlyValue &&) = default;
    MoveOnlyValue & operator=(MoveOnlyValue &&) = default;
    MoveOnlyValue(MoveOnlyValue const &) = delete;
    MoveOnlyValue & operator=(MoveOnlyValue const &) = delete;
};

/// User specialisation whose template arguments are all types but which is not tuple-like.
template <class TFirst, class TSecond>
struct TypeOnlyTemplate
{
};
}  // namespace

TEST_CASE("detail::is_directly_invocable checks single-argument rvalue invocability")
{
    using fnfelt::detail::is_directly_invocable;
    using fnfelt::detail::is_spread_invocable;

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
    using fnfelt::detail::is_spread_invocable;

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

TEST_CASE("detail::is_dispatch_invocable combines the direct and spread predicates")
{
    using fnfelt::detail::is_dispatch_invocable;

    // A direct-only callable is accepted.
    static_assert(is_dispatch_invocable(^^decltype([](int) { return 1; }), ^^int));
    // A spread-only callable is accepted.
    static_assert(
        is_dispatch_invocable(^^decltype([](int, int) { return 1; }), ^^std::pair<int, int>));
    // A callable accepting the value in neither way is rejected.
    static_assert(!is_dispatch_invocable(^^decltype([](int) { return 1; }), ^^std::pair<int, int>));
    // A non-spreadable value is rejected for a spread-only callable.
    static_assert(
        !is_dispatch_invocable(^^decltype([](int, int) { return 1; }), ^^std::optional<int>));
    // The receiver's value category is honoured: an `&&`-only callable with a bare (rvalue)
    // receiver is accepted, but not with an lvalue receiver.
    static_assert(is_dispatch_invocable(^^RvalueDirectFn, ^^int));
    static_assert(!is_dispatch_invocable(add_lvalue_reference(^^RvalueDirectFn), ^^int));
}

TEST_CASE("detail::dispatch_result_meta reflects the dispatch result")
{
    using fnfelt::detail::dispatch_result_meta;

    // Direct invocation result.
    static_assert(
        is_same_type(dispatch_result_meta(^^decltype([](int) { return 1.0; }), ^^int), ^^double));
    // Spread invocation result.
    static_assert(is_same_type(
        dispatch_result_meta(
            ^^decltype([](int lhs, int rhs) { return lhs + rhs; }), ^^std::tuple<int, int>),
        ^^int));
    // Direct invocation wins over spreading.
    static_assert(is_same_type(
        dispatch_result_meta(^^OverloadedDirectSpreadFn, ^^std::tuple<int, int>), ^^int));
    // Non-invocable callables return the void diagnostic placeholder.
    static_assert(is_same_type(
        dispatch_result_meta(^^decltype([](std::string) { return 1; }), ^^int), ^^void));
    // A non-spreadable value returns the void diagnostic placeholder.
    static_assert(is_same_type(
        dispatch_result_meta(
            ^^decltype([](int lhs, int rhs) { return lhs + rhs; }), ^^std::optional<int>),
        ^^void));
}

TEST_CASE("detail::invoke_dispatch invokes directly or spreads")
{
    using fnfelt::detail::invoke_dispatch;

    CHECK_EQ(invoke_dispatch([](int value) { return value * 2; }, 21), 42);
    // The value is spread as if by std::apply.
    CHECK_EQ(invoke_dispatch([](int lhs, int rhs) { return lhs * rhs; }, std::pair{6, 7}), 42);
    // Direct invocation wins over spreading: the tuple overload (not the spread one) runs.
    constexpr auto direct_wins_result =
        invoke_dispatch(OverloadedDirectSpreadFn{}, std::tuple<int, int>{1, 2});
    static_assert(is_same_type(
        ^^decltype(invoke_dispatch(OverloadedDirectSpreadFn{}, std::tuple<int, int>{})), ^^int));
    static_assert(direct_wins_result == 1);
    // The receiver is taken from the callable deduction: through an lvalue, the `&` overload
    // (returning int) runs rather than the `&&` one (returning long).
    constexpr RefQualified lvalue_fn{};
    static_assert(is_same_type(^^decltype(invoke_dispatch(lvalue_fn, 3)), ^^int));
    static_assert(invoke_dispatch(lvalue_fn, 3) == 3);
    // Through an rvalue (a temporary callable), the `&&` overload runs.
    // NOLINTNEXTLINE(runtime/int): the `&&` overload above returns the fundamental `long`.
    static_assert(is_same_type(^^decltype(invoke_dispatch(RefQualified{}, 3)), ^^long));
    static_assert(invoke_dispatch(RefQualified{}, 3) == 3);
    // The value is perfect-forwarded: a move-only value reaches a callable taking it by value.
    // Without `std::forward<TValue>` the call would be ill-formed (the copy is deleted).
    static_assert(invoke_dispatch([](MoveOnlyValue) { return 1; }, MoveOnlyValue{}) == 1);
    CHECK_EQ(invoke_dispatch([](MoveOnlyValue) { return 1; }, MoveOnlyValue{}), 1);
    // The dispatch is constexpr-evaluable in the direct and spread forms.
    static_assert(invoke_dispatch([](int value) { return value * 2; }, 21) == 42);
    static_assert(
        invoke_dispatch([](int lhs, int rhs) { return lhs * rhs; }, std::pair{6, 7}) == 42);
}

TEST_CASE("spread dispatch preserves tuple expression qualifiers")
{
    using fnfelt::detail::dispatch_result_meta;
    using fnfelt::detail::invoke_dispatch;
    using fnfelt::detail::is_spread_invocable;
    constexpr auto mutable_refs = [](int & first, int & second)
    {
        first += second;
        return first;
    };
    constexpr auto const_refs = [](int const & first, int const & second)
    { return first + second; };
    using tuple_type = std::tuple<int, int>;
    static_assert(is_spread_invocable(^^decltype(mutable_refs), ^^tuple_type &));
    static_assert(!is_spread_invocable(^^decltype(mutable_refs), ^^tuple_type const &));
    static_assert(is_spread_invocable(^^decltype(const_refs), ^^tuple_type const &));
    static_assert(
        is_same_type(dispatch_result_meta(^^decltype(mutable_refs), ^^tuple_type &), ^^int));
    tuple_type values{2, 3};
    CHECK_EQ(invoke_dispatch(mutable_refs, values), 5);
    CHECK_EQ(std::get<0>(values), 5);
    CHECK_EQ(invoke_dispatch(const_refs, std::as_const(values)), 8);
    int first = 2;
    int second = 3;
    std::tuple<int &, int &> const references{first, second};
    CHECK_EQ(invoke_dispatch(mutable_refs, references), 5);
}

#endif  // DOCTEST_CONFIG_DISABLE
