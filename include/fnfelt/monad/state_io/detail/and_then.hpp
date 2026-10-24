// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/and_then.hpp
 *
 * Implementation details for the StateIO monad's monadic composition.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/detail/type_names.hpp>
// The action delegates the composition to the inner IO's and_then, whose definition (and the IO
// member's deduction target) is required here.
#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/state_io/detail.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{

/**
 * Tombstone returned by `state_io::and_then` when the continuation is invalid.
 *
 * The invalid and_then is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct AndThenError
{
};

/**
 * Constructs the diagnostic message for a rejected composition.
 *
 * The message includes the StateIO name, the composition signature
 * `<name>{<source name>() => <continuation short name>}`, the specific validation failure reason,
 * and the offending continuation's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_meta Reflection of the source StateIO type.
 * @tparam continuation_meta Reflection of the (decayed) continuation type.
 * @param name Display name of the resulting StateIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_and_then`.
 * @return Formatted error message string.
 */
template <std::meta::info source_meta, std::meta::info continuation_meta>
consteval std::string and_then_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " and_then error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += '{';
    fnfelt::detail::append_string_view(msg, maybe_state_io_name<source_meta>());
    msg += "() => ";
    fnfelt::detail::append_string_view(
        msg, fnfelt::detail::short_type_name(dealias(continuation_meta)));
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(continuation_meta));
    return msg;
}

/**
 * Invoke a StateIO continuation with updated state.
 *
 * @tparam TState Environment type.
 * @tparam TContinuation StateIO-producing callable type.
 */
template <class TState, class TContinuation>
struct BoundContinuation
{
    /// StateIO-producing callable.
    TContinuation continuation;

    /**
     * Consume the continuation and bind its returned StateIO to updated state.
     *
     * @param self Bound continuation to invoke.
     * @param value Produced source value.
     * @return Materialised inner IO.
     */
    constexpr auto operator()(this auto && self, auto && value)
    {
        return bind_state<TState>(
            fnfelt::detail::invoke_dispatch(FW(self).continuation, FW(value).first),
            FW(value).second);
    }
};

/**
 * Thread state through a source and a logical-value continuation inside the IO tree.
 *
 * @tparam TState The StateIO's state type.
 * @tparam TSource Source StateIO to invoke.
 * @tparam TContinuation Continuation taking the source's value and returning a StateIO.
 */
template <class TState, class TSource, class TContinuation>
struct AndThenAction
{
    /// Source StateIO to invoke.
    TSource source;
    /// Continuation applied to the source's value.
    TContinuation continuation;

    /**
     * Invoke the source and pass its updated state to the continuation's computation.
     *
     * @param self The action to run (explicit object parameter).
     * @param state The environment for this invocation.
     * @return The composed IO.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        auto source_io = bind_state<TState>(FW(self).source, std::move(state));
        return std::move(source_io).and_then(
            BoundContinuation<TState, TContinuation>{FW(self).continuation});
    }
};

/**
 * Traits validating the source StateIO and continuation function provided to
 * `state_io::and_then`.
 *
 * @tparam name_cstr Null-terminated name of the StateIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct AndThenStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Validate the source StateIO and continuation function.
     *
     * The source must be a StateIO whose action produces a
     * value; the continuation must have the stored-callable shape shared with the IO monad, be
     * invocable (directly or spread) with the source's produced value, and return a StateIO over
     * the same state type whose action produces a value.
     *
     * @param source_meta Reflection of the source StateIO type.
     * @param continuation_meta Reflection of the (decayed) continuation type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_and_then(
        std::meta::info source_meta, std::meta::info continuation_meta)
    {
        if (!detail::is_state_io(source_meta))
        {
            return "source is not a StateIO";
        }
        std::meta::info const state_meta = detail::state_io_state_meta(source_meta);
        std::meta::info const source_value_meta = detail::state_io_value_meta(source_meta);
        if (is_void_type(source_value_meta))
        {
            return "source StateIO produces no value";
        }
        switch (fnfelt::detail::callable_shape_of(continuation_meta))
        {
            case fnfelt::detail::CallableShape::incomplete:
                return "continuation function is not a complete type";
            case fnfelt::detail::CallableShape::reference:
                return "continuation function must not be a reference type";
            case fnfelt::detail::CallableShape::not_class_or_function_pointer:
                return "continuation function is not a class or function pointer type";
            case fnfelt::detail::CallableShape::not_movable:
                return "continuation function must be move constructible";
            case fnfelt::detail::CallableShape::valid:
                break;
        }
        std::meta::info const continuation_meta_bare = dealias(remove_cvref(continuation_meta));
        if (!fnfelt::detail::is_dispatch_invocable(continuation_meta_bare, source_value_meta))
        {
            return "continuation function does not accept the source StateIO's value";
        }
        std::meta::info const result_meta =
            fnfelt::detail::dispatch_result_meta(continuation_meta_bare, source_value_meta);
        if (!detail::is_state_io(result_meta))
        {
            return "continuation function must return a StateIO";
        }
        if (!is_same_type(detail::state_io_state_meta(result_meta), state_meta))
        {
            return "continuation function must return a StateIO with the same state type";
        }
        if (is_void_type(detail::state_io_value_meta(result_meta)))
        {
            return "continuation function must return a StateIO that produces a value";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
