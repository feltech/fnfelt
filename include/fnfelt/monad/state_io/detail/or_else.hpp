// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/or_else.hpp
 *
 * Conditional error recovery retaining the latest state.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/monad/state_io/detail/and_then.hpp>
#include <fnfelt/result.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for invalid recovery.
struct OrElseError
{
};

/**
 * Infer only reachable recovery and its replacement error domain.
 *
 * @tparam TSource Source StateIO type.
 * @tparam TContinuation Error-taking StateIO continuation type.
 */
template <class TSource, class TContinuation>
struct OrElseContinuationInfo
{
    /// Selected error payload invocation, without querying an impossible handler.
    static constexpr auto continuation_info = fnfelt::detail::ErrorContinuationInfo::from(
        ^^TContinuation, state_io_value_meta(^^TSource));
    /// Source logical Result shape.
    static constexpr auto source_result_info =
        fnfelt::detail::ResultTypeInfo::from(state_io_value_meta(^^TSource));
    /// Final logical Result, preserving source success and replacing handled errors.
    using value_type = [:[] consteval
                        {
                            auto info = source_result_info;
                            if constexpr (continuation_info.is_reachable)
                            {
                                info.has_error = false;
                                info.error_payload_meta = ^^fnfelt::Infallible;
                                info = info.merge(
                                    fnfelt::detail::ResultTypeInfo::from(
                                        state_io_value_meta(continuation_info.result_meta)));
                            }
                            return info.type_meta();
                        }():];
    /// Logical Result and latest state.
    using pair_type = std::pair<value_type, typename TSource::state_type>;
    /// An impossible handler contributes no asynchronous child type.
    static constexpr bool is_async = [] consteval
    {
        if constexpr (continuation_info.is_reachable)
        {
            return io::detail::action_is_async(
                io::detail::io_action_meta(state_io_io_meta(continuation_info.result_meta)));
        }
        return false;
    }();
};

/**
 * Recover an error inline using the source's updated state.
 *
 * @tparam TInfo Recovery inference and result types.
 * @tparam TPair Current logical Result and state expression type.
 * @tparam TContinuation Recovery expression type.
 * @param input Current logical Result and state.
 * @param continuation Error-taking StateIO continuation.
 * @return Recovered logical Result and latest state.
 */
template <class TInfo, class TPair, class TContinuation>
constexpr typename TInfo::pair_type run_or_else_inline(
    TPair && input, TContinuation && continuation)
{
    if constexpr (!TInfo::continuation_info.is_reachable)
    {
        // Preserve success or stop without forming any invocation of the impossible handler.
        return FW(input);
    }
    else
    {
        using result_type = typename TInfo::value_type;
        if (auto passthrough =
                fnfelt::detail::maybe_stop_or_success<true, result_type>(FW(input).first))
        {
            return {*std::move(passthrough), FW(input).second};
        }
        auto next = fnfelt::detail::invoke_error_continuation(FW(continuation), FW(input).first);
        auto output = bind_state(std::move(next), FW(input).second).build().sync_wait();
        return {
            fnfelt::detail::convert_result<result_type>(std::move(output.first)),
            std::move(output.second)};
    }
}

/**
 * Retain the failing state while awaiting an asynchronous recovery.
 *
 * @tparam TInfo Recovery inference and result types.
 * @tparam TPair Current logical Result and state type.
 * @tparam TContinuation Recovery continuation type.
 */
template <class TInfo, class TPair, class TContinuation>
struct OrElseStepProxy : io::detail::TransportProxyTag
{
    /// Recovered logical Result and latest state.
    using value_type = typename TInfo::pair_type;
    /// Current pair and owned recovery.
    std::tuple<TPair, TContinuation> arg;

    /**
     * Pass through success and stop, or await the error recovery.
     *
     * @par Callable parameters
     * The first argument is supplied by libfork. `input` owns the current Result/state pair and
     * `continuation` handles its error payload.
     */
    static constexpr auto fn = [](auto, TPair input, TContinuation continuation)
        -> lf::task<io::detail::task_result_t<value_type>>
    {
        using result_type = typename TInfo::value_type;
        if (auto passthrough =
                fnfelt::detail::maybe_stop_or_success<true, result_type>(std::move(input.first)))
        {
            co_return io::detail::pack_result<value_type>(
                value_type{*std::move(passthrough), std::move(input.second)});
        }
        auto next = fnfelt::detail::invoke_error_continuation(
            std::move(continuation), std::move(input.first));
        auto output = io::detail::unpack_result(
            co_await io::detail::just_child(bind_state(std::move(next), std::move(input.second))));
        co_return io::detail::pack_result<value_type>(value_type{
            fnfelt::detail::convert_result<result_type>(std::move(output.first)),
            std::move(output.second)});
    };

    /**
     * Evaluate identical routing inline during constant evaluation.
     *
     * @par Callable parameters
     * `input`: Current logical Result and state. `continuation`: Error-payload recovery.
     */
    static constexpr auto run_inline = [](TPair input, TContinuation continuation) -> value_type
    {
        return state_io::detail::run_or_else_inline<TInfo>(
            std::move(input), std::move(continuation));
    };

