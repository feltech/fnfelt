// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/ap.hpp
 *
 * Implementation details for the ReaderIO monad's applicative application.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <utility>

#include <fnfelt/detail/type_names.hpp>
// The action delegates the application to io::ap, whose definition is required here.
#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/detail.hpp>
// The IO's ap validator (`ApIOTraits::validate_ap`).
#include <fnfelt/monad/io/detail/ap.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{

/**
 * Tombstone returned by `reader_io::ap` when an argument source is invalid.
 *
 * The invalid application is already rejected by a friendly static_assert; this type exists only
 * to keep the return type well-formed and must never be used.
 */
struct ApError
{
};

/**
 * Constructs the diagnostic message for a rejected application.
 *
 * The message includes the ReaderIO name, the application signature
 * `<name>{(<fn source name>()(<value source name>()) => <unknown>}` (the application's result is
 * not determinable without a state), the specific validation failure reason, and the offending
 * source's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam fn_meta Reflection of the function source type.
 * @tparam value_meta Reflection of the value source type.
 * @param name Display name of the resulting ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_ap`.
 * @return Formatted error message string.
 */
template <std::meta::info fn_meta, std::meta::info value_meta>
consteval std::string ap_error_msg(std::string_view name, std::string_view reason)
{
    std::meta::info const target_meta = (!is_reader_io(fn_meta) || !is_reader_io(value_meta))
        ? (is_reader_io(fn_meta) ? value_meta : fn_meta)
        : fn_meta;

    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " ap error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += "{(";
    fnfelt::detail::append_string_view(msg, maybe_reader_io_name<fn_meta>());
    msg += "()";
    msg += "(";
    fnfelt::detail::append_string_view(msg, maybe_reader_io_name<value_meta>());
    msg += "()";
    msg += ") => ";
    msg += "<unknown>";
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(target_meta));
    return msg;
}

/**
 * Action that invokes a function ReaderIO with a copy of the state and a value ReaderIO with the
 * state, then applies the function's produced callable to the value's produced value.
 *
 * Exactly one state copy: the function source gets its own copy and the value source gets the
 * (moved) bound state. The application is delegated to `io::ap`, preserving the IO's concurrent
 * asynchronous application. The two sources are forwarded with the composition's value category,
 * so an lvalue run copies and an rvalue run moves; replays work.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TFnSource ReaderIO whose produced IO yields the callable.
 * @tparam TValueSource ReaderIO whose produced IO yields the callable's argument.
 */
template <class TState, class TFnSource, class TValueSource>
struct ApAction
{
    /// ReaderIO whose produced IO yields the callable.
    TFnSource fn_source;
    /// ReaderIO whose produced IO yields the callable's argument.
    TValueSource value_source;

    /**
     * Invoke both sources and apply the produced callable to the produced value.
     *
     * @param self The action to run (explicit object parameter).
     * @param state The environment for this invocation.
     * @return The application's IO.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        auto fn_state = state;
        return io::ap(
            FW(self).fn_source.build(std::move(fn_state)).io(),
            FW(self).value_source.build(std::move(state)).io());
    }
};

/**
 * Traits validating the function and value sources provided to `reader_io::ap`.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct ApReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Validate the two sources provided to `reader_io::ap`.
     *
     * Both must be ReaderIOs typed over the same copy-constructible state. The callable/value
     * contract between them is delegated to the IO's ap validator on the reflected inner IOs (the
     * sources' produced values, unwrapped through async proxies).
     *
     * @param fn_meta Reflection of the function source type.
     * @param value_meta Reflection of the value source type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_ap(
        std::meta::info fn_meta, std::meta::info value_meta)
    {
        if (!detail::is_reader_io(fn_meta))
        {
            return "function source is not a ReaderIO";
        }
        if (!detail::is_reader_io(value_meta))
        {
            return "value source is not a ReaderIO";
        }
        std::meta::info const fn_state_meta = detail::reader_io_state_meta(fn_meta);
        std::meta::info const value_state_meta = detail::reader_io_state_meta(value_meta);
        if (!is_same_type(fn_state_meta, value_state_meta))
        {
            return "function source and value source must have the same state type";
        }
        if (!is_copy_constructible_type(fn_state_meta))
        {
            return "state must be copy constructible";
        }
        std::meta::info const fn_io_meta = detail::reader_io_io_meta(fn_meta);
        std::meta::info const value_io_meta = detail::reader_io_io_meta(value_meta);
        return io::detail::ApIOTraits<>::validate_ap(fn_io_meta, value_io_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
