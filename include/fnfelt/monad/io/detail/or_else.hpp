// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/or_else.hpp
 *
 * Recovery of reported errors without handling stopped computations.
 */
#pragma once

#include <meta>

#include <functional>
#include <optional>
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
template <class TSourceIO, class TContinuation>
struct OrElseProxy;

namespace detail
{
/// Placeholder for invalid recovery compositions.
struct OrElseError
{
};

/**
 * Result type information for an or_else composition.
 *
 * @param source_result_info Source success payload and error type information.
 * @param continuation_info Recovery invocation and declared return type information.
 * @return Source result type information with handled errors replaced by recovery results.
 */
consteval fnfelt::detail::ResultTypeInfo or_else_result_type_info(
    fnfelt::detail::ResultTypeInfo source_result_info,
    fnfelt::detail::ErrorContinuationInfo continuation_info)
{
    if (continuation_info.is_valid)
    {
        // Recovery replaces the handled domain rather than joining it with the new error.
        source_result_info.has_error = false;
        source_result_info.error_payload_meta = ^^fnfelt::Infallible;
        source_result_info = source_result_info.merge(
            fnfelt::detail::ResultTypeInfo::from(io_value_meta(continuation_info.result_meta)));
    }
    return source_result_info;
}

/**
 * Shared inference and execution policy for recovery.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TContinuation Recovery continuation type.
 */
template <class TSourceIO, class TContinuation>
struct OrElseContinuationInfo
{
    /// Reachable recovery invocations.
    static constexpr auto continuation_info =
        fnfelt::detail::ErrorContinuationInfo::from(^^TContinuation, io_value_meta(^^TSourceIO));
    /// Source payload and error domain.
    static constexpr auto source_result_info =
        fnfelt::detail::ResultTypeInfo::from(io_value_meta(^^TSourceIO));
    /// Final canonical value after recovery.
    using value_type = [:or_else_result_type_info(source_result_info, continuation_info)
                             .type_meta():];
    /// Whether the recovery already returns the final Result type.
    static constexpr bool is_identity_result = []
    {
        if constexpr (continuation_info.is_valid)
        {
            return is_same_type(^^value_type, io_value_meta(continuation_info.result_meta));
        }
        // An unreachable handler has no return type to inspect.
        return false;
    }();
};

/**
 * Whether the source or either reachable recovery requires asynchronous execution.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TContinuation Recovery continuation type.
 * @return Whether the composition needs an asynchronous driver.
 */
template <class TSourceIO, class TContinuation>
consteval bool or_else_is_async()
{
    constexpr auto continuation_info =
        fnfelt::detail::ErrorContinuationInfo::from(^^TContinuation, io_value_meta(^^TSourceIO));
    if constexpr (action_is_async(io_action_meta(^^TSourceIO)))
    {
        return true;
    }
    if constexpr (continuation_info.is_valid)
    {
        if constexpr (action_is_async(io_action_meta(continuation_info.result_meta)))
        {
            return true;
        }
    }
    return false;
}

/**
 * Logical result or asynchronous transport for recovery.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TContinuation Recovery continuation type.
 * @return Reflection of the action's result type.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info or_else_result_meta()
{
    if constexpr (or_else_is_async<TSourceIO, TContinuation>())
    {
        return ^^OrElseProxy<TSourceIO, TContinuation>;
    }
    return ^^typename OrElseContinuationInfo<TSourceIO, TContinuation>::value_type;
}

/**
 * Run a source and its error recovery inline.
 *
 * @tparam TContinuationInfo Recovery inference and execution policy.
 * @tparam TSourceIO Source IO argument type.
 * @tparam TContinuation Recovery continuation argument type.
 * @param source Source IO.
 * @param continuation Recovery continuation.
 * @return Recovered result or unchanged successful or stopped source.
 */
template <class TContinuationInfo, class TSourceIO, class TContinuation>
constexpr typename TContinuationInfo::value_type run_or_else_inline(
    TSourceIO && source, TContinuation && continuation)
{
    using result_type = typename TContinuationInfo::value_type;
    auto input = FW(source).build().sync_wait();
    if constexpr (!TContinuationInfo::continuation_info.is_reachable)
    {
        // Infallible sources can still stop, but neither state can invoke recovery.
        return input;
    }
    else
    {
        if (auto passthrough_result = fnfelt::detail::
                maybe_stop_or_success<TContinuationInfo::continuation_info.is_valid, result_type>(
                    std::move(input)))
        {
            return *std::move(passthrough_result);
        }
        if constexpr (TContinuationInfo::continuation_info.is_valid)
        {
            auto next =
                fnfelt::detail::invoke_error_continuation(FW(continuation), std::move(input));
            if constexpr (TContinuationInfo::is_identity_result)
            {
                return std::move(next).build().sync_wait();
            }
            else
            {
                return fnfelt::detail::convert_result<result_type>(
                    std::move(next).build().sync_wait());
            }
        }
        std::unreachable();
    }
}

/**
 * Deferred recovery with one shared source outcome representation.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TContinuation Recovery continuation type.
 */
template <class TSourceIO, class TContinuation>
struct OrElseAction
{
    /// Logical value or asynchronous driver.
    using result_type = [:or_else_result_meta<TSourceIO, TContinuation>():];
    /// Deferred source.
    TSourceIO source;
    /// Error-taking recovery.
    TContinuation continuation;

