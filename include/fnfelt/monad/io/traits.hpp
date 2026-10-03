// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file traits.hpp
 *
 * Default traits for the IO monad, naming the instantiation used in diagnostics.
 */
#pragma once

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>

namespace fnfelt::monad::io
{
/**
 * Default traits for the IO monad, naming the instantiation used in diagnostics.
 *
 * Publicly usable to name an IO (e.g. `IOTraits<"FileIO"_ss>`), or override with custom traits to
 * change which actions and continuations are accepted.
 *
 * Checks, in order: the action is a complete, non-reference, non-IO class or function pointer type;
 * it is move constructible; it is callable with no arguments, including when const; and its
 * invocation returns a movable, non-reference, non-IO, non-void value. Also validates bind source
 * IO and continuation pairs (see `validate_bind`), and applicative function/value IO pairs (see
 * `validate_ap`).
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
     * Perform the validation.
     *
     * @param action_meta Reflection of the action type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        action_meta = dealias(action_meta);
        if (!is_complete_type(action_meta))
        {
            return "is not a complete type";
        }
        if (is_reference_type(action_meta))
        {
            return "must not be a reference type";
        }
        if (is_io(action_meta))
        {
            return "is already an IO, pass its action instead";
        }
        if (!is_class_type(action_meta) &&
            !(is_pointer_type(action_meta) && is_function_type(remove_pointer(action_meta))))
        {
            return "is not a class or function pointer type";
        }
        if (!is_move_constructible_type(action_meta))
        {
            return "must be move constructible";
        }
        if (!is_invocable_type(action_meta, {}))
        {
            return "is not callable with no arguments";
        }
        if (!is_invocable_type(add_const(action_meta), {}))
        {
            return "is only callable when non-const";
        }
        std::meta::info result_meta = invoke_result(action_meta, {});
        if (is_void_type(result_meta))
        {
            return "does not return a value";
        }
        if (is_reference_type(result_meta))
        {
            return "returns a reference, return by value instead";
        }
        if (is_io(result_meta))
        {
            return "should not return an IO";
        }
        if (!is_move_constructible_type(result_meta))
        {
            return "returns a non-movable type";
        }
        return {};
    }

    /**
     * Perform the validation.
     *
     * Checks, in order: the source is an IO producing a value (not void); the kleisli is a
     * complete, non-reference, move constructible class or function pointer type; it is callable
     * with the source value either directly or spread from its template arguments; and its result
     * is an IO.
     *
     * This is the overridable default implementation used by `io::bind`: custom traits may
     * replace it to change which source IO and continuation pairs are accepted.
     *
     * @param io_meta Reflection of the source IO type.
     * @param kleisli_meta Reflection of the kleisli type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_bind(
        std::meta::info io_meta, std::meta::info kleisli_meta)
    {
        if (!is_io(io_meta))
        {
            return "is not an IO";
        }
        auto const value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(value_meta))
        {
            return "source produces no value";
        }
        if (!is_complete_type(kleisli_meta))
        {
            return "is not a complete type";
        }
        if (is_reference_type(kleisli_meta))
        {
            return "must not be a reference type";
        }
        kleisli_meta = remove_cvref(kleisli_meta);
        if (!is_class_type(kleisli_meta) &&
            !(is_pointer_type(kleisli_meta) && is_function_type(remove_pointer(kleisli_meta))))
        {
            return "is not a class or function pointer type";
        }
        if (!is_move_constructible_type(kleisli_meta))
        {
            return "must be move constructible";
        }
        if (detail::is_directly_invocable(kleisli_meta, value_meta))
        {
            std::meta::info result_meta = invoke_result(kleisli_meta, {value_meta});
            if (!is_io(result_meta))
            {
                return "must return an IO";
            }
            return {};
        }
        if (detail::is_spread_invocable(kleisli_meta, value_meta))
        {
            std::meta::info result_meta =
                invoke_result(kleisli_meta, template_arguments_of(dealias(value_meta)));
            if (!is_io(result_meta))
            {
                return "must return an IO";
            }
            return {};
        }
        return "is not callable with the value";
    }

    /**
     * Perform the validation.
     *
     * Checks that both reflections are IO specialisations, and that the function IO's value is
     * callable with the value IO's value as its single argument, and that the application returns
     * a value (not void).
     *
     * `is_invocable_type` bare-type (prvalue) semantics match the runtime invocation
     * `std::invoke(std::move(fn), std::move(value))` on moved locals (unlike
     * `detail::is_directly_invocable`, which tests a const lvalue receiver).
     *
     * This is the overridable default implementation used by `io::ap`: custom traits may replace
     * it to change which function/value IO pairs are accepted.
     *
     * @param fn_io_meta Reflection of the function IO type.
     * @param value_io_meta Reflection of the value IO type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_ap(
        std::meta::info fn_io_meta, std::meta::info value_io_meta)
    {
        if (!is_io(fn_io_meta) || !is_io(value_io_meta))
        {
            return "is not an IO";
        }
        if (!is_invocable_type(
                detail::io_value_meta(fn_io_meta), {detail::io_value_meta(value_io_meta)}))
        {
            return "function IO's value is not callable with the value IO's value";
        }
        std::meta::info result_meta = invoke_result(
            detail::io_value_meta(fn_io_meta), {detail::io_value_meta(value_io_meta)});
        if (is_void_type(result_meta))
        {
            return "does not return a value";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::io
