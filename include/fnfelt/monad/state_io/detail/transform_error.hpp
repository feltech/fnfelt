// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/transform_error.hpp
 *
 * Error mapping that preserves the latest state.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <utility>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/monad/state_io/detail/transform.hpp>
#include <fnfelt/result.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for rejected error mapping.
struct TransformErrorError
{
};

/**
 * Map an error payload while leaving its paired state untouched.
 *
 * @tparam TTransformer Error mapper type.
 */
template <class TTransformer>
struct MapErrorValue
{
    /// Pure error mapper.
    TTransformer transformer;

    /**
     * Preserve successful and stopped results, mapping only the error payload.
     *
     * @param self Stored error mapper.
     * @param input Logical Result and latest state.
     * @return Mapped logical Result and latest state.
     */
    constexpr auto operator()(this auto && self, auto && input)
    {
        using result_type = [:[] consteval
                             {
                                 auto info =
                                     fnfelt::detail::ResultTypeInfo::from(^^decltype(input.first));
                                 auto const continuation_info =
                                     fnfelt::detail::ErrorContinuationInfo::from(
                                         ^^TTransformer, ^^decltype(input.first));
                                 if (continuation_info.is_reachable)
                                     info.error_payload_meta = continuation_info.result_meta;
                                 return info.type_meta();
                             }():];
        return std::pair<result_type, decltype(input.second)>{
            fnfelt::detail::map_error_result<result_type>(FW(self).transformer, FW(input).first),
            FW(input).second};
    }
};

/**
 * Bind state before mapping the logical Result's error.
 *
 * @tparam TState State type.
 * @tparam TSource Source StateIO type.
 * @tparam TTransformer Error mapper type.
 */
template <class TState, class TSource, class TTransformer>
struct TransformErrorAction
{
    /// Deferred source.
    TSource source;
    /// Pure error mapper.
    TTransformer transformer;

    /**
     * Map the source pair without moving state into the error payload.
     *
     * @param self Stored mapping action.
     * @param state Initial state.
     * @return Inner IO producing the mapped Result and latest state.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return bind_state<TState>(FW(self).source, std::move(state))
            .transform(MapErrorValue{FW(self).transformer});
    }
};

/**
 * Validation of error mapping over a StateIO logical Result.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct TransformErrorStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Validate only a reachable error mapper signature.
     *
     * @param source_meta Source StateIO reflection.
     * @param transformer_meta Error mapper reflection.
     * @return Empty on success, otherwise a validation reason.
     */
    static consteval std::string_view validate_transform_error(
        std::meta::info source_meta, std::meta::info transformer_meta)
    {
        if (!is_state_io(source_meta))
            return "source is not a StateIO";
        auto const value_meta = state_io_value_meta(source_meta);
        auto const source_result_info = fnfelt::detail::ResultTypeInfo::from(value_meta);
        if (auto const reason = source_result_info.validate(); !reason.empty())
            return reason;
        if (!source_result_info.is_result)
            return "source StateIO does not produce a Result";
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
        // An Infallible error domain provides no argument with which to inspect the mapper.
        if (!source_result_info.has_error)
            return {};
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(
                                    io::detail::declared_function_result_meta(transformer_meta))
                                    .validate();
            !reason.empty())
            return reason;
        auto const continuation_info =
            fnfelt::detail::ErrorContinuationInfo::from(transformer_meta, value_meta);
        if (!continuation_info.is_valid)
            return "error mapper does not accept the source StateIO's error";
        auto const result_meta = continuation_info.result_meta;
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(result_meta).validate();
            !reason.empty())
            return reason;
        if (!is_complete_type(result_meta) || !is_object_type(result_meta) ||
            is_reference_type(result_meta) || !is_move_constructible_type(result_meta) ||
            io::detail::is_io(result_meta) || is_state_io(result_meta) ||
            is_same_type(result_meta, ^^fnfelt::Infallible))
            return "error mapper must return an owned error value";
        return fnfelt::detail::ErrorTraits::validate(result_meta);
    }
};

/**
 * Format an error mapper diagnostic without querying its signature.
 *
 * @tparam source_meta Source StateIO reflection.
 * @tparam transformer_meta Error mapper reflection.
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Contextual diagnostic.
 */
template <std::meta::info source_meta, std::meta::info transformer_meta>
consteval std::string transform_error_error_msg(std::string_view name, std::string_view reason)
{
    return transform_error_msg<source_meta, transformer_meta>(name, reason, "transform_error");
}
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
