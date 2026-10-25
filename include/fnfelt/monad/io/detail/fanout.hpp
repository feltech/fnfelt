// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/fanout.hpp
 *
 * Shared-input IO continuation adapters.
 */
#pragma once
#include <meta>

#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/detail/sequence.hpp>
#include <fnfelt/monad/io/sequence.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io::detail
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
template <char const * name_cstr = default_io_name>
struct FanoutIOTraits : SequenceIOTraits<name_cstr>
{
    /**
     * Check stored continuation types before input is available.
     *
     * @param functions_meta Tuple of continuation types.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_fanout_factory(std::meta::info functions_meta)
    {
        if (auto const reason = SequenceIOTraits<name_cstr>::validate_sequence(^^std::tuple<>);
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
     * Check exact shared-input invocation and collected results.
     *
     * @param functions_meta Tuple of continuation types.
     * @param value_meta Shared input expression type, as an lvalue.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_fanout(
        std::meta::info functions_meta, std::meta::info value_meta)
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
            results.push_back(result_meta);
        }
        return SequenceIOTraits<name_cstr>::validate_sequence(substitute(^^std::tuple, results));
    }
};

/**
 * Map one stable input through continuations and sequence their IOs.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TFunctions Tuple of stored continuations.
 */
template <class TTraits, class TFunctions>
struct FanoutContinuation
{
    /// Owned continuations.
    TFunctions functions;
    /**
     * Invoke every continuation with the same stable lvalue input.
     *
     * @param self Continuation adapter, copied for lvalue use and moved for rvalue use.
     * @param value Shared input, never moved into a branch.
     * @return IO collecting branch results.
     */
    template <class TValue>
    constexpr auto operator()(this auto && self, TValue && value)
    {
        constexpr auto reason = TTraits::validate_fanout(^^TFunctions, ^^decltype((value)));
        static_assert(reason.empty(), sequence_error_msg<^^TFunctions>(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            auto functions = FW(self).functions;
            return io::sequence<TTraits>(std::apply(
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
}  // namespace fnfelt::monad::io::detail

#include <fnfelt/macros_pop.hpp>
