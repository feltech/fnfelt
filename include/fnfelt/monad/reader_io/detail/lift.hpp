// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/lift.hpp
 *
 * Implementation details for lifting an IO into the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
// The definitions backing `fnfelt::monad::io::detail::io_value_meta` (which calls `action_is_async`
// from) live in async.hpp; including only io's detail.hpp would hard-error with "action_is_async
// used before its definition".
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{

/**
 * Tombstone returned by `reader_io::lift` when the source is invalid.
 *
 * The invalid lift is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct LiftError
{
};

/**
 * Action that yields its embedded IO, accepting and discarding the state.
 *
 * The embedded IO was already fully constructed at lift time and carries no state parameter, so
 * the state has no effect on which IO is yielded: a lift is a constant embedding of a
 * state-independent computation into a stateful context.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TIO The embedded IO type.
 */
template <class TState, class TIO>
struct LiftAction
{
    /// The embedded IO.
    TIO io;

    /**
     * Yield the embedded IO for the given state.
     *
     * The explicit object parameter is not const, so an rvalue proxy (the only consumable one)
     * moves the embedded IO out; a copyable embedded IO is copied per lvalue build by
     * `ReaderIO::build`'s reachability check.
     *
     * @param self The action to run (explicit object parameter).
     * @param state The state (ignored).
     * @return The embedded IO, copied or moved according to the run's value category.
     */
    constexpr auto operator()(this auto && self, [[maybe_unused]] TState state) -> TIO
    {
        return FW(self).io;
    }
};

/**
 * Constructs the diagnostic message for a rejected lift source.
 *
 * The message includes the ReaderIO name, the source's signature, the specific validation failure
 * reason, and the offending source's display string, so the reason stays readable before any
 * compiler truncation of the message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_meta Reflection of the (decayed) source type.
 * @param name Display name of the resulting ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_lift`.
 * @return Formatted error message string.
 */
template <std::meta::info source_meta>
consteval std::string lift_error_msg(std::string_view name, std::string_view reason)
{
    // The source may be a non-IO when the not-an-IO check fails, so it is named by an IO name
    // when it is an IO and by its short name otherwise.
    std::string_view const source_name = io::detail::is_io(source_meta)
        ? io::detail::maybe_io_name<source_meta>()
        : fnfelt::detail::short_type_name(source_meta);
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " lift error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += '{';
    fnfelt::detail::append_string_view(msg, source_name);
    msg += "()}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(source_meta));
    return msg;
}

/**
 * Traits validating the source IO provided to `reader_io::lift`.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct LiftReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Validate the source IO provided to `reader_io::lift`.
     *
     * The source must be an IO (not already a ReaderIO: a ReaderIO composes by passing its action
     * rather than being lifted) that produces a value, so the embedded IO can feed continuation
     * composition later.
     *
     * @param source_meta Reflection of the (decayed) source type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_lift(std::meta::info source_meta)
    {
        if (detail::is_reader_io(source_meta))
        {
            return "provided source is already a ReaderIO, pass it directly";
        }
        if (!io::detail::is_io(source_meta))
        {
            return "provided source is not an IO";
        }
        if (is_void_type(io::detail::io_value_meta(source_meta)))
        {
            return "provided source IO produces no value";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
