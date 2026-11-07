// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/and_then.hpp
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
#include <fnfelt/result.hpp>

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
    return fnfelt::detail::SuccessContinuationInfo::from(
               ^^TContinuation, io_value_meta(^^TSourceIO))
        .result_meta;
}

/**
 * Result type information for an and_then composition.
 *
 * Combines preserved source errors with the continuation IO's result type.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Combined source and continuation result type information.
 */
template <class TSourceIO, class TContinuation>
consteval fnfelt::detail::ResultTypeInfo and_then_result_type_info()
{
    constexpr auto source_meta = io_value_meta(^^TSourceIO);
    constexpr auto continuation_info =
        fnfelt::detail::SuccessContinuationInfo::from(^^TContinuation, source_meta);
    constexpr auto result_meta = io_value_meta(continuation_info.result_meta);
    return fnfelt::detail::ResultTypeInfo::from(source_meta, continuation_info.is_result)
        .merge(fnfelt::detail::ResultTypeInfo::from(result_meta));
}

/**
 * Source interpretation and result lifting shared by all bind execution paths.
 *
 * @tparam TSourceIO Source IO.
 * @tparam TContinuation Continuation type.
 */
template <class TSourceIO, class TContinuation>
struct AndThenContinuationInfo
{
    /// Payload invocation selected by the Result shape.
    static constexpr auto continuation_info =
        fnfelt::detail::SuccessContinuationInfo::from(^^TContinuation, io_value_meta(^^TSourceIO));
    /// Source payload and error domain.
    static constexpr auto source_result_info =
        fnfelt::detail::ResultTypeInfo::from(io_value_meta(^^TSourceIO));
    /// Final plain value or promoted Result.
    using value_type = [:and_then_result_type_info<TSourceIO, TContinuation>().type_meta():];
    /// Whether the continuation already returns the joined Result type.
    static constexpr bool is_identity_result =
        is_same_type(^^value_type, io_value_meta(continuation_info.result_meta));
};

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
    return and_then_result_type_info<TSourceIO, TContinuation>().type_meta();
}

/**
 * Run a source and its successful continuation inline.
 *
 * @tparam TContinuationInfo Source interpretation and result adaptation policy.
 * @tparam TSourceIO Source IO argument type.
 * @tparam TContinuation Continuation argument type.
 * @param source Source IO.
 * @param continuation Continuation applied to successful payload data.
 * @return Final value or propagated source outcome.
 */
template <class TContinuationInfo, class TSourceIO, class TContinuation>
constexpr typename TContinuationInfo::value_type run_and_then_inline(
    TSourceIO && source, TContinuation && continuation)
{
    using result_type = typename TContinuationInfo::value_type;
    auto input = FW(source).build().sync_wait();
    if constexpr (TContinuationInfo::source_result_info.is_result)
    {
        if (auto maybe_stop_or_error = fnfelt::detail::
                maybe_stop_or_error<TContinuationInfo::continuation_info.is_result, result_type>(
                    std::move(input)))
        {
            return *std::move(maybe_stop_or_error);
        }
    }
    // Preserve the continuation's receiver category so dispatch matches validation and inference.
    auto next =
        fnfelt::detail::invoke_success_continuation<TContinuationInfo::continuation_info.is_result>(
            FW(continuation), std::move(input));
    if constexpr (TContinuationInfo::is_identity_result)
    {
        return std::move(next).build().sync_wait();
    }
    else
    {
        return fnfelt::detail::convert_result<result_type>(std::move(next).build().sync_wait());
    }
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
    /// Continuation applied to successful payload data.
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
            using continuation_info = AndThenContinuationInfo<TSourceIO, TContinuation>;
            return run_and_then_inline<continuation_info>(FW(self).source, FW(self).continuation);
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
        // Rejected source payloads must not instantiate a generic continuation to format an error.
        if constexpr (!fnfelt::detail::ResultTypeInfo::from(io_value_meta(source_io_meta))
                           .validate()
                           .empty())
        {
            return "<unknown>";
        }
        else
        {
            constexpr auto continuation_info = fnfelt::detail::SuccessContinuationInfo::from(
                continuation_meta, io_value_meta(source_io_meta));

            if constexpr (continuation_info.is_valid)
            {
                // A continuation may return an IO reference; a reference cannot be spliced, and
                // the assertion message is instantiated even when validation passes.
                constexpr std::meta::info named_meta = remove_cvref(continuation_info.result_meta);
                return maybe_io_name<named_meta>();
            }
        }
    }

    return "<unknown>";
}

