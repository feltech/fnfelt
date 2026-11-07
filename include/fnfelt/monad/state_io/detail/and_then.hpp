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
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/detail/type_names.hpp>
// The action delegates the composition to the inner IO's and_then, whose definition (and the IO
// member's deduction target) is required here.
#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/state_io/detail.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>
#include <fnfelt/result.hpp>

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
 * @param operation Composition operation shown in the diagnostic.
 * @return Formatted error message string.
 */
template <std::meta::info source_meta, std::meta::info continuation_meta>
consteval std::string and_then_error_msg(
    std::string_view name, std::string_view reason, std::string_view operation = "and_then")
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += ' ';
    fnfelt::detail::append_string_view(msg, operation);
    msg += " error: ";
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
 * Infer successful continuation routing and the joined logical result.
 *
 * @tparam TSource Source StateIO type.
 * @tparam TContinuation StateIO-producing callable type.
 */
template <class TSource, class TContinuation>
struct AndThenContinuationInfo
{
    /// Selected successful payload invocation.
    static constexpr auto continuation_info = fnfelt::detail::SuccessContinuationInfo::from(
        ^^TContinuation, state_io_value_meta(^^TSource));
    /// Source Result shape without interpreting nested payloads.
    static constexpr auto source_result_info =
        fnfelt::detail::ResultTypeInfo::from(state_io_value_meta(^^TSource));
    /// Final plain logical value or promoted Result.
    using value_type = [:fnfelt::detail::ResultTypeInfo::from(
                             state_io_value_meta(^^TSource), continuation_info.is_result)
                             .merge(
                                 fnfelt::detail::ResultTypeInfo::from(
                                     state_io_value_meta(continuation_info.result_meta)))
                             .type_meta():];
    /// Updated state remains outside the Result, including when stopped.
    using pair_type = std::pair<value_type, typename TSource::state_type>;
    /// Whether the selected child requires an asynchronous driver.
    static constexpr bool is_async = io::detail::action_is_async(
        io::detail::io_action_meta(state_io_io_meta(continuation_info.result_meta)));
};

/**
 * Execute a successful continuation inline while retaining state on bypass.
 *
 * @tparam TInfo Continuation inference and result types.
 * @tparam TPair Source logical result and updated state type.
 * @tparam TContinuation Continuation expression type.
 * @param input Source logical result and updated state.
 * @param continuation Successful-payload continuation.
 * @return Final logical result and latest state.
 */
template <class TInfo, class TPair, class TContinuation>
constexpr typename TInfo::pair_type run_and_then_inline(
    TPair && input, TContinuation && continuation)
{
    using result_type = typename TInfo::value_type;
    if constexpr (TInfo::source_result_info.is_result)
    {
        if (auto passthrough = fnfelt::detail::
                maybe_stop_or_error<TInfo::continuation_info.is_result, result_type>(
                    FW(input).first))
        {
            // Bypassing the child preserves the state already returned by the source.
            return {*std::move(passthrough), FW(input).second};
        }
    }
    auto next = fnfelt::detail::invoke_success_continuation<TInfo::continuation_info.is_result>(
        FW(continuation), FW(input).first);
    auto output = bind_state(std::move(next), FW(input).second).build().sync_wait();
    return {
        fnfelt::detail::convert_result<result_type>(std::move(output.first)),
        std::move(output.second)};
}

/**
 * Run a selected asynchronous child with ownership of the source state.
 *
 * @tparam TInfo Continuation inference and result types.
 * @tparam TPair Source logical result and state type.
 * @tparam TContinuation Successful-payload continuation type.
 */
template <class TInfo, class TPair, class TContinuation>
struct AndThenStepProxy : io::detail::TransportProxyTag
{
    /// Final logical result and latest state.
    using value_type = typename TInfo::pair_type;
    /// Current result/state and selected continuation.
    std::tuple<TPair, TContinuation> arg;

