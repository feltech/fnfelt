// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/split.hpp
 *
 * Element-wise StateIO continuation adapters.
 */
#pragma once
#include <meta>

#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/detail/split.hpp>
#include <fnfelt/monad/state_io/detail/sequence.hpp>
#include <fnfelt/monad/state_io/sequence.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for rejected shared-input continuation application.
struct SplitError
{
};

/**
 * Validate continuations paired with distinct logical inputs.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr>
struct SplitStateIOTraits : SequenceStateIOTraits<name_cstr>
{
    /**
     * Check state and stored continuation types before input is available.
     *
     * @param functions_meta Tuple of continuation types.
     * @param state_meta Common state type.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_split_factory(
        std::meta::info functions_meta, std::meta::info state_meta)
    {
        if (auto const reason =
                SequenceStateIOTraits<name_cstr>::validate_sequence(^^std::tuple<>, state_meta);
            !reason.empty())
        {
            return reason;
        }
        return io::detail::SplitIOTraits<name_cstr>::validate_split_factory(functions_meta);
    }

    /**
     * Check matching arity, element invocation and collected result states.
     *
     * @param functions_meta Tuple of continuation types.
     * @param value_meta Tuple of owned input values.
     * @param state_meta Common state type.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_split(
        std::meta::info functions_meta, std::meta::info value_meta, std::meta::info state_meta)
    {
        if (!io::detail::is_sequence_tuple(functions_meta) ||
            !io::detail::is_sequence_tuple(value_meta))
        {
            return "continuations and values must be std::tuples";
        }
        auto const functions = template_arguments_of(dealias(functions_meta));
        auto const values = template_arguments_of(dealias(value_meta));
        if (functions.size() != values.size())
        {
            return "continuation and value tuples must have equal sizes";
        }
        std::vector<std::meta::info> results;
        for (std::size_t idx = 0; idx < functions.size(); ++idx)
        {
            auto const fn_meta = functions[idx];
            if (fnfelt::detail::callable_shape_of(fn_meta) !=
                    fnfelt::detail::CallableShape::valid ||
                !fnfelt::detail::is_dispatch_invocable(fn_meta, values[idx]))
            {
                return "continuation must accept its corresponding input value";
            }
            auto const result_meta = fnfelt::detail::dispatch_result_meta(fn_meta, values[idx]);
            if (auto const reason =
                    SequenceStateIOTraits<name_cstr>::validate_element(result_meta, state_meta);
                !reason.empty())
            {
                return reason;
            }
            results.push_back(result_meta);
        }
        return SequenceStateIOTraits<name_cstr>::validate_sequence(
            substitute(^^std::tuple, results), state_meta);
    }
};

/**
 * Pair distinct inputs with continuations and sequence their StateIOs.
 *
 * @tparam TState Threaded state type.
 * @tparam TTraits Validation and result traits.
 * @tparam TFunctions Tuple of stored continuations.
 */
template <class TState, class TTraits, class TFunctions>
struct SplitAction
{
    /// Owned continuations.
    TFunctions functions;
    /**
     * Invoke each continuation with its owned corresponding tuple element.
     *
     * @param self Continuation adapter, copied for lvalue use and moved for rvalue use.
     * @param values Owned input tuple.
     * @return StateIO collecting branch results sequentially.
     */
    template <class TValues>
    constexpr auto operator()(this auto && self, TValues values)
    {
        constexpr auto reason = TTraits::validate_split(^^TFunctions, ^^TValues, ^^TState);
        static_assert(reason.empty(), sequence_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            auto functions = FW(self).functions;
            auto mapped = [&]<std::size_t... idx>(std::index_sequence<idx...>)
            {
                return std::make_tuple(
                    fnfelt::detail::invoke_dispatch(
                        std::get<idx>(std::move(functions)), std::get<idx>(std::move(values)))...);
            }(std::make_index_sequence<std::tuple_size_v<TValues>>{});
            return state_io::sequence<TState, TTraits>(std::move(mapped));
        }
        else
        {
            return SplitError{};
        }
    }
};
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
