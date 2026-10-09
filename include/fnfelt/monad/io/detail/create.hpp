// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/create.hpp
 *
 * Implementation-detail traits for `io::create` and asynchronous proxies.
 *
 * Validates an IO's action, including the executable shape of any asynchronous proxy it produces.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <vector>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/fwd.hpp>

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
 * Traits validating an IO's action and the executable shape of any asynchronous proxy it produces.
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
     * Validate the executable shape of an asynchronous proxy.
     *
     * Detection as asynchronous (deriving @ref AsyncProxyTag with a non-void `value_type` alias) is
     * not enough to run a proxy: execution additionally requires the stored arguments and the
     * stateless coroutine entry. Acceptance therefore checks that the proxy:
     *
     * - exposes an unambiguous non-void `value_type` alias;
     * - stores its arguments in a non-static data member named `arg` whose type is a `std::tuple`
     *   specialisation, whose elements are spread into `fn` at run time;
     * - exposes a static data member named `fn`, callable as `fn(self, args...)` with a
     *   libfork-generated first argument, returning an `lf::task` whose value matches `value_type`.
     *
     * The unwrapped value must also satisfy the plain-result rules: it must not be a reference or
     * an IO, and it must be movable. libfork returns it through an `lf::task`, whose `returnable`
     * concept requires `std::movable` (move constructible and move assignable).
     *
     * This is the overridable default implementation called by `validate_action`: custom traits may
     * replace it to change which executable proxies are accepted.
     *
     * @param proxy_meta Reflection of the async proxy type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_async_proxy(std::meta::info proxy_meta)
    {
        proxy_meta = dealias(remove_cvref(proxy_meta));
        if (!detail::async_proxy_has_value(proxy_meta))
        {
            return "provided action's async proxy does not expose a value_type alias";
        }
        if (detail::async_proxy_ambiguous_value(proxy_meta))
        {
            return "provided action's async proxy exposes ambiguous value_type aliases";
        }
        std::meta::info const value_meta = detail::async_proxy_value_meta(proxy_meta);
        if (is_void_type(value_meta))
        {
            return "provided action's async proxy must not produce void";
        }
        if (is_reference_type(value_meta))
        {
            return "provided action's async proxy value must not be a reference type";
        }
        if (detail::is_io(value_meta))
        {
            return "provided action's async proxy value should not be an IO";
        }
        if (!is_complete_type(value_meta))
        {
            return "provided action's async proxy value is not a complete type";
        }
        if (!is_move_constructible_type(value_meta) || !is_move_assignable_type(value_meta))
        {
            return "provided action's async proxy value must be movable";
        }
        // The stored argument tuple: a non-static data member named "arg".
        std::meta::info arg_type = ^^void;
        for (std::meta::info const member :
             nonstatic_data_members_of(proxy_meta, std::meta::access_context::unprivileged()))
        {
            if (has_identifier(member) && identifier_of(member) == "arg")
            {
                arg_type = dealias(type_of(member));
            }
        }
        if (is_void_type(arg_type))
        {
            return "provided action's async proxy must expose an arg member";
        }
        if (!has_template_arguments(arg_type) || template_of(arg_type) != ^^std::tuple)
        {
            return "provided action's async proxy arg member must be a std::tuple";
        }
        // The stateless coroutine entry: a static data member named "fn".
        std::meta::info fn_type = ^^void;
        for (std::meta::info const member :
             static_data_members_of(proxy_meta, std::meta::access_context::unprivileged()))
        {
            if (has_identifier(member) && identifier_of(member) == "fn")
            {
                fn_type = remove_cvref(type_of(member));
            }
        }
        if (is_void_type(fn_type))
        {
            return "provided action's async proxy must expose a static fn member";
        }
        // libfork injects a `self` first argument, so a placeholder stands in for it, followed by
        // the stored tuple's element types.
        std::vector<std::meta::info> args{^^int};
        for (std::meta::info const element : template_arguments_of(arg_type))
        {
            args.push_back(element);
        }
        if (!is_complete_type(fn_type) || !is_invocable_type(fn_type, args))
        {
            return "provided action's async proxy fn must accept the stored arguments";
        }
        std::meta::info const result = dealias(invoke_result(fn_type, args));
        // `template_of` comparison is the same idiom as `is_io`; `lf::task` is an inline-namespace
        // name.
        if (!has_template_arguments(result) || template_of(result) != ^^lf::task)
        {
            return "provided action's async proxy fn must return an lf::task";
        }
        if (!is_same_type(template_arguments_of(result)[0], value_meta))
        {
            return "provided action's async proxy fn result does not match its value_type";
        }
        return {};
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
        if (detail::action_is_async(action_meta))
        {
            return validate_async_proxy(remove_cvref(result_meta));
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
