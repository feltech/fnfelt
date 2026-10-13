// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/just.hpp
 *
 * Implementation details for lifting a value into the IO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/**
 * Action that yields a stored value each time it is run.
 *
 * The stored value is yielded with the value category of the action: moved out of an rvalue
 * action, copied out of a const or lvalue action. Lvalue and const runs therefore copy the stored
 * value, so `TValue` must be copy constructible for the IO to be runnable repeatedly; rvalue runs
 * move the value out. The default traits require copy constructibility via `validate_just`.
 *
 * @tparam TValue Decayed type of the stored value.
 */
template <class TValue>
struct JustAction
{
    /// The value yielded when the action is run.
    TValue value;

    /**
     * Yields the stored value with the value category of the action.
     *
     * @param self The action to run (explicit object parameter).
     * @return The stored value, forwarded with the value category of `self`.
     */
    constexpr auto operator()(this auto && self)
    {
        return FW(self).value;
    }
};

/**
 * Tombstone returned by `io::just` when the value is invalid.
 *
 * The invalid just is already rejected by a friendly static_assert; this type exists only to keep
 * the return type well-formed and must never be used.
 */
struct JustError
{
};

/**
 * Constructs the diagnostic message for a rejected value.
 *
 * Uses reflection to extract the value type's name and diagnostics when just validation fails. The
 * message includes the IO name, the lifted value's short type name, the specific validation failure
 * reason, and the offending value's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam value_meta Reflection of the (decayed) value type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_just`.
 * @return Formatted error message string.
 */
template <std::meta::info value_meta>
consteval std::string just_error_msg(std::string_view io_name, std::string_view reason)
{
    // The workhorse passes its decayed `value_type` alias by name; dealias so the diagnostic names
    // the underlying type (e.g. "unique_ptr", not "value_type").
    std::meta::info const target_meta = dealias(value_meta);
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO just error: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += "{() => ";
    fnfelt::detail::append_string_view(msg, fnfelt::detail::short_type_name(target_meta));
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(target_meta));
    return msg;
}

/**
 * Traits validating a value lifted by `io::just`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct JustIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the value lifted into the IO.
     *
     * Checks, in order: the value is a complete, non-reference, non-IO type that is copy
     * constructible - the value is stored in the action and copied out on each run of an lvalue
     * IO.
     *
     * @param value_meta Reflection of the (decayed) value type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_just(std::meta::info value_meta)
    {
        value_meta = dealias(value_meta);
        if (!is_complete_type(value_meta))
        {
            return "provided value is not a complete type";
        }
        if (is_reference_type(value_meta))
        {
            return "provided value must not be a reference type";
        }
        value_meta = remove_cvref(value_meta);
        if (detail::is_io(value_meta))
        {
            return "provided value should not be an IO";
        }
        if (!is_copy_constructible_type(value_meta))
        {
            return "provided value must be copy constructible";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
