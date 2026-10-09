// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file detail.hpp
 *
 * Implementation details for the IO monad.
 *
 * Reflection helper predicates and the bind action used by IO::bind.
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
}  // namespace detail
}  // namespace fnfelt::monad::io
