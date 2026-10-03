// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/and_then.hpp
 *
 * Implementation details for the IO monad's bind operation.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
// Forward declaration for the detail helpers below, which reflect on the proxy. The proxy itself
// is defined after them.
template <class TSourceIO, class TContinuation>
struct AndThenProxy;

namespace detail
{

/**
 * Tombstone returned by `io::and_then` when the continuation is invalid.
 *
 * The invalid and_then is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct AndThenError
{
};

/**
 * Reflection of the IO returned by a source IO's continuation.
 *
 * The continuation is applied with the source IO's value directly when possible, otherwise the
 * value is spread into it (direct invocation wins over spreading), mirroring the run-time dispatch.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the continuation's IO, or `void` if not determinable.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_continuation_io_meta()
{
    return fnfelt::detail::dispatch_result_meta(^^TContinuation, io_value_meta(^^TSourceIO));
}

/**
 * Reflection of the value an and_then action produces when run.
 *
 * The invocation result is the continuation's IO, whose own value (async-transparent) is the final
 * value.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the value the action produces.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_value_type_meta()
{
    return io_value_meta(and_then_continuation_io_meta<TSourceIO, TContinuation>());
}

/**
 * Whether an and_then composition must run asynchronously.
 *
 * The composition is asynchronous when either the source action or the continuation's returned IO's
 * action is asynchronous. The result type and the action's run-time dispatch both derive from this
 * predicate, keeping them in sync.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return True if the composition runs asynchronously.
 */
template <class TSourceIO, class TContinuation>
consteval bool and_then_is_async()
{
    constexpr std::meta::info source_action_meta = io_action_meta(^^TSourceIO);
    if constexpr (action_is_async(source_action_meta))
    {
        return true;
    }
    constexpr std::meta::info continuation_io_meta =
        and_then_continuation_io_meta<TSourceIO, TContinuation>();
    if constexpr (is_io(continuation_io_meta))
    {
        return action_is_async(io_action_meta(continuation_io_meta));
    }
    return false;
}

/**
 * Reflection of the value an AndThenAction's declared return type names.
 *
 * Synchronous compositions produce a plain value; asynchronous ones produce the composed async
 * proxy.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the action's declared return type.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_result_type_meta()
{
    if constexpr (and_then_is_async<TSourceIO, TContinuation>())
    {
        return ^^AndThenProxy<TSourceIO, TContinuation>;
    }
    return and_then_value_type_meta<TSourceIO, TContinuation>();
}

/**
 * Action that runs a source IO and feeds its value to a continuation.
 *
 * Synchronous compositions run the source and continuation inline and produce a plain value;
 * asynchronous or mixed compositions instead materialise the composed async proxy so that the whole
 * tree runs under a single top-level scheduler.
 *
 * The continuation is invoked with the value directly when possible, otherwise the value is spread
 * into the continuation as if by `std::apply` (direct invocation wins over spreading). The action's
 * value category is forwarded to the source, and the stored continuation is invoked as an rvalue,
 * matching validation and result inference.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TContinuation>
struct AndThenAction
{
    /// Value the action produces when run (async proxy or plain value).
    using result_type = [:and_then_result_type_meta<TSourceIO, TContinuation>():];
    /// Source IO to run.
    TSourceIO source;
    /// Continuation applied to the source's value.
    TContinuation continuation;

    /**
     * Run the source IO, apply the continuation to its value, then run the resulting IO.
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The composed async proxy, or the final value on the synchronous path.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (and_then_is_async<TSourceIO, TContinuation>())
        {
            return result_type{
                std::tuple<TSourceIO, TContinuation>{FW(self).source, FW(self).continuation}};
        }
        else
        {
            auto input = FW(self).source.build().sync_wait();
            // The stored continuation is moved into the call so the receiver matches validation
            // and inference; the direct-vs-spread dispatch is `invoke_dispatch`'s.
            auto next = fnfelt::detail::invoke_dispatch(FW(self).continuation, std::move(input));
            return std::move(next).build().sync_wait();
        }
        // Not reachable: validation guarantees one of the dispatches above is taken.
        std::unreachable();
    }
};

/**
 * Name of IO returned from the continuation.
 *
 * The application is dispatched like the run-time continuation invocation: direct when possible,
 * otherwise spread (`is_dispatch_invocable`). Invalid, incomplete, or non-dispatchable inputs
 * yield "<unknown>".
 *
 * @tparam source_io_meta Source IO reflection.
 * @tparam continuation_meta Continuation reflection.
 * @return Name of IO returned from the continuation if possible, otherwise "<unknown>".
 */
template <std::meta::info source_io_meta, std::meta::info continuation_meta>
constexpr std::string_view maybe_continuation_result_io_name()
{
    // Completeness is guarded because the diagnostic is instantiated even when validation fails: a
    // query on an incomplete continuation is a hard, non-catchable trait-precondition error,
    // whereas validation itself reports "continuation function is not a complete type".
    if constexpr (
        is_io(source_io_meta) && is_type(continuation_meta) &&
        is_complete_type(remove_cvref(continuation_meta)))
    {
        constexpr std::meta::info continuation_arg_meta = io_value_meta(source_io_meta);

        if constexpr (fnfelt::detail::is_dispatch_invocable(
                          continuation_meta, continuation_arg_meta))
        {
            // A continuation may return an IO reference; a reference cannot be spliced, and the
            // assertion message is instantiated even when validation passes.
            constexpr std::meta::info named_meta = remove_cvref(
                fnfelt::detail::dispatch_result_meta(continuation_meta, continuation_arg_meta));
            return maybe_io_name<named_meta>();
        }
    }

    return "<unknown>";
}

/**
 * Constructs the diagnostic message for a rejected composition.
 *
 * Uses reflection to extract type names and diagnostics about the source IO and continuation when
 * and_then validation fails. The message includes the IO name, the source IO type, the
 * continuation's result type (if determinable), the specific validation failure reason, and the
 * offending argument's display string (the source type if the source is not an IO, otherwise the
 * continuation type).
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_io_meta Reflection info for the source IO type.
 * @tparam continuation_meta Reflection info for the continuation type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_and_then`.
 * @return Formatted error message string.
 */
template <std::meta::info source_io_meta, std::meta::info continuation_meta>
consteval std::string and_then_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO and_then error: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += "{(";
    fnfelt::detail::append_string_view(msg, maybe_io_name<source_io_meta>());
    msg += "()) => ";
    fnfelt::detail::append_string_view(
        msg, maybe_continuation_result_io_name<source_io_meta, continuation_meta>());
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    std::meta::info const target_meta =
        !detail::is_io(source_io_meta) ? source_io_meta : continuation_meta;
    fnfelt::detail::append_string_view(msg, display_string_of(target_meta));

