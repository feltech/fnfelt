// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/sequence.hpp
 *
 * Collect ReaderIO results over a common environment.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <immer/array.hpp>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/sequence.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Collect a tuple of ReaderIO results in input order.
 *
 * Each source receives a copy of the environment. Tuple-valued results remain whole. Empty
 * input produces an empty tuple and accepts movable, noncopyable environments. Elements overlap
 * under one runtime scheduler; constant evaluation runs them inline. Source actions
 * run afresh on each execution.
 *
 * @tparam TState Common environment type, explicit also for empty input.
 * @tparam TTraits Validation and result traits.
 * @tparam TReaders Element types.
 * @param readers Tuple of readers to collect.
 * @return ReaderIO producing a tuple, or a rejected-operation placeholder.
 */
template <class TState, class TTraits = SequenceReaderIOTraits<>, class... TReaders>
[[nodiscard]] constexpr auto sequence(std::tuple<TReaders...> readers)
{
    using tuple_type = std::tuple<TReaders...>;
    constexpr auto reason = TTraits::validate_sequence(^^tuple_type, ^^TState);
    static_assert(reason.empty(), detail::sequence_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return reader_io::create<TState, TTraits>(
            detail::TupleSequenceAction<TState, tuple_type>{std::move(readers)});
    }
    else
    {
        return detail::SequenceError{};
    }
}

/**
 * Collect an immutable array of readers, preserving its memory policy.
 *
 * Unique input storage is consumed without element copies. Shared storage detaches first,
 * preserving retained handles and repeated lvalue runs. Elements must be copyable for this
 * copy-on-write path; result values may be move-only. Empty arrays produce empty results.
 *
 * @tparam TState Common environment type.
 * @tparam TTraits Validation and result traits.
 * @tparam TReader Element ReaderIO type.
 * @tparam TMemoryPolicy Immutable storage policy.
 * @param readers Immutable array of readers.
 * @return ReaderIO producing an immutable array of results.
 */
template <
    class TState,
    class TTraits = SequenceReaderIOTraits<>,
    class TReader,
    class TMemoryPolicy>
[[nodiscard]] constexpr auto sequence(immer::array<TReader, TMemoryPolicy> readers)
{
    using array_type = immer::array<TReader, TMemoryPolicy>;
    constexpr auto reason = TTraits::validate_sequence(^^array_type, ^^TState);
    static_assert(reason.empty(), detail::sequence_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return reader_io::create<TState, TTraits>(
            detail::ArraySequenceAction<TState, TReader, TMemoryPolicy>{std::move(readers)});
    }
    else
    {
        return detail::SequenceError{};
    }
}

/**
 * Collect a variadic pack of readers with the tuple overload's semantics.
 *
 * @tparam TState Common environment type.
 * @tparam TTraits Validation and result traits.
 * @tparam TReaders Element types.
 * @param readers Readers to collect.
 * @return ReaderIO producing the collected tuple.
 */
template <class TState, class TTraits = SequenceReaderIOTraits<>, class... TReaders>
requires(
    ((!io::detail::is_sequence_tuple(^^TReaders) && !io::detail::is_sequence_array(^^TReaders)) &&
     ...)) [[nodiscard]] constexpr auto sequence(TReaders... readers)
{
    return reader_io::sequence<TState, TTraits>(std::make_tuple(std::move(readers)...));
}

/**
 * Collect a tuple, array or pack of readers, naming the result.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TState Common environment type.
 * @tparam TReaders Collection or element types.
 * @param readers Collection or elements to collect.
 * @return Named ReaderIO producing the collected results.
 */
template <char const * name_cstr, class TState, class... TReaders>
[[nodiscard]] constexpr auto sequence(TReaders... readers)
{
    return reader_io::sequence<TState, SequenceReaderIOTraits<name_cstr>>(std::move(readers)...);
}
}  // namespace fnfelt::monad::reader_io
