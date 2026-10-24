// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/and_then.hpp
 *
 * Implementation details for the ReaderIO monad's monadic composition.
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
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{

/**
 * Tombstone returned by `reader_io::and_then` when the continuation is invalid.
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
 * The message includes the ReaderIO name, the composition signature
 * `<name>{<source name>() => <continuation short name>}`, the specific validation failure reason,
 * and the offending continuation's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_meta Reflection of the source ReaderIO type.
 * @tparam continuation_meta Reflection of the (decayed) continuation type.
 * @param name Display name of the resulting ReaderIO (from traits).
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
    fnfelt::detail::append_string_view(msg, maybe_reader_io_name<source_meta>());
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
 * Invoke a Reader continuation with its retained environment.
 *
 * @tparam TState Environment type.
 * @tparam TContinuation Reader-producing callable type.
 */
template <class TState, class TContinuation>
struct BoundContinuation
{
    /// Reader-producing callable.
    TContinuation continuation;
    /// Environment retained for its result.
    TState state;

    /**
     * Consume the continuation and bind its returned reader.
     *
     * @param self Bound continuation to invoke.
     * @param value Produced source value.
     * @return Materialised inner IO.
     */
    constexpr auto operator()(this auto && self, auto && value)
    {
        return fnfelt::detail::invoke_dispatch(FW(self).continuation, FW(value))
            .build(FW(self).state)
            .io();
    }
};

/**
 * Action that invokes a source ReaderIO with the state, feeds its value to a continuation, and
 * invokes the continuation's ReaderIO with a second copy of the same state, yielding the IO to
 * run.
 *
 * The state copy for the continuation is made before the source runs, so a source mutating its
 * own copy is not seen by the continuation. The source and the continuation are forwarded with
 * the composition's value category, so an lvalue run copies and an rvalue run moves; replays
 * work. The final stage delegates to the inner IO's and_then, so the inner run keeps the IO
 * monad's direct-and-spread dispatch and async semantics.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TSource Source ReaderIO to invoke.
 * @tparam TContinuation Continuation taking the source's value and returning a ReaderIO.
 */
template <class TState, class TSource, class TContinuation>
struct AndThenAction
{
    /// Source ReaderIO to invoke.
    TSource source;
    /// Continuation applied to the source's value.
    TContinuation continuation;

    /**
     * Invoke the source with a copy of the state, then the continuation's ReaderIO with a second
     * copy of the same state.
     *
     * @param self The action to run (explicit object parameter).
     * @param state The environment for this invocation.
     * @return The composed IO.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        auto source_state = state;
        auto cont_state = std::move(state);
        auto source_io = FW(self).source.build(std::move(source_state)).io();
        return std::move(source_io).and_then(
            BoundContinuation<TState, TContinuation>{FW(self).continuation, std::move(cont_state)});
    }
};

/**
 * Traits validating the source ReaderIO and continuation function provided to
 * `reader_io::and_then`.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct AndThenReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Validate the source ReaderIO and continuation function.
     *
     * The source must be a ReaderIO whose state is copy constructible and whose action produces a
     * value; the continuation must have the stored-callable shape shared with the IO monad, be
     * invocable (directly or spread) with the source's produced value, and return a ReaderIO over
     * the same state type whose action produces a value.
     *
     * @param source_meta Reflection of the source ReaderIO type.
     * @param continuation_meta Reflection of the (decayed) continuation type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_and_then(
        std::meta::info source_meta, std::meta::info continuation_meta)
    {
        if (!detail::is_reader_io(source_meta))
        {
            return "source is not a ReaderIO";
        }
        std::meta::info const state_meta = detail::reader_io_state_meta(source_meta);
        if (!is_copy_constructible_type(state_meta))
        {
            return "state must be copy constructible";
        }
        std::meta::info const source_value_meta = detail::reader_io_value_meta(source_meta);
        if (is_void_type(source_value_meta))
        {
            return "source ReaderIO produces no value";
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
            return "continuation function does not accept the source ReaderIO's value";
        }
        std::meta::info const result_meta =
            fnfelt::detail::dispatch_result_meta(continuation_meta_bare, source_value_meta);
        if (!detail::is_reader_io(result_meta))
        {
            return "continuation function must return a ReaderIO";
        }
        if (!is_same_type(detail::reader_io_state_meta(result_meta), state_meta))
        {
            return "continuation function must return a ReaderIO with the same state type";
        }
        if (is_void_type(detail::reader_io_value_meta(result_meta)))
        {
            return "continuation function must return a ReaderIO that produces a value";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
