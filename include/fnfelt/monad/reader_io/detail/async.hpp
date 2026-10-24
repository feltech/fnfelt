// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/async.hpp
 *
 * Implementation details for lifting the ReaderIO monad's asynchronous leaves.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/async.hpp>
// The IO's async validator (`CreateAsyncIOTraits::validate_create_async`).
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
 * Tombstone returned by `reader_io::create_async` when the coroutine/argument combination is
 * invalid.
 *
 * The invalid create_async is already rejected by a friendly static_assert; this type exists only
 * to keep the return type well-formed and must never be used.
 */
struct CreateAsyncError
{
};

/**
 * Constructs the diagnostic message for a rejected async function.
 *
 * The message includes the ReaderIO name, the specific validation failure reason, and the
 * offending function's display string, so the reason stays readable before any compiler truncation
 * of the message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam fn_meta Reflection of the (decayed) async function type.
 * @param name Display name of the resulting ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_create_async`.
 * @return Formatted error message string.
 */
template <std::meta::info fn_meta>
consteval std::string create_async_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " async action is invalid: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(fn_meta));
    return msg;
}

/**
 * Action that yields an asynchronous leaf IO built from the stored coroutine function and
 * arguments, ignoring the state except as the first bound argument.
 *
 * Invoking the action re-enters `io::create_async` with the state as the coroutine's first bound
 * argument (after libfork's own self argument) followed by the stored arguments: the leaf is
 * materialised per invocation, so the bound state feeds the coroutine's declared
 * `(auto, TState, TArgs...) -> lf::task` signature. The action's value category is forwarded to
 * the stored function and arguments, so an rvalue run moves them.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TFn The stateless coroutine function type.
 * @tparam TArgs The stored argument types (decayed).
 */
template <class TState, class TFn, class... TArgs>
struct CreateAsyncAction
{
    /// Stateless coroutine function.
    TFn fn;
    /// Stored arguments spread after the state into the coroutine.
    std::tuple<TArgs...> args;

    /**
     * Materialise the asynchronous leaf IO for the bound state.
     *
     * @param self The action to run (explicit object parameter).
     * @param state The environment for this invocation.
     * @return The async leaf IO, with the state spread as the coroutine's first bound argument.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return std::apply(
            [&](auto &&... stored)
            { return io::create_async(FW(self).fn, std::move(state), FW(stored)...); },
            FW(self).args);
    }
};

/**
 * Traits validating the state-first coroutine function and arguments provided to
 * `reader_io::create_async`.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct CreateAsyncReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Validate the state-first coroutine function and its stored arguments.
     *
     * Delegates to
     * `fnfelt::monad::io::detail::CreateAsyncIOTraits<name_cstr>::validate_create_async` on the
     * validation tuple `(TState, TArgs...)` - the coroutine's declared
     * `(auto, TState, TArgs...) -> lf::task` signature. The IO validator guards the function's
     * completenes, statelessness, constant-expression usability and invocability (with a
     * placeholder for libfork's generated self argument); the reader validator adds the state
     * completeness guard, since the IO invocation query is a hard, non-catchable gcc error for
     * incomplete argument types.
     *
     * @param fn_meta Reflection of the (decayed) async function type.
     * @param arg_tuple_meta Reflection of `std::tuple<TState, TArgs...>`.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_create_async(
        std::meta::info fn_meta, std::meta::info arg_tuple_meta)
    {
        // An alias reflection reports has_template_arguments false and makes
        // template_arguments_of throw, so dealias before extracting the state type.
        arg_tuple_meta = dealias(arg_tuple_meta);
        // The state is the coroutine's first bound argument: the io invocation query hard-errors
        // for incomplete argument types, so guard it before delegating.
        std::meta::info const state_meta = template_arguments_of(arg_tuple_meta)[0];
        if (is_reference_type(state_meta))
        {
            return "provided state must not be a reference type";
        }
        if (!is_complete_type(remove_cvref(state_meta)))
        {
            return "provided state is not a complete type";
        }
        if (!is_move_constructible_type(state_meta))
        {
            return "provided state must be move constructible";
        }
        return io::detail::CreateAsyncIOTraits<name_cstr>::validate_create_async(
            fn_meta, arg_tuple_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
