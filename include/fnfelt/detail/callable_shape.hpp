// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file callable_shape.hpp
 *
 * Reflection predicate for the storage shape shared by stored monad actions and continuations.
 */
#pragma once

#include <meta>

namespace fnfelt::detail
{
/**
 * Storage shape a monad requires of a callable it stores by value.
 */
enum class CallableShape
{
    /// Complete, non-reference, move constructible class or function pointer.
    valid,
    /// Not a complete type.
    incomplete,
    /// A reference type.
    reference,
    /// Neither a class type nor a pointer to function type.
    not_class_or_function_pointer,
    /// Not move constructible.
    not_movable
};

/**
 * Classify the storage shape shared by stored monad actions and continuations.
 *
 * Shape only: invocability is caller-owned and not checked here.
 *
 * The caller owns cv-ref and alias policy on its own reflections: the helper dealiases and strips
 * cv-qualifiers (after the reference check) only while classifying, so a `valid` result does not
 * normalise the caller's argument.
 *
 * @param callable_meta Reflection of the callable type.
 * @return The callable's shape classification.
 */
consteval CallableShape callable_shape_of(std::meta::info callable_meta)
{
    callable_meta = dealias(callable_meta);
    if (!is_complete_type(callable_meta))
    {
        return CallableShape::incomplete;
    }
    if (is_reference_type(callable_meta))
    {
        return CallableShape::reference;
    }
    callable_meta = remove_cvref(callable_meta);
    if (!is_class_type(callable_meta) &&
        !(is_pointer_type(callable_meta) && is_function_type(remove_pointer(callable_meta))))
    {
        return CallableShape::not_class_or_function_pointer;
    }
    if (!is_move_constructible_type(callable_meta))
    {
        return CallableShape::not_movable;
    }
    return CallableShape::valid;
}
}  // namespace fnfelt::detail