/**
 * Constructs the diagnostic message for a rejected composition.
 *
 * Uses reflection to extract type names and diagnostics about the source IO and continuation when
 * and_then validation fails. The message includes the IO name, the source IO type, the
 * continuation's result type (if determinable), the specific validation failure reason, and
 * the offending argument's display string (the source type if the source is not an IO, otherwise
 * the continuation type).
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
    using value_type = [:detail::and_then_result_type_info<TSourceIO, TContinuation>()
                             .type_meta():];
    /// Source IO and continuation spread into @ref fn.
    std::tuple<TSourceIO, TContinuation> arg;
    /**
     * Async/mixed composition driver.
     *
     * @par Callable parameters
     * The unnamed first argument is supplied by libfork and is unused.
     * `source`: Source IO to run. `continuation`: Successful-payload continuation.
     */
    static constexpr auto fn = [](auto, TSourceIO source, TContinuation continuation)
        -> lf::task<detail::task_result_t<value_type>>
    {
        using continuation_info = detail::AndThenContinuationInfo<TSourceIO, TContinuation>;
        auto input = detail::unpack_result(co_await detail::just_child(std::move(source)));
        if constexpr (continuation_info::source_result_info.is_result)
        {
            if (auto maybe_stop_or_error = fnfelt::detail::
                    maybe_stop_or_error<continuation_info::continuation_info.is_result, value_type>(
                        std::move(input)))
            {
                co_return detail::pack_result<value_type>(*std::move(maybe_stop_or_error));
            }
        }
        // The stored continuation is moved into the call so the receiver matches validation and
        // inference; the direct-vs-spread dispatch is `invoke_dispatch`'s, mirroring the
        // synchronous action.
        auto next = fnfelt::detail::invoke_success_continuation<
            continuation_info::continuation_info.is_result>(
            std::move(continuation), std::move(input));
        if constexpr (continuation_info::is_identity_result)
        {
            co_return co_await detail::just_child(std::move(next));
        }
        else
        {
            auto output = detail::unpack_result(co_await detail::just_child(std::move(next)));
            co_return detail::pack_result<value_type>(
                fnfelt::detail::convert_result<value_type>(std::move(output)));
        }
    };

    /**
     * Run dependent effects inline during constant evaluation.
     *
     * @par Callable parameters
     * `source`: Source IO to run. `continuation`: Successful-payload continuation.
     */
    static constexpr auto run_inline = [](TSourceIO source,
                                          TContinuation continuation) -> value_type
    {
        using continuation_info = detail::AndThenContinuationInfo<TSourceIO, TContinuation>;
        return detail::run_and_then_inline<continuation_info>(
            std::move(source), std::move(continuation));
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
     * the ordinary source value or explicit Result payload either directly or spread; and its
     * result is an IO that produces a value.
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
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(value_meta).validate();
            !reason.empty())
        {
            return reason;
        }
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
        auto const continuation_info =
            fnfelt::detail::SuccessContinuationInfo::from(continuation_meta, value_meta);
        if (!continuation_info.is_valid)
        {
            return "continuation function does not accept the source IO's value";
        }
        std::meta::info const result_meta = continuation_info.result_meta;
        if (!detail::is_io(result_meta))
        {
            return "continuation function must return an IO";
        }
        if (is_void_type(detail::io_value_meta(result_meta)))
        {
            return "continuation function must return an IO that produces a value";
        }
        if (auto const reason =
                fnfelt::detail::ResultTypeInfo::from(detail::io_value_meta(result_meta)).validate();
            !reason.empty())
        {
            return reason;
        }
        return fnfelt::detail::ResultTypeInfo::from(value_meta, continuation_info.is_result)
            .merge(fnfelt::detail::ResultTypeInfo::from(detail::io_value_meta(result_meta)))
            .validate();
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
