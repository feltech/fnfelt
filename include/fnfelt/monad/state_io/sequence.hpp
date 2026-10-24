// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/sequence.hpp
 *
 * Collect StateIO results over a common environment.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <immer/array.hpp>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/sequence.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Collect a tuple of StateIO results in input order.
 *
 * Each source receives the previous source's updated state, in input order. Tuple-valued results
 * remain whole. Empty input preserves the initial state. Async elements execute sequentially
 * under one scheduler; synchronous tuples remain constant-evaluable. State may be move-only.
 *
 * @tparam TState Common environment type, explicit also for empty input.
 * @tparam TTraits Validation and result traits.
 * @tparam TStateIOs Element types.
 * @param computations Tuple of computations to collect.
 * @return StateIO producing a tuple and final state, or a rejected-operation placeholder.
 */
template <class TState, class TTraits = detail::SequenceStateIOTraits<>, class... TStateIOs>
[[nodiscard]] constexpr auto sequence(std::tuple<TStateIOs...> computations)
{
    using tuple_type = std::tuple<TStateIOs...>;
    constexpr auto reason = TTraits::validate_sequence(^^tuple_type, ^^TState);
    static_assert(reason.empty(), detail::sequence_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::create<TState, TTraits>(
            detail::TupleSequenceAction<TState, tuple_type>{std::move(computations)});
    }
    else
    {
        return detail::SequenceError{};
    }
}

/**
 * Collect an immutable array of computations, preserving its memory policy.
 *
 * Unique input storage is consumed without element copies. Shared storage detaches first,
 * preserving retained handles and repeated lvalue runs. Elements must be copyable for this
 * copy-on-write path; result values may be move-only. Empty arrays produce empty results.
 * Immer additionally requires copy assignment for trivially copyable input elements; ordinary
 * pointer/reference-capturing lambda actions can fall into this unsupported category.
 *
 * @tparam TState Common environment type.
 * @tparam TTraits Validation and result traits.
 * @tparam TStateIO Element StateIO type.
 * @tparam TMemoryPolicy Immutable storage policy.
 * @param computations Immutable array of computations.
 * @return StateIO producing an immutable array of results.
 */
template <
    class TState,
    class TTraits = detail::SequenceStateIOTraits<>,
    class TStateIO,
    class TMemoryPolicy>
[[nodiscard]] constexpr auto sequence(immer::array<TStateIO, TMemoryPolicy> computations)
{
    using array_type = immer::array<TStateIO, TMemoryPolicy>;
    constexpr auto reason = TTraits::validate_sequence(^^array_type, ^^TState);
    static_assert(reason.empty(), detail::sequence_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::create<TState, TTraits>(
            detail::ArraySequenceAction<TState, TStateIO, TMemoryPolicy>{std::move(computations)});
    }
    else
    {
        return detail::SequenceError{};
    }
}

/**
 * Collect a variadic pack of computations with the tuple overload's semantics.
 *
 * @tparam TState Common environment type.
 * @tparam TTraits Validation and result traits.
 * @tparam TStateIOs Element types.
 * @param computations Readers to collect.
 * @return StateIO producing the collected tuple.
 */
template <class TState, class TTraits = detail::SequenceStateIOTraits<>, class... TStateIOs>
requires(
    ((!io::detail::is_sequence_tuple(^^TStateIOs) && !io::detail::is_sequence_array(^^TStateIOs)) &&
     ...)) [[nodiscard]] constexpr auto sequence(TStateIOs... computations)
{
    return state_io::sequence<TState, TTraits>(std::make_tuple(std::move(computations)...));
}

/**
 * Collect a tuple, array or pack of computations, naming the result.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TState Common environment type.
 * @tparam TStateIOs Collection or element types.
 * @param computations Collection or elements to collect.
 * @return Named StateIO producing the collected results.
 */
template <char const * name_cstr, class TState, class... TStateIOs>
[[nodiscard]] constexpr auto sequence(TStateIOs... computations)
{
    return state_io::sequence<TState, detail::SequenceStateIOTraits<name_cstr>>(
        std::move(computations)...);
}
}  // namespace fnfelt::monad::state_io
