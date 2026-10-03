// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file detail.hpp
 *
 * Generic implementation details for the IO monad.
 *
 * Reflection helper predicates shared by the IO monad's operations.
 */
#pragma once

#include <meta>

#include <string_view>

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
 * Non-type template parameters (e.g. `std::array<T, N>`'s `N`) cannot be spread into a kleisli
 * invocation, and `template_arguments_of` on an alias must be dealiased before use.
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
 * Reflection of the action type wrapped by an IO.
 *
 * Non-IO types have no wrapped action, but `template_arguments_of` throws for non-template types
 * (a consteval throw is a hard compile error), so `void` is returned as a placeholder in that
 * case, mirroring `action_value_meta`'s convention.
 *
 * @param io_meta Reflection of the (possibly IO) type.
 * @return Reflection of the wrapped action, or `void` if the type is not an IO.
 */
consteval std::meta::info io_action_meta(std::meta::info io_meta)
{
    if (!is_io(io_meta))
    {
        return ^^void;
    }
    // Extract the scalar inside the consteval function: vector<info> results allocate and cannot
    // escape a constant-evaluated context.
    return template_arguments_of(dealias(remove_cvref(io_meta)))[0];
}

/**
 * Reflection of the value an IO's action produces when run.
 *
 * @param io_meta Reflection of the (possibly IO) type.
 * @return Reflection of the wrapped action's invocation result, or `void` if the type is not an IO
 * (or the action is incomplete or not callable, via `action_value_meta`).
 */
consteval std::meta::info io_value_meta(std::meta::info io_meta)
{
    return action_value_meta(io_action_meta(io_meta));
}

/**
 * Check whether a kleisli is directly invocable with the source IO's value.
 *
 * The kleisli is stored as a const member and invoked as a const lvalue, so const-lvalue receiver
 * semantics are checked (`add_lvalue_reference(add_const(...))`).
 *
 * @param kleisli_meta Reflection of the kleisli type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the kleisli accepts the value as a single argument.
 */
consteval bool is_directly_invocable(std::meta::info kleisli_meta, std::meta::info value_meta)
{
    return is_invocable_type(add_lvalue_reference(add_const(kleisli_meta)), {value_meta});
}

/**
 * Check whether a kleisli is invocable with the source IO's value spread as several arguments.
 *
 * Values that are specialisations with all-type template arguments (e.g. `std::pair<int, int>`,
 * `std::tuple<int, int>`) have their arguments spread into the kleisli invocation. The value
 * reflection is dealiased first because alias reflections report `has_template_arguments` false.
 *
 * @note Spreading follows the value's template arguments, so non-tuple specialisations whose
 * template arguments are all types (e.g. `std::vector<int>` spreads as `int,
 * std::allocator<int>`) are also treated as spread-callable, matching the legacy `applicable_with`
 * semantics; `std::apply` will only accept genuinely tuple-like values at run time.
 *
 * @param kleisli_meta Reflection of the kleisli type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the kleisli accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable(std::meta::info kleisli_meta, std::meta::info value_meta)
{
    value_meta = dealias(value_meta);
    return has_template_arguments(value_meta) && all_template_arguments_are_types(value_meta) &&
        is_invocable_type(
               add_lvalue_reference(add_const(kleisli_meta)), template_arguments_of(value_meta));
}

/**
 * Name of IO.
 *
 * @tparam io_meta IO reflection to check.
 * @return "<unknown>" if the IO reflection is not an IO, otherwise the IO's name.
 */
template <std::meta::info io_meta>
constexpr std::string_view maybe_io_name()
{
    if constexpr (is_io(io_meta))
    {
        return [:io_meta:] ::name;
    }

    return "<unknown>";
}
}  // namespace detail
}  // namespace fnfelt::monad::io