    /**
     * Execute recovery only for a failed source with a matching signature.
     *
     * @param self Stored recovery action.
     * @return Canonical value or asynchronous driver.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (or_else_is_async<TSourceIO, TContinuation>())
        {
            return result_type{{FW(self).source, FW(self).continuation}};
        }
        else
        {
            using continuation_info = OrElseContinuationInfo<TSourceIO, TContinuation>;
            return run_or_else_inline<continuation_info>(FW(self).source, FW(self).continuation);
        }
    }
};

/**
 * Validation and result traits for shared-outcome recovery.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct OrElseIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate a reachable recovery's IO result and successful payload.
     *
     * @param result_meta Recovery return type.
     * @param success_meta Source success type.
     * @return Empty on success, otherwise a diagnostic reason.
     */
    static consteval std::string_view validate_recovery_result(
        std::meta::info result_meta, std::meta::info success_meta)
    {
        if (!is_io(result_meta))
        {
            return "recovery function must return an IO";
        }
        auto const value_meta = io_value_meta(result_meta);
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(value_meta).validate();
            !reason.empty())
        {
            return reason;
        }
        if (is_void_type(value_meta))
        {
            return "recovery function must return an IO that produces a value";
        }
        auto const result_type_info = fnfelt::detail::ResultTypeInfo::from(value_meta);
        if (!is_same_type(success_meta, result_type_info.success_payload_meta))
        {
            return "recovery and source success must have the same payload type; use transform "
                   "before or_else to align successful payloads";
        }
        return result_type_info.validate();
    }

    /**
     * Validate source outcomes and each reachable recovery signature.
     *
     * @param source_meta Source IO type.
     * @param continuation_meta Recovery continuation type.
     * @return Empty on success, otherwise a diagnostic reason.
     */
    static consteval std::string_view validate_or_else(
        std::meta::info source_meta, std::meta::info continuation_meta)
    {
        if (!is_io(source_meta))
        {
            return "source IO is not an IO";
        }
        if (auto const reason =
                fnfelt::detail::ResultTypeInfo::from(io_value_meta(source_meta)).validate();
            !reason.empty())
        {
            return reason;
        }
        if (is_void_type(io_value_meta(source_meta)))
        {
            return "source IO produces no value";
        }
        if (!fnfelt::detail::ResultTraits::is_result_type(io_value_meta(source_meta)))
        {
            return "source IO does not produce a Result";
        }
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
        auto const continuation_info = fnfelt::detail::ErrorContinuationInfo::from(
            continuation_meta, io_value_meta(source_meta));
        auto const source_result_info =
            fnfelt::detail::ResultTypeInfo::from(io_value_meta(source_meta));
        if (auto reason = source_result_info.validate(); !reason.empty())
        {
            return reason;
        }
        if (!continuation_info.is_valid)
        {
            // An Infallible Result has no errors to recover. Do not inspect the callback
            // signature: a generic handler's body may intentionally be invalid for this source.
            if (!continuation_info.is_reachable)
            {
                return {};
            }
            return "recovery function does not accept the source error";
        }
        if (auto reason = validate_recovery_result(
                continuation_info.result_meta, source_result_info.success_payload_meta);
            !reason.empty())
        {
            return reason;
        }
        return or_else_result_type_info(source_result_info, continuation_info).validate();
    }
};

