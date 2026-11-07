// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/transform_error.hpp
 *
 * Error payload mapping for deferred IO outcomes.
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
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/result.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
template <class TSourceIO, class TTransformer>
struct TransformErrorProxy;

namespace detail
{
/// Placeholder for rejected error mappings.
struct TransformErrorError
{
};

/**
 * Infer the mapped error domain while preserving the Result layer.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TTransformer Error mapper type.
 * @return Reflection of the resulting Result.
 */
template <class TSourceIO, class TTransformer>
consteval std::meta::info transform_error_value_meta()
{
    auto const source_meta = io_value_meta(^^TSourceIO);
    auto result_type_info = fnfelt::detail::ResultTypeInfo::from(source_meta);
    auto const continuation_info =
        fnfelt::detail::ErrorContinuationInfo::from(^^TTransformer, source_meta);
    if (!continuation_info.is_reachable)
    {
        return source_meta;
    }

    // The mapper's result is opaque error data, including when it is itself a Result.
    result_type_info.error_payload_meta = continuation_info.result_meta;

    return result_type_info.type_meta();
}

/**
 * Infer synchronous value or asynchronous transport without instantiating execution.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TTransformer Error mapper type.
 * @return Reflection of the action's declared result.
 */
template <class TSourceIO, class TTransformer>
consteval std::meta::info transform_error_result_meta()
{
    if constexpr (action_is_async(io_action_meta(^^TSourceIO)))
    {
        return ^^TransformErrorProxy<TSourceIO, TTransformer>;
    }
    return transform_error_value_meta<TSourceIO, TTransformer>();
}

/**
 * Deferred mapping of the source's error payload.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TTransformer Error mapper type.
 */
template <class TSourceIO, class TTransformer>
struct TransformErrorAction
{
    /// Plain Result or asynchronous transport.
    using result_type = [:transform_error_result_meta<TSourceIO, TTransformer>():];
    /// Deferred source.
    TSourceIO source;
    /// Stored error mapper.
    TTransformer transformer;

    /**
     * Run the source and map only its error.
     *
     * @param self Stored mapping action.
     * @return Mapped Result or its asynchronous transport.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (action_is_async(io_action_meta(^^TSourceIO)))
        {
            return result_type{{FW(self).source, FW(self).transformer}};
        }
        else
        {
            auto input = FW(self).source.build().sync_wait();
            return fnfelt::detail::map_error_result<result_type>(
                FW(self).transformer, std::move(input));
        }
    }
};

/**
 * Build an error mapping diagnostic without querying unreachable mapper signatures.
 *
 * @param name IO diagnostic name.
 * @param reason Validation failure.
 * @return Friendly diagnostic.
 */
consteval std::string transform_error_error_msg(std::string_view name, std::string_view reason)
{
    std::string message = "fnfelt: IO transform_error error: ";
    message += name;
    message += ": ";
    message += reason;
    return message;
}

/**
 * Validation and construction traits for error mapping.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct TransformErrorIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the source and only a reachable mapper signature.
     *
     * @param source_meta Source IO type.
     * @param transformer_meta Error mapper type.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate_transform_error(
        std::meta::info source_meta, std::meta::info transformer_meta)
    {
        if (!is_io(source_meta))
        {
            return "source IO is not an IO";
        }
        auto const source_value = io_value_meta(source_meta);
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(source_value).validate();
            !reason.empty())
        {
            return reason;
        }
        if (!fnfelt::detail::ResultTraits::is_result_type(source_value))
        {
            return "source IO does not produce a Result";
        }
        switch (fnfelt::detail::callable_shape_of(transformer_meta))
        {
            case fnfelt::detail::CallableShape::incomplete:
                return "error mapper is not a complete type";
            case fnfelt::detail::CallableShape::reference:
                return "error mapper must not be a reference type";
            case fnfelt::detail::CallableShape::not_class_or_function_pointer:
                return "error mapper is not a class or function pointer type";
            case fnfelt::detail::CallableShape::not_movable:
                return "error mapper must be move constructible";
            case fnfelt::detail::CallableShape::valid:
                break;
        }
        auto const source_result_info = fnfelt::detail::ResultTypeInfo::from(source_value);
        // An unreachable mapper is never inspected, even when its return type is malformed.
        if (!source_result_info.has_error)
        {
            return {};
        }
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(
                                    declared_function_result_meta(transformer_meta))
                                    .validate();
            !reason.empty())
        {
            return reason;
        }
        auto const continuation_info =
            fnfelt::detail::ErrorContinuationInfo::from(transformer_meta, source_value);
        if (!continuation_info.is_reachable)
        {
            return {};
        }
        if (!continuation_info.is_valid)
        {
            return "error mapper does not accept the source IO's error";
        }
        auto const result_meta = continuation_info.result_meta;
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(result_meta).validate();
            !reason.empty())
        {
            return reason;
        }
        if (is_void_type(result_meta))
        {
            return "error mapper must return an error value";
        }
        if (is_reference_type(result_meta))
        {
            return "error mapper returns a reference, return by value instead";
        }
        if (is_io(result_meta))
        {
            return "error mapper should not return an IO";
        }
        if (is_same_type(result_meta, ^^fnfelt::Infallible))
        {
            return "error mapper must not return Infallible";
        }
        if (auto reason = fnfelt::detail::ResultTraits::validate(
                source_result_info.success_payload_meta, result_meta);
            !reason.empty())
        {
            return reason;
        }
        if (!is_move_constructible_type(result_meta))
        {
            return "error mapper returns a non-movable type";
        }
        return {};
    }
};
}  // namespace detail

/**
 * Asynchronous transport preserving the source scheduler boundary.
 *
 * @tparam TSourceIO Source IO type.
 * @tparam TTransformer Error mapper type.
 */
template <class TSourceIO, class TTransformer>
struct TransformErrorProxy : detail::TransportProxyTag
{
    /// Mapped logical Result.
    using value_type = [:detail::transform_error_value_meta<TSourceIO, TTransformer>():];
    /// Source and mapper arguments.
    std::tuple<TSourceIO, TTransformer> arg;
    /**
     * Asynchronous error mapping driver.
     *
     * @par Callable parameters
     * The unnamed first argument is supplied by libfork and is unused.
     * `source`: Source IO to run. `transformer`: Error mapper.
     */
    static constexpr auto fn = [](auto, TSourceIO source, TTransformer transformer)
        -> lf::task<detail::task_result_t<value_type>>
    {
        auto input = detail::unpack_result(co_await detail::just_child(std::move(source)));
        co_return detail::pack_result<value_type>(
            fnfelt::detail::map_error_result<value_type>(std::move(transformer), std::move(input)));
    };
    /**
     * Inline execution during constant evaluation.
     *
     * @par Callable parameters
     * `source`: Source IO to run. `transformer`: Error mapper.
     */
    static constexpr auto run_inline = [](TSourceIO source, TTransformer transformer) -> value_type
    {
        auto input = std::move(source).build().sync_wait();
        return fnfelt::detail::map_error_result<value_type>(
            std::move(transformer), std::move(input));
    };

    /**
     * Store the deferred source and mapper.
     *
     * @param arguments Source and mapper.
     */
    constexpr explicit TransformErrorProxy(std::tuple<TSourceIO, TTransformer> arguments)
        : arg{std::move(arguments)}
    {
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
