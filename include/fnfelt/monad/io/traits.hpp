// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
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
        std::meta::info const result_meta = invoke_result(action_meta, {});
        if (is_reference_type(result_meta))
        {
            return "provided action returns a reference, return by value instead";
        }
        if (detail::action_is_async(action_meta))
        {
            std::meta::info const proxy_meta = remove_cvref(result_meta);
            if (!detail::async_proxy_has_value(proxy_meta))
            {
                return "provided action's async proxy does not expose a value_type alias";
            }
            if (is_void_type(detail::async_proxy_value_meta(proxy_meta)))
            {
                return "provided action's async proxy must not produce void";
            }
            return {};
        }
        if (is_void_type(result_meta))
        {
            return "provided action does not return a value";
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

    /**
     * Validate the source IO and continuation function provided to @ref and_then.
     *
     * Checks, in order: the source is an IO producing a value (not void); the continuation function
     * is a complete, non-reference, move constructible class or function pointer type; it accepts
     * the source IO's value either directly or spread from its template arguments; and its result
     * is an IO.
     *
     * This is the overridable default implementation used by `io::and_then`: custom traits may
     * replace it to change which source IO and continuation pairs are accepted.
     *
     * @param io_meta Reflection of the source IO type.
     * @param continuation_meta Reflection of the continuation function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_and_then(
        std::meta::info io_meta, std::meta::info continuation_meta)
    {
        if (!is_io(io_meta))
        {
            return "source IO is not an IO";
        }
        auto const value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(value_meta))
        {
            return "source IO produces no value";
        }
        if (!is_complete_type(continuation_meta))
        {
            return "continuation function is not a complete type";
        }
        if (is_reference_type(continuation_meta))
        {
            return "continuation function must not be a reference type";
        }
        continuation_meta = remove_cvref(continuation_meta);
        if (!is_class_type(continuation_meta) &&
            !(is_pointer_type(continuation_meta) &&
              is_function_type(remove_pointer(continuation_meta))))
        {
            return "continuation function is not a class or function pointer type";
        }
        if (!is_move_constructible_type(continuation_meta))
        {
            return "continuation function must be move constructible";
        }
        if (detail::is_directly_invocable(continuation_meta, value_meta))
        {
            std::meta::info result_meta = invoke_result(continuation_meta, {value_meta});
            if (!is_io(result_meta))
            {
                return "continuation function must return an IO";
            }
            return {};
        }
        if (detail::is_spread_invocable(continuation_meta, value_meta))
        {
            std::meta::info result_meta =
                invoke_result(continuation_meta, template_arguments_of(dealias(value_meta)));
            if (!is_io(result_meta))
            {
                return "continuation function must return an IO";
            }
            return {};
        }
        return "continuation function does not accept the source IO's value";
    }

    /**
     * Validate the source IO and transformer function provided to @ref transform.
     *
     * Checks, in order: the source is an IO producing a value (not void); the transformer function
     * is a complete, non-reference, move constructible class or function pointer type; it accepts
     * the source IO's value either directly or spread from its template arguments; and its result
     * is a valid plain value (not void, not a reference, not an IO, and move constructible).
     *
     * This is the overridable default implementation used by `io::transform`: custom traits may
     * replace it to change which source IO and transformer pairs are accepted.
     *
     * @param io_meta Reflection of the source IO type.
     * @param transformer_meta Reflection of the transformer function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_transform(
        std::meta::info io_meta, std::meta::info transformer_meta)
    {
        if (!is_io(io_meta))
        {
            return "source IO is not an IO";
        }
        auto const value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(value_meta))
        {
            return "source IO produces no value";
        }
        if (!is_complete_type(transformer_meta))
        {
            return "transformer function is not a complete type";
        }
        if (is_reference_type(transformer_meta))
        {
            return "transformer function must not be a reference type";
        }
        transformer_meta = remove_cvref(transformer_meta);
        if (!is_class_type(transformer_meta) &&
            !(is_pointer_type(transformer_meta) &&
              is_function_type(remove_pointer(transformer_meta))))
        {
            return "transformer function is not a class or function pointer type";
        }
        if (!is_move_constructible_type(transformer_meta))
        {
            return "transformer function must be move constructible";
        }
        auto const validate_result = [](std::meta::info result_meta) consteval -> std::string_view
        {
            if (is_void_type(result_meta))
            {
                return "transformer function does not return a value";
            }
            if (is_reference_type(result_meta))
            {
                return "transformer function returns a reference, return by value instead";
            }
            if (is_io(result_meta))
            {
                return "transformer function should not return an IO";
            }
            if (!is_move_constructible_type(result_meta))
            {
                return "transformer function returns a non-movable type";
            }
            return {};
        };
        if (detail::is_directly_invocable(transformer_meta, value_meta))
        {
            return validate_result(invoke_result(transformer_meta, {value_meta}));
        }
        if (detail::is_spread_invocable(transformer_meta, value_meta))
        {
            return validate_result(
                invoke_result(transformer_meta, template_arguments_of(dealias(value_meta))));
        }
        return "transformer function does not accept the source IO's value";
    }

    /**
     * Validate the IO-wrapped function and value provided to @ref ap.
     *
     * Checks that both reflections are IO specialisations, and that the IO-wrapped function's
     * callable accepts the IO-wrapped value's value either directly or spread from the value's
     * template arguments (direct invocation wins), and that the application returns a value (not
     * void).
     *
     * `is_invocable_type` bare-type (prvalue) semantics match the runtime invocation
     * `std::invoke(std::move(fn), std::move(value))` / `std::apply(std::move(fn), ...)` on moved
     * locals (unlike `detail::is_directly_invocable`, which tests a const lvalue receiver).
     *
     * This is the overridable default implementation used by `io::ap`: custom traits may replace
     * it to change which IO-wrapped function/value pairs are accepted.
     *
     * @param fn_io_meta Reflection of the function IO type.
     * @param value_io_meta Reflection of the value IO type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_ap(
        std::meta::info fn_io_meta, std::meta::info value_io_meta)
    {
        if (!is_io(fn_io_meta))
        {
            return "IO-wrapped function is not an IO";
        }
        if (!is_io(value_io_meta))
        {
            return "IO-wrapped value is not an IO";
        }
        std::meta::info const fn_value_meta = detail::io_value_meta(fn_io_meta);
        std::meta::info const val_value_meta = detail::io_value_meta(value_io_meta);
        if (is_invocable_type(fn_value_meta, {val_value_meta}))
        {
            if (is_void_type(invoke_result(fn_value_meta, {val_value_meta})))
            {
                return "IO-wrapped function's callable does not return a value";
            }
            return {};
        }
        if (detail::is_spread_invocable_as(fn_value_meta, val_value_meta))
        {
            if (is_void_type(
                    invoke_result(fn_value_meta, template_arguments_of(dealias(val_value_meta)))))
            {
                return "IO-wrapped function's callable does not return a value";
            }
            return {};
        }
        return "IO-wrapped function's callable does not accept the IO-wrapped value's value";
    }
};
}  // namespace fnfelt::monad::io