/**
 * Describe an invalid recovery composition.
 *
 * @param name Diagnostic IO name.
 * @param reason Validation failure.
 * @return Human-readable diagnostic.
 */
consteval std::string or_else_error_msg(std::string_view name, std::string_view reason)
{
    std::string result = "fnfelt: IO or_else error: ";
    fnfelt::detail::append_string_view(result, name);
    result += ": ";
    fnfelt::detail::append_string_view(result, reason);
    return result;
}
}  // namespace detail

/**
 * Scheduler-transparent asynchronous shared-outcome recovery.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TContinuation Recovery continuation type.
 */
template <class TSourceIO, class TContinuation>
struct OrElseProxy : detail::TransportProxyTag
{
    /// Shared inference and dispatch policy.
    using continuation_info = detail::OrElseContinuationInfo<TSourceIO, TContinuation>;
    /// Canonical logical result.
    using value_type = typename continuation_info::value_type;
    /// Stored source and recovery, spread into the driver.
    std::tuple<TSourceIO, TContinuation> arg;
    /**
     * Run the source and the one selected runtime recovery under one scheduler.
     *
     * @par Callable parameters
     * The unnamed first argument is supplied by libfork and is unused.
     * `source`: Source IO to run. `continuation`: Error recovery continuation.
     */
    static constexpr auto fn = [](auto, TSourceIO source, TContinuation continuation)
        -> lf::task<detail::task_result_t<value_type>>
    {
        auto input = detail::unpack_result(co_await detail::just_child(std::move(source)));
        if constexpr (!continuation_info::continuation_info.is_reachable)
        {
            // Keep an impossible handler out of invocation and return-type inference.
            co_return detail::pack_result<value_type>(std::move(input));
        }
        else
        {
            if (auto passthrough_result = fnfelt::detail::maybe_stop_or_success<
                    continuation_info::continuation_info.is_valid,
                    typename continuation_info::value_type>(std::move(input)))
            {
                co_return detail::pack_result<value_type>(*std::move(passthrough_result));
            }
            if constexpr (continuation_info::continuation_info.is_valid)
            {
                auto next = fnfelt::detail::invoke_error_continuation(
                    std::move(continuation), std::move(input));
                if constexpr (continuation_info::is_identity_result)
                {
                    co_return co_await detail::just_child(std::move(next));
                }
                else
                {
                    auto output =
                        detail::unpack_result(co_await detail::just_child(std::move(next)));
                    co_return detail::pack_result<value_type>(
                        fnfelt::detail::convert_result<value_type>(std::move(output)));
                }
            }
            std::unreachable();
        }
    };
    /**
     * Evaluate the same outcome dispatch inline during constant evaluation.
     *
     * @par Callable parameters
     * `source`: Source IO to run. `continuation`: Error recovery continuation.
     */
    static constexpr auto run_inline = [](TSourceIO source,
                                          TContinuation continuation) -> value_type
    {
        return detail::run_or_else_inline<continuation_info>(
            std::move(source), std::move(continuation));
    };

    /**
     * Store the source and continuation for deferred execution.
     *
     * @param value Source IO and continuation.
     */
    constexpr explicit OrElseProxy(std::tuple<TSourceIO, TContinuation> value)
        : arg{std::move(value)}
    {
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
