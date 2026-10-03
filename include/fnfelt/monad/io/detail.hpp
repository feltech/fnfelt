// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file detail.hpp
 *
 * Implementation details for the IO monad.
 *
 * Reflection helper predicates and the bind action used by IO::bind.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/fwd.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Check whether a reflection is of a (cv-ref-stripped) specialisation of IO, with any traits.
 *
 * @param type_meta Reflection of the type to check.
 * @return True if the type is an IO specialisation.
 */
consteval bool is_io(std::meta::info type_meta)
{
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^IO;
}

/**
 * Check whether every template argument of a specialisation is a type.
 *
 * @param target_meta Reflection of a type specialisation.
 * @return True if all template arguments are types.
 */
consteval bool all_template_arguments_are_types(std::meta::info target_meta)
{
    for (std::meta::info const arg_meta : template_arguments_of(target_meta))
    {
        if (!is_type(arg_meta))
        {
            return false;
        }
    }
    return true;
}

/**
 * Reflection of the value an action produces when run.
 *
 * Incomplete actions (or non-callable types) have no meaningful result, but invocability queries
 * are a hard error for incomplete types, so `void` is returned as a placeholder in that case.
 *
 * @param action_meta Reflection of the action type.
 * @return Reflection of the action's invocation result, or `void` if the action is incomplete or
 * not callable with no arguments.
 */
consteval std::meta::info action_value_meta(std::meta::info action_meta)
{
    if (is_complete_type(action_meta) && is_invocable_type(action_meta, {}))
    {
        return invoke_result(action_meta, {});
    }
    return ^^void;
}

/**
 * Check whether a callable is directly invocable with a value when the callable is invoked as a
 * const lvalue.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type.
 * @return True if the function accepts the value as a single argument.
 */
consteval bool is_directly_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    return is_invocable_type(add_lvalue_reference(add_const(receiver_meta)), {value_meta});
}

/**
 * Check whether a callable is invocable with a spread value (std::apply) when the callable is
 * invoked as a const lvalue.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the continuation accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    value_meta = dealias(value_meta);
    return has_template_arguments(value_meta) && all_template_arguments_are_types(value_meta) &&
        is_invocable_type(
               add_lvalue_reference(add_const(receiver_meta)), template_arguments_of(value_meta));
}
}  // namespace detail
}  // namespace fnfelt::monad::io
