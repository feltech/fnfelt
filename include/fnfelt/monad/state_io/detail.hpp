// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail.hpp
 *
 * Generic implementation details for the StateIO monad.
 *
 * Reflection helper predicates shared by the StateIO monad's operations.
 */
#pragma once

#include <meta>

#include <fnfelt/monad/state_io/fwd.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{
/**
 * Check whether a reflection is of a (cv-ref-stripped) specialisation of StateIO, with any state
 * type, action and traits.
 *
 * @param type_meta Reflection of the type to check.
 * @return True if the type is a StateIO specialisation.
 */
consteval bool is_state_io(std::meta::info type_meta)
{
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^StateIO;
}

/**
 * Reflection of the state type a StateIO is typed over.
 *
 * @param state_io_meta Reflection of the (possibly StateIO) type.
 * @return Reflection of the state type, or `void` if the type is not a StateIO. The `void` is
 * the diagnostic placeholder: it keeps an invalid instantiation well-formed while the companion
 * validators remain the single, friendly error.
 */
consteval std::meta::info state_io_state_meta(std::meta::info state_io_meta)
{
    // `template_arguments_of` throws for non-template types, and a consteval throw is a hard
    // compile error, so `void` is returned as a placeholder for non-StateIO types.
    if (!is_state_io(state_io_meta))
    {
        return ^^void;
    }
    // Extract the scalar inside the consteval function: vector<info> results allocate and cannot
    // escape a constant-evaluated context.
    return template_arguments_of(dealias(remove_cvref(state_io_meta)))[0];
}

/**
 * Reflection of the action type wrapped by a StateIO.
 *
 * The action is the StateIO's second template argument (the first is the state type).
 *
 * @param state_io_meta Reflection of the (possibly StateIO) type.
 * @return Reflection of the wrapped action, or `void` if the type is not a StateIO, with the
 * same diagnostic placeholder semantics as `state_io_state_meta`.
 */
consteval std::meta::info state_io_action_meta(std::meta::info state_io_meta)
{
    if (!is_state_io(state_io_meta))
    {
        return ^^void;
    }
    return template_arguments_of(dealias(remove_cvref(state_io_meta)))[1];
}

/**
 * Name of StateIO.
 *
 * @tparam state_io_meta StateIO reflection to check.
 * @return "<unknown>" if the reflection is not a StateIO, otherwise the StateIO's name.
 */
template <std::meta::info state_io_meta>
constexpr std::string_view maybe_state_io_name()
{
    // Guarded like `is_state_io`'s members: the member splice requires the specialization to be
    // complete, so diagnostics over pre-validation or declared-only reflections stay well-formed.
    if constexpr (
        is_state_io(state_io_meta) && is_complete_type(dealias(remove_cvref(state_io_meta))))
    {
        return [:state_io_meta:] ::traits::name;
    }

    return "<unknown>";
}
}  // namespace detail
}  // namespace fnfelt::monad::state_io
