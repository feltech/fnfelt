// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/fanout.hpp
 *
 * Shared-input ReaderIO continuation adapters.
 */
#pragma once
#include <meta>

#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/reader_io/detail/sequence.hpp>
#include <fnfelt/monad/reader_io/sequence.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/// Placeholder for rejected shared-input continuation application.
struct FanoutError
{
};

/**
 * Validate continuations sharing a stable logical input.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr = default_reader_io_name>
struct FanoutReaderIOTraits : SequenceReaderIOTraits<name_cstr>
{
    /**
     * Check state and stored continuation types before input is available.
     *
     * @param functions_meta Tuple of continuation types.
     * @param state_meta Common state type.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_fanout_factory(
        std::meta::info functions_meta, std::meta::info state_meta)
    {
        if (auto const reason =
                SequenceReaderIOTraits<name_cstr>::validate_sequence(^^std::tuple<>, state_meta);
            !reason.empty())
        {
            return reason;
        }
        if (!io::detail::is_sequence_tuple(functions_meta))
        {
            return "continuations must be a std::tuple";
        }
        for (auto const fn_meta : template_arguments_of(dealias(functions_meta)))
        {
            if (fnfelt::detail::callable_shape_of(fn_meta) != fnfelt::detail::CallableShape::valid)
            {
                return "continuation must be a complete move-constructible callable";
            }
        }
        return {};
    }

    /**
     * Check exact shared-input invocation and collected result states.
     *
     * @param functions_meta Tuple of continuation types.
     * @param value_meta Shared input expression type, as an lvalue.
     * @param state_meta Common state type.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_fanout(
        std::meta::info functions_meta, std::meta::info value_meta, std::meta::info state_meta)
    {
        if (!io::detail::is_sequence_tuple(functions_meta))
        {
            return "continuations must be a std::tuple";
        }
        std::vector<std::meta::info> results;
        for (auto const fn_meta : template_arguments_of(dealias(functions_meta)))
        {
            if (fnfelt::detail::callable_shape_of(fn_meta) !=
                    fnfelt::detail::CallableShape::valid ||
                !fnfelt::detail::is_dispatch_invocable(fn_meta, value_meta))
            {
                return "continuation must accept the shared input as an lvalue";
            }
            auto const result_meta = fnfelt::detail::dispatch_result_meta(fn_meta, value_meta);
            if (auto const reason =
                    SequenceReaderIOTraits<name_cstr>::validate_element(result_meta, state_meta);
                !reason.empty())
            {
                return reason;
            }
            results.push_back(result_meta);
        }
        return SequenceReaderIOTraits<name_cstr>::validate_sequence(
            substitute(^^std::tuple, results), state_meta);
    }
};

/**
 * Map one stable input through continuations and sequence their ReaderIOs.
 *
 * @tparam TState Environment type.
 * @tparam TTraits Validation and result traits.
 * @tparam TFunctions Tuple of stored continuations.
 */
template <class TState, class TTraits, class TFunctions>
struct FanoutContinuation
{
    /// Owned continuations.
    TFunctions functions;
    /**
     * Invoke every continuation with the same stable lvalue input.
     *
     * @param self Continuation adapter, copied for lvalue use and moved for rvalue use.
     * @param value Shared input, never moved into a branch.
     * @return ReaderIO collecting branch results.
     */
    template <class TValue>
    constexpr auto operator()(this auto && self, TValue && value)
    {
        constexpr auto reason =
            TTraits::validate_fanout(^^TFunctions, ^^decltype((value)), ^^TState);
        static_assert(reason.empty(), sequence_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            auto functions = FW(self).functions;
            return reader_io::sequence<TState, TTraits>(std::apply(
                [&](auto &&... fn)
                { return std::make_tuple(fnfelt::detail::invoke_dispatch(FW(fn), value)...); },
                std::move(functions)));
        }
        else
        {
            return FanoutError{};
        }
    }
};
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
