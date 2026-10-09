// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/detail.hpp
 *
 * Generic implementation details for the IO monad.
 *
 * Reflection helper predicates shared by the IO monad's operations.
 */
#pragma once

#include <meta>

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
 * Check whether a reflection is of an empty class type.
 *
 * A stateless object holds no state: it has no non-static data members in itself or in any base.
 * Virtual functions and virtual bases also count as state for this check. This is how a stateless
 * callable (e.g. a non-capturing lambda) is recognised.
 *
 * @param type_meta Reflection of the type to check.
 * @return True if the type is a complete, non-polymorphic class type with no non-static data
 * members anywhere.
 */
consteval bool is_empty_object(std::meta::info type_meta)
{
    // Member queries do not see through cv-ref qualifiers, so strip them first.
    type_meta = dealias(remove_cvref(type_meta));
    // `is_complete_type` returns false (rather than throwing) for non-type reflections, so it
    // guards the `is_class_type` category query.
    if (!is_complete_type(type_meta) || !is_class_type(type_meta))
    {
        return false;
    }
    // `is_empty_type` reports no non-static data members, bases included. The obvious
    // `nonstatic_data_members_of` walk does NOT work for closures on gcc 16.2: it reports zero
    // members for a capturing lambda, so a stateful async function would slip through.
    return is_empty_type(type_meta);
}

/**
 * Reflection of the value an action produces when run.
 *
 * An asynchronous action's value comes from its proxy's `value_type` alias.
 *
 * Inputs may be pre-validation, so `void` is the diagnostic placeholder: it keeps an invalid
 * instantiation well-formed while the companion validator's static_assert remains the single,
 * friendly error. Validated inputs never observe the placeholder.
 *
 * @param action_meta Reflection of the action type.
 * @return Reflection of the action's invocation result, or `void` if the action is incomplete, not
 * callable with no arguments, or an async proxy without a usable `value_type` alias.
 */
consteval std::meta::info action_value_meta(std::meta::info action_meta)
{
    // Invocability queries are a hard error for incomplete types, so guard completeness first.
    if (is_complete_type(action_meta) && is_invocable_type(action_meta, {}))
    {
        if (action_is_async(action_meta))
        {
            return async_proxy_value_meta(remove_cvref(invoke_result(action_meta, {})));
        }
        return invoke_result(action_meta, {});
    }
    return ^^void;
}

/**
 * Reflection of the action type wrapped by an IO.
 *
 * @param io_meta Reflection of the (possibly IO) type.
 * @return Reflection of the wrapped action, or `void` if the type is not an IO. The `void` is the
 * diagnostic placeholder described on `action_value_meta`.
 */
consteval std::meta::info io_action_meta(std::meta::info io_meta)
{
    // `template_arguments_of` throws for non-template types, and a consteval throw is a hard
    // compile error, so `void` is returned as a placeholder for non-IO types.
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
 * or its action produces no value.
 */
consteval std::meta::info io_value_meta(std::meta::info io_meta)
{
    return action_value_meta(io_action_meta(io_meta));
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
        return [:io_meta:] ::traits::name;
    }

    return "<unknown>";
}
}  // namespace detail
}  // namespace fnfelt::monad::io