    /**
     * Bypass unsuccessful results or await the successful child.
     *
     * @par Callable parameters
     * The first argument is supplied by libfork. `input` owns the current pair and `continuation`
     * produces the next state computation.
     */
    static constexpr auto fn = [](auto, TPair input, TContinuation continuation)
        -> lf::task<io::detail::task_result_t<value_type>>
    {
        using result_type = typename TInfo::value_type;
        if constexpr (TInfo::source_result_info.is_result)
        {
            if (auto passthrough = fnfelt::detail::
                    maybe_stop_or_error<TInfo::continuation_info.is_result, result_type>(
                        std::move(input.first)))
            {
                co_return io::detail::pack_result<value_type>(
                    value_type{*std::move(passthrough), std::move(input.second)});
            }
        }
        auto next = fnfelt::detail::invoke_success_continuation<TInfo::continuation_info.is_result>(
            std::move(continuation), std::move(input.first));
        auto output = io::detail::unpack_result(
            co_await io::detail::just_child(bind_state(std::move(next), std::move(input.second))));
        co_return io::detail::pack_result<value_type>(value_type{
            fnfelt::detail::convert_result<result_type>(std::move(output.first)),
            std::move(output.second)});
    };

    /**
     * Apply identical routing during constant evaluation.
     *
     * @par Callable parameters
     * `input`: Current logical result and state. `continuation`: Successful-payload continuation.
     */
    static constexpr auto run_inline = [](TPair input, TContinuation continuation) -> value_type
    {
        return state_io::detail::run_and_then_inline<TInfo>(
            std::move(input), std::move(continuation));
    };

    /**
     * Retain the current pair and continuation.
     *
     * @param stored Owned invocation arguments.
     */
    constexpr explicit AndThenStepProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Give selected and bypassed branches one deferred IO type.
 *
 * @tparam TInfo Continuation inference and result types.
 * @tparam TPair Source logical result and state type.
 * @tparam TContinuation Successful-payload continuation type.
 */
template <class TInfo, class TPair, class TContinuation>
struct AndThenStepAction
{
    /// Plain pair or asynchronous driver.
    using result_type = std::conditional_t<
        TInfo::is_async,
        AndThenStepProxy<TInfo, TPair, TContinuation>,
        typename TInfo::pair_type>;
    /// Source result and updated state.
    TPair input;
    /// Successful-payload continuation.
    TContinuation continuation;

    /**
     * Execute inline or retain arguments in the asynchronous driver.
     *
     * @param self Stored step.
     * @return Final pair or its asynchronous driver.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (TInfo::is_async)
        {
            return result_type{{FW(self).input, FW(self).continuation}};
        }
        else
        {
            return state_io::detail::run_and_then_inline<TInfo>(
                FW(self).input, FW(self).continuation);
        }
    }
};

/**
 * Bind a continuation to a source's current logical result and state.
 *
 * @tparam TSource Source StateIO type.
 * @tparam TContinuation StateIO-producing callable type.
 */
template <class TSource, class TContinuation>
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
        using input_type = std::remove_cvref_t<decltype(value)>;
        using info_type = AndThenContinuationInfo<TSource, TContinuation>;
        return io::create(
            AndThenStepAction<info_type, input_type, TContinuation>{
                FW(value), FW(self).continuation});
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
            BoundContinuation<TSource, TContinuation>{FW(self).continuation});
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
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(source_value_meta).validate();
            !reason.empty())
        {
            return reason;
        }
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
        auto const continuation_info = fnfelt::detail::SuccessContinuationInfo::from(
            continuation_meta_bare, source_value_meta);
        if (!continuation_info.is_valid)
        {
            return "continuation function does not accept the source StateIO's value";
        }
        std::meta::info const result_meta = continuation_info.result_meta;
        if (!detail::is_state_io(result_meta))
        {
            return "continuation function must return a StateIO";
        }
        if (!is_same_type(detail::state_io_state_meta(result_meta), state_meta))
        {
            return "continuation function must return a StateIO with the same state type";
        }
        auto const result_value_meta = detail::state_io_value_meta(result_meta);
        if (is_void_type(result_value_meta))
        {
            return "continuation function must return a StateIO that produces a value";
        }
        auto const result_type_info = fnfelt::detail::ResultTypeInfo::from(result_value_meta);
        if (auto const reason = result_type_info.validate(); !reason.empty())
        {
            return reason;
        }
        return fnfelt::detail::ResultTypeInfo::from(source_value_meta, continuation_info.is_result)
            .merge(result_type_info)
            .validate();
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
