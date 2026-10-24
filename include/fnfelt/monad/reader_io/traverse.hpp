// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/traverse.hpp
 *
 * Traverse tuple or immutable array values through ReaderIO-producing continuations.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/reader_io/detail/traverse.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>
#include <fnfelt/monad/reader_io/sequence.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Map tuple or immutable array values to ReaderIOs, then collect their results in input order.
 *
 * The continuation runs once per element at composition as a mutable lvalue. Direct invocation
 * wins over pair/tuple spreading. Its invocation order is unspecified. Mapped ReaderIO actions
 * run afresh at execution with copies of the same environment. Mapped actions overlap under one
 * runtime scheduler; constant evaluation runs them inline. An empty tuple produces an empty tuple
 * without invoking the continuation.
 *
 * Arrays preserve their memory policy and consume unique input storage without element copies;
 * shared storage detaches first. Input elements and mapped actions must support Immer's
 * copy-on-write storage. Empty arrays validate their element types without invoking the
 * continuation.
 *
 * @tparam TState Common environment type, explicit also for empty input.
 * @tparam TTraits Validation and result traits.
 * @tparam TTuple Tuple or immutable array of values.
 * @tparam TContinuation ReaderIO-producing continuation.
 * @param values Values to traverse.
 * @param continuation Continuation applied once per value.
 * @return ReaderIO collecting the mapped results.
 */
template <
    class TState,
    class TTraits = detail::TraverseReaderIOTraits<>,
    class TTuple,
    class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation)
{
    constexpr auto reason = TTraits::validate_traverse(^^TTuple, ^^TContinuation, ^^TState);
    static_assert(reason.empty(), detail::traverse_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        if constexpr (io::detail::is_sequence_array(^^TTuple))
        {
            return reader_io::sequence<TState, TTraits>(
                io::detail::map_traverse_array(std::move(values), continuation));
        }
        else
        {
            return reader_io::sequence<TState, TTraits>(std::apply(
                [&](auto &&... value)
                {
                    return std::make_tuple(
                        fnfelt::detail::invoke_dispatch(continuation, FW(value))...);
                },
                std::move(values)));
        }
    }
    else
    {
        return detail::TraverseError{};
    }
}

/**
 * Traverse a tuple or immutable array, naming the result.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TState Common environment type.
 * @tparam TTuple Tuple or immutable array of values.
 * @tparam TContinuation ReaderIO-producing continuation.
 * @param values Values to traverse.
 * @param continuation Continuation applied once per value.
 * @return Named ReaderIO collecting the results.
 */
template <char const * name_cstr, class TState, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation)
{
    return reader_io::traverse<TState, detail::TraverseReaderIOTraits<name_cstr>>(
        std::move(values), std::move(continuation));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