    /**
     * Retain source state and recovery for execution.
     *
     * @param stored Owned current pair and recovery.
     */
    constexpr explicit OrElseStepProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Give bypassed and recovered outcomes one deferred IO type.
 *
 * @tparam TInfo Recovery inference and result types.
 * @tparam TPair Current logical Result and state type.
 * @tparam TContinuation Recovery type.
 */
template <class TInfo, class TPair, class TContinuation>
struct OrElseStepAction
{
    /// Plain result/state pair or asynchronous recovery driver.
    using result_type = std::conditional_t<
        TInfo::is_async,
        OrElseStepProxy<TInfo, TPair, TContinuation>,
        typename TInfo::pair_type>;
    /// Current logical Result and state.
    TPair input;
    /// Error-taking recovery.
    TContinuation continuation;

    /**
     * Select inline recovery or asynchronous transport.
     *
     * @param self Stored recovery step.
     * @return Recovered pair or its asynchronous driver.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (TInfo::is_async)
        {
            return result_type{{FW(self).input, FW(self).continuation}};
        }
        else
        {
            return state_io::detail::run_or_else_inline<TInfo>(
                FW(self).input, FW(self).continuation);
        }
    }
};

/**
 * Bind error recovery to the state returned by its source.
 *
 * @tparam TSource Source StateIO type.
 * @tparam TContinuation Error-taking StateIO continuation type.
 */
template <class TSource, class TContinuation>
struct BoundErrorContinuation
{
    /// Error-taking recovery.
    TContinuation continuation;

    /**
     * Retain the complete source outcome and state for conditional recovery.
     *
     * @param self Bound recovery.
     * @param input Source logical Result and updated state.
     * @return Deferred recovery IO.
     */
    constexpr auto operator()(this auto && self, auto && input)
    {
        using pair_type = std::remove_cvref_t<decltype(input)>;
        using info_type = OrElseContinuationInfo<TSource, TContinuation>;
        return io::create(
            OrElseStepAction<info_type, pair_type, TContinuation>{
                FW(input), FW(self).continuation});
    }
};

/**
 * Run the source once before selecting error recovery.
 *
 * @tparam TState State type.
 * @tparam TSource Source StateIO type.
 * @tparam TContinuation Error-taking StateIO continuation type.
 */
template <class TState, class TSource, class TContinuation>
struct OrElseAction
{
    /// Deferred source.
    TSource source;
    /// Error recovery.
    TContinuation continuation;

    /**
     * Bind state and thread its latest value into recovery.
     *
     * @param self Stored recovery action.
     * @param state Initial state.
     * @return Inner IO producing the logical Result and latest state.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return bind_state<TState>(FW(self).source, std::move(state))
            .and_then(BoundErrorContinuation<TSource, TContinuation>{FW(self).continuation});
    }
};

/**
 * Validation of Result recovery over one state type.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct OrElseStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Validate source Result and only a reachable recovery signature.
     *
     * @param source_meta Source StateIO reflection.
     * @param continuation_meta Recovery callable reflection.
     * @return Empty on success, otherwise a validation reason.
     */
    static consteval std::string_view validate_or_else(
        std::meta::info source_meta, std::meta::info continuation_meta)
    {
        if (!is_state_io(source_meta))
            return "source is not a StateIO";
        auto const source_value_meta = state_io_value_meta(source_meta);
        auto source_result_info = fnfelt::detail::ResultTypeInfo::from(source_value_meta);
        if (auto const reason = source_result_info.validate(); !reason.empty())
            return reason;
        if (!source_result_info.is_result)
            return "source StateIO does not produce a Result";
        switch (fnfelt::detail::callable_shape_of(continuation_meta))
        {
            case fnfelt::detail::CallableShape::incomplete:
                return "recovery function is not a complete type";
            case fnfelt::detail::CallableShape::reference:
                return "recovery function must not be a reference type";
            case fnfelt::detail::CallableShape::not_class_or_function_pointer:
                return "recovery function is not a class or function pointer type";
            case fnfelt::detail::CallableShape::not_movable:
                return "recovery function must be move constructible";
            case fnfelt::detail::CallableShape::valid:
                break;
        }
        auto const continuation_info =
            fnfelt::detail::ErrorContinuationInfo::from(continuation_meta, source_value_meta);
        if (!continuation_info.is_reachable)
            return {};
        if (!continuation_info.is_valid)
            return "recovery function does not accept the source error";
        auto const result_meta = continuation_info.result_meta;
        if (!is_state_io(result_meta))
            return "recovery function must return a StateIO";
        if (!is_same_type(state_io_state_meta(source_meta), state_io_state_meta(result_meta)))
            return "recovery function must return a StateIO with the same state type";
        auto const value_meta = state_io_value_meta(result_meta);
        auto const result_type_info = fnfelt::detail::ResultTypeInfo::from(value_meta);
        if (auto const reason = result_type_info.validate(); !reason.empty())
            return reason;
        if (is_void_type(value_meta))
            return "recovery function must return a StateIO that produces a value";
        if (!is_same_type(
                source_result_info.success_payload_meta, result_type_info.success_payload_meta))
            return "recovery and source success must have the same payload type; use transform "
                   "before or_else to align successful payloads";
        source_result_info.has_error = false;
        source_result_info.error_payload_meta = ^^fnfelt::Infallible;
        return source_result_info.merge(result_type_info).validate();
    }
};

/**
 * Format recovery diagnostics without querying the handler signature.
 *
 * @tparam source_meta Source StateIO reflection.
 * @tparam continuation_meta Recovery callable reflection.
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Contextual diagnostic.
 */
template <std::meta::info source_meta, std::meta::info continuation_meta>
consteval std::string or_else_error_msg(std::string_view name, std::string_view reason)
{
    return and_then_error_msg<source_meta, continuation_meta>(name, reason, "or_else");
}
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
