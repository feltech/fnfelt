// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/just.hpp
 *
 * Implementation details for lifting a value into the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail/just.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/lift.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{

/**
 * Tombstone returned by `reader_io::just` when the value is invalid.
 *
 * The invalid just is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct JustError
{
};

/**
 * Constructs the diagnostic message for a rejected value.
 *
 * The message includes the ReaderIO name, the lifted value's short type name, the specific
 * validation failure reason, and the offending value's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam value_meta Reflection of the (decayed) value type.
 * @param name Display name of the resulting ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_just`.
 * @return Formatted error message string.
 */
template <std::meta::info value_meta>
consteval std::string just_error_msg(std::string_view name, std::string_view reason)
{
    // The value is passed by its decayed type; dealias so the diagnostic names the underlying
    // type (e.g. "unique_ptr", not "value_type").
    std::meta::info const target_meta = dealias(value_meta);
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " just error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += "{() => ";
    fnfelt::detail::append_string_view(msg, fnfelt::detail::short_type_name(target_meta));
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(target_meta));
    return msg;
}

/**
 * Traits validating a value lifted by `reader_io::just`.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct JustReaderIOTraits : LiftReaderIOTraits<name_cstr>
{
    /**
     * Validate the value lifted into the ReaderIO.
     *
     * Delegates to `fnfelt::monad::io::detail::JustIOTraits<name_cstr>::validate_just`: the value
     * ends up inside the embedded IO's action, so the same contract applies - a complete,
     * non-reference, non-IO type that is copy constructible, since the embedded IO is copied out on
     * each lvalue run.
     *
     * @param value_meta Reflection of the (decayed) value type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_just(std::meta::info value_meta)
    {
        return io::detail::JustIOTraits<name_cstr>::validate_just(value_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io
