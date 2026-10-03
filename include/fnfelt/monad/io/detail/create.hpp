// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/create.hpp
 *
 * Implementation-detail traits for `io::create`.
 *
 * Validates an IO's action.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

#include <fnfelt/monad/io/detail.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/**
 * Constructs a detailed error message for an invalid `io::create` action.
 *
 * The message includes the IO name, the specific validation failure reason, and the offending
 * action's display string, so the reason stays readable before any compiler truncation of the
 * message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam action_meta Reflection of the (decayed) action type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_action`.
 * @return Formatted error message string.
 */
template <std::meta::info action_meta>
consteval std::string create_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    append_string_view(msg, io_name);
    msg += ' ';
    msg += "action is invalid: ";
    append_string_view(msg, reason);
    msg += ": ";
    append_string_view(msg, display_string_of(action_meta));
    return msg;
}
}  // namespace detail

/**
 * Traits validating an IO's action.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct CreateIOTraits
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
        if (detail::is_io(action_meta))
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
        std::meta::info const result_meta = invoke_result(action_meta, {});
        if (is_reference_type(result_meta))
        {
            return "provided action returns a reference, return by value instead";
        }
        if (is_void_type(result_meta))
        {
            return "provided action does not return a value";
        }
        if (detail::is_io(result_meta))
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

#include <fnfelt/macros_pop.hpp>
