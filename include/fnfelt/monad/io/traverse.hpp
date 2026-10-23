// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/traverse.hpp
 *
 * Applicative traversal of tuple or immutable array values through an IO-producing continuation.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/detail/traverse.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/sequence.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Traverses tuple or immutable array values with an IO-producing continuation.
 *
 * The continuation runs once per element at composition as a mutable lvalue. Direct invocation
 * wins over pair/tuple spreading. Mapped IOs run afresh on execution, overlapping under one runtime
 * scheduler; constant evaluation runs tuple elements inline. Results retain input order and the
 * collection shape. Arrays preserve their memory policy.
 *
 * Inputs are taken by value and consumed. Array input elements and mapped IOs must support Immer's
 * copy-on-write storage: unique storage avoids element copies, while shared storage detaches.
 * Empty arrays still validate their element and continuation types but never invoke the
 * continuation. Empty tuples do not require a per-element invocation.
 *
 * @tparam TTraits Validation and result traits, including validate_traverse, validate_sequence,
 * validate_action and name.
 * @tparam TTuple Tuple or immutable array of values.
 * @tparam TContinuation IO-producing continuation, invoked as an lvalue.
 * @param values Values to traverse.
 * @param continuation Continuation applied once per value.
 * @return IO collecting the mapped results, or a rejected-operation placeholder.
 */
template <class TTraits, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation)
{
    using tuple_type = TTuple;
    using continuation_type = TContinuation;
    static constexpr auto reason = TTraits::validate_traverse(^^tuple_type, ^^continuation_type);
    static_assert(
        reason.empty(),
        detail::traverse_error_msg<^^tuple_type, ^^continuation_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        if constexpr (detail::is_sequence_array(^^TTuple))
        {
            return io::sequence<TTraits>(
                detail::map_traverse_array(std::move(values), continuation));
        }
        else
        {
            // Apply the continuation to every value now (once per element), then collect the mapped
            // IOs. The continuation is passed as an lvalue (it is applied once per element and
            // never moved), matching the lvalue receiver the validation and diagnostics model; the
            // direct-vs-spread dispatch is `fnfelt::detail::invoke_dispatch`'s.
            return io::sequence<TTraits>(std::apply(
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
 * Traverses tuple or immutable array values with a continuation, naming the result (applicative
 * `traverse`).
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the collection and continuation. Overloads on the first
 * template argument: a name (`char const *` non-type parameter) or a traits type.
 *
 * See the defaulted-traits overload for the traversal's runtime and validation semantics, which are
 * identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My traverse"_ss`).
 * @tparam TTuple Collection of values to traverse, deduced (unsupported input is rejected by
 * validation).
 * @tparam TContinuation Continuation mapping each value to an IO, applied once per element when
 * the composition is built.
 * @param values Collection of values to traverse, in the order their results are collected.
 * @param continuation Continuation applied to each value, producing the IO run for it.
 * @return IO running the mapped IOs and producing their collected values with
 * detail::TraverseIOTraits<name_cstr>, or detail::TraverseError if the collection and continuation
 * are invalid (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation)
{
    return io::traverse<detail::TraverseIOTraits<name_cstr>>(
        std::move(values), std::move(continuation));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