    return msg;
}

}  // namespace detail

/**
 * Async proxy composing a source IO with a continuation (async/mixed `and_then`).
 *
 * The driver runs the source IO, applies the continuation to its value with the same direct-vs-
 * spread dispatch as the synchronous action, then runs the continuation's IO.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TContinuation>
struct AndThenProxy : detail::TransportProxyTag
{
    /// Value the composed IO produces.
    using value_type = [:detail::and_then_value_type_meta<TSourceIO, TContinuation>():];
    /// Source IO and continuation spread into @ref fn.
    std::tuple<TSourceIO, TContinuation> arg;
    /// Async/mixed composition driver.
    static constexpr auto fn = [](auto, TSourceIO source, TContinuation continuation)
        -> lf::task<detail::task_result_t<value_type>>
    {
        auto input = detail::unpack_result(co_await detail::just_child(std::move(source)));
        // The stored continuation is moved into the call so the receiver matches validation and
        // inference; the direct-vs-spread dispatch is `invoke_dispatch`'s, mirroring the
        // synchronous action.
        auto next = fnfelt::detail::invoke_dispatch(std::move(continuation), std::move(input));
        co_return co_await detail::just_child(std::move(next));
    };

    /// Run dependent effects inline during constant evaluation.
    static constexpr auto run_inline = [](TSourceIO source,
                                          TContinuation continuation) -> value_type
    {
        auto input = std::move(source).build().sync_wait();
        auto next = fnfelt::detail::invoke_dispatch(std::move(continuation), std::move(input));
        return std::move(next).build().sync_wait();
    };

    /**
     * Construct from the source IO and continuation.
     *
     * @param arg_in Source IO and continuation to store.
     */
    constexpr explicit AndThenProxy(std::tuple<TSourceIO, TContinuation> arg_in)
        : arg{std::move(arg_in)}
    {
    }
};

namespace detail
{
/**
 * Traits validating the source IO and continuation function provided to `io::and_then`.
 *
 * The continuation function must accept the source IO's value and return an IO that produces a
 * value.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct AndThenIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the source IO and continuation function.
     *
     * Checks, in order: the source is an IO producing a value (not void); the continuation function
     * is a complete, non-reference, move constructible class or function pointer type; it accepts
     * the source IO's value either directly or spread from its template arguments; and its result
     * is an IO that produces a value.
     *
     * @param io_meta Reflection of the source IO type.
     * @param continuation_meta Reflection of the continuation function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_and_then(
        std::meta::info io_meta, std::meta::info continuation_meta)
    {
        if (!detail::is_io(io_meta))
        {
            return "source IO is not an IO";
        }
        auto const value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(value_meta))
        {
            return "source IO produces no value";
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
        continuation_meta = dealias(remove_cvref(continuation_meta));
        if (!fnfelt::detail::is_dispatch_invocable(continuation_meta, value_meta))
        {
            return "continuation function does not accept the source IO's value";
        }
        std::meta::info const result_meta =
            fnfelt::detail::dispatch_result_meta(continuation_meta, value_meta);
        if (!detail::is_io(result_meta))
        {
            return "continuation function must return an IO";
        }
        if (is_void_type(detail::io_value_meta(result_meta)))
        {
            return "continuation function must return an IO that produces a value";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
