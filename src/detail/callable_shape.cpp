// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/detail/callable_shape.hpp>

#include <doctest/doctest.h>

namespace
{
/// Complete callable class - the valid stored shape.
struct Callable
{
    int operator()() const
    {
        return 1;
    }
};

/// Incomplete class - cannot be a stored callable.
struct IncompleteCallable;

/// Callable class that cannot be moved - must fail the shape check.
struct NonMovableCallable  // NOLINT(*-special-member-functions)
{
    NonMovableCallable() = default;
    NonMovableCallable(NonMovableCallable &&) = delete;
    int operator()() const
    {
        return 1;
    }
};

/// Member-bearing class, for the pointer-to-member shape.
struct WithMember
{
    int member;
};

/// Alias to a callable - classification must dealias it.
using CallableAlias = Callable;
}  // namespace

TEST_CASE("detail::callable_shape_of classifies the stored-callable shape")
{
    using fnfelt::detail::callable_shape_of;
    using fnfelt::detail::CallableShape;

    // Complete callable class.
    static_assert(callable_shape_of(^^Callable) == CallableShape::valid);
    // A const-qualified callable is still valid: the helper strips cv after the reference check,
    // and move construction from a const value binds the copy constructor for the movability
    // check.
    static_assert(callable_shape_of(^^Callable const) == CallableShape::valid);
    // Function pointer.
    static_assert(callable_shape_of(^^int (*)()) == CallableShape::valid);
    // Incomplete struct.
    static_assert(callable_shape_of(^^IncompleteCallable) == CallableShape::incomplete);
    // References are rejected before the cv stripping.
    static_assert(callable_shape_of(^^Callable &) == CallableShape::reference);
    static_assert(callable_shape_of(^^Callable const &) == CallableShape::reference);
    static_assert(callable_shape_of(^^Callable &&) == CallableShape::reference);
    // Neither a class nor a function pointer: a plain type, a function type (as opposed to a
    // function pointer) and a pointer-to-member.
    static_assert(callable_shape_of(^^int) == CallableShape::not_class_or_function_pointer);
    static_assert(callable_shape_of(^^int()) == CallableShape::not_class_or_function_pointer);
    static_assert(
        callable_shape_of(^^int WithMember::*) == CallableShape::not_class_or_function_pointer);
    // Not move constructible.
    static_assert(callable_shape_of(^^NonMovableCallable) == CallableShape::not_movable);
    // An alias of a callable resolves to the callable.
    static_assert(callable_shape_of(^^CallableAlias) == CallableShape::valid);
    // Void is an incomplete type.
    static_assert(callable_shape_of(^^void) == CallableShape::incomplete);
}

#endif  // DOCTEST_CONFIG_DISABLE
