// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/or_else.hpp
 *
 * Error recovery with the ReaderIO environment retained.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <utility>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/monad/io/or_else.hpp>
#include <fnfelt/monad/reader_io/detail/and_then.hpp>
#include <fnfelt/result.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/// Placeholder for invalid ReaderIO recovery.
struct OrElseError
{
};

/**
 * Recover an inner IO using the same environment as its source.
 *
 * @tparam TState Environment type.
 * @tparam TSource Source ReaderIO type.
 * @tparam TContinuation ReaderIO-producing recovery type.
 */
template <class TState, class TSource, class TContinuation>
struct OrElseAction
{
    /// Deferred source reader.
    TSource source;
    /// Error-payload recovery.
    TContinuation continuation;

    /**
     * Bind independent environment copies and delegate routing to IO.
     *
     * @param self Stored recovery action.
     * @param state Environment for source and recovery.
     * @return Inner IO preserving success and stop without invoking recovery.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        constexpr auto source_result_info =
            fnfelt::detail::ResultTypeInfo::from(reader_io_value_meta(^^TSource));
        if constexpr (!source_result_info.has_error)
        {
            // An impossible error needs neither a callback invocation nor an environment copy.
            return FW(self).source.build(std::move(state)).io();
        }
        else
        {
            auto source_state = state;
            auto source_io = FW(self).source.build(std::move(source_state)).io();
            return std::move(source_io).or_else(
                BoundContinuation<TState, TContinuation>{FW(self).continuation, std::move(state)});
        }
    }
};

/**
 * Validate recovery before constructing its environment-bound callback.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct OrElseReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Check the source Result and only a reachable recovery signature.
     *
     * @param source_meta Source ReaderIO type.
     * @param continuation_meta Recovery callable type.
     * @return Empty on success, otherwise a validation reason.
     */
    static consteval std::string_view validate_or_else(
        std::meta::info source_meta, std::meta::info continuation_meta)
    {
        if (!is_reader_io(source_meta))
        {
            return "source is not a ReaderIO";
        }
        auto const state_meta = reader_io_state_meta(source_meta);
        auto const source_value_meta = reader_io_value_meta(source_meta);
        auto source_result_info = fnfelt::detail::ResultTypeInfo::from(source_value_meta);
        if (auto const reason = source_result_info.validate(); !reason.empty())
        {
            return reason;
        }
        if (!source_result_info.is_result)
        {
            return "source ReaderIO does not produce a Result";
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
        auto const continuation_info =
            fnfelt::detail::ErrorContinuationInfo::from(continuation_meta, source_value_meta);
        if (!continuation_info.is_reachable)
        {
            // Infallible does not provide an argument with which to inspect the handler.
            return {};
        }
        if (!is_copy_constructible_type(state_meta))
        {
            return "state must be copy constructible";
        }
        if (!continuation_info.is_valid)
        {
            return "recovery function does not accept the source error";
        }
        auto const result_meta = continuation_info.result_meta;
        if (!is_reader_io(result_meta))
        {
            return "recovery function must return a ReaderIO";
        }
        if (!is_same_type(reader_io_state_meta(result_meta), state_meta))
        {
            return "recovery function must return a ReaderIO with the same state type";
        }
        auto const result_value_meta = reader_io_value_meta(result_meta);
        if (is_void_type(result_value_meta))
        {
            return "recovery function must return a ReaderIO that produces a value";
        }
        auto const result_type_info = fnfelt::detail::ResultTypeInfo::from(result_value_meta);
        if (auto const reason = result_type_info.validate(); !reason.empty())
        {
            return reason;
        }
        if (!is_same_type(
                source_result_info.success_payload_meta, result_type_info.success_payload_meta))
        {
            return "recovery and source success must have the same payload type; use transform "
                   "before or_else to align successful payloads";
        }
        // A handled error is replaced by the recovery's error domain, rather than joined with it.
        source_result_info.has_error = false;
        source_result_info.error_payload_meta = ^^fnfelt::Infallible;
        return source_result_info.merge(result_type_info).validate();
    }
};

/**
 * Format a recovery error without inspecting unreachable callback signatures.
 *
 * @tparam source_meta Source ReaderIO reflection.
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
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
