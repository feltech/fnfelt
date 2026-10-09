// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file traits.hpp
 *
 * Traits for the IO monad, used in diagnostics.
 */
#pragma once

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>

namespace fnfelt::monad::io
{
/**
 * Traits for the IO monad, used in diagnostics.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct IOTraits
{
    /// Name of this IO instantiation, used in compile-time diagnostic messages.
    // A string_view over a static string is constant-initialised and cannot throw.
    // NOLINTNEXTLINE(*-statically-constructed-objects)
    static constexpr std::string_view name{name_cstr};

    /**
     * Check whether a type is a (cv-ref-stripped) specialisation of IO, with any traits.
     *
     * @param type_meta Reflection of the type.
     * @return True if the type is an IO specialisation.
     */
    static consteval bool is_io(std::meta::info type_meta)
    {
        return detail::is_io(type_meta);
    }

    /**
     * Validate the action given to an IO.
     *
     * @param action_meta Reflection of the action type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        action_meta = dealias(action_meta);
        if (!is_complete_type(action_meta))
        {
            return "provided action is not a complete type";
        }
        if (is_reference_type(action_meta))
        {
            return "provided action must not be a reference type";
        }
        if (is_io(action_meta))
        {
            return "provided action is already an IO, pass its action instead";
        }
        if (!is_class_type(action_meta) &&
            !(is_pointer_type(action_meta) && is_function_type(remove_pointer(action_meta))))
        {
            return "provided action is not a class or function pointer type";
        }
        if (!is_move_constructible_type(action_meta))
        {
            return "provided action must be move constructible";
        }
        if (!is_invocable_type(action_meta, {}))
        {
            return "provided action is not callable with no arguments";
        }
        if (!is_invocable_type(add_const(action_meta), {}))
        {
            return "provided action is only callable when non-const";
        }
        std::meta::info result_meta = invoke_result(action_meta, {});
        if (is_void_type(result_meta))
        {
            return "provided action does not return a value";
        }
        if (is_reference_type(result_meta))
        {
            return "provided action returns a reference, return by value instead";
        }
        if (is_io(result_meta))
        {
            return "provided action should not return an IO";
        }
        if (!is_move_constructible_type(result_meta))
        {
            return "provided action returns a non-movable type";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::io
