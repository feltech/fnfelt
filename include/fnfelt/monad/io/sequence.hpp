// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file monad/io/sequence.hpp
 *
 * Collecting a tuple or array of IOs into an IO.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <immer/array.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/sequence.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Sequences an immutable array of IOs into an IO of their values, in input order.
 *
 * Execution is lazy. An empty input produces an empty array. Elements are forked into indexed
 * result slots and joined under one runtime scheduler.
 * Each value is collected whole, without tuple spreading. The input memory policy is preserved.
 *
 * Element IOs are moved from transient storage when run. Unique storage is consumed without
 * copying elements; shared storage is copied first, preserving other array handles and reusable
 * lvalue sequences. Element IOs must still be copyable for this copy-on-write path, even if the
 * storage is unique at runtime. Result values are moved into fresh storage and may be move-only;
 * array operations that copy elements still require copyable values.
 *
 * @tparam TTraits Validation and result IO traits, defaulting to detail::SequenceIOTraits.
 * @tparam TIO Element IO type.
 * @tparam TMemoryPolicy Immutable array memory policy.
 * @param ios Immutable array of IOs to run.
 * @return IO producing the collected immutable array, or detail::SequenceError for invalid input
 * (also rejected by a friendly static_assert).
 */
template <class TTraits = detail::SequenceIOTraits<>, class TIO, class TMemoryPolicy>
[[nodiscard]] constexpr auto sequence(immer::array<TIO, TMemoryPolicy> ios)
{
    using array_type = immer::array<TIO, TMemoryPolicy>;
    static constexpr auto reason = TTraits::validate_sequence(^^array_type);
    static_assert(reason.empty(), detail::sequence_error_msg<^^array_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return io::create<TTraits>(detail::ArraySequenceAction<TIO, TMemoryPolicy>{std::move(ios)});
    }
    else
    {
        return detail::SequenceError{};
    }
}

/**
 * Sequences an immutable array of IOs, naming the result.
 *
 * Runtime and validation semantics are identical to the defaulted-traits array overload.
 *
 * @tparam name_cstr Null-terminated name with static storage duration.
 * @tparam TIO Element IO type.
 * @tparam TMemoryPolicy Immutable array memory policy.
 * @param ios Immutable array of IOs to run.
 * @return Named IO producing the collected immutable array.
 */
template <char const * name_cstr, class TIO, class TMemoryPolicy>
[[nodiscard]] constexpr auto sequence(immer::array<TIO, TMemoryPolicy> ios)
{
    return io::sequence<detail::SequenceIOTraits<name_cstr>>(std::move(ios));
}

/**
 * Sequences a tuple of IOs into an IO of their collected values (applicative).
 *
 * The returned IO runs the element IOs and produces `std::tuple` of their values in the same order
 * as the tuple. An empty tuple yields an IO of an empty tuple. Each element contributes exactly one
 * element to the result, so a value that is itself a `std::pair` or `std::tuple` is collected whole
 * rather than spread.
 *
 * At runtime all elements are forked into indexed result slots and joined under one scheduler.
 * Constant evaluation runs the elements inline in input order.
 *
 * The tuple is taken by value and moved into the result IO's action, so the element IOs move with
 * it and a move-only element is supported. An rvalue tuple moves its elements into that parameter
 * and an lvalue tuple is copied, so an lvalue tuple of move-only IOs is not accepted.
 *
 * An invalid tuple (one that is not a `std::tuple`, or with an element that is not an IO) is
 * rejected by a friendly static_assert and yields `detail::SequenceError`.
 *
 * @tparam TTraits Traits used to validate the tuple and for the result IO, defaulting to
 * detail::SequenceIOTraits.
 * @tparam TIOs Element IO types, deduced (an element that is not an IO is rejected by validation).
 * @param ios Tuple of IOs to run, in the order their values are collected.
 * @return IO running the element IOs and producing the tuple of their values, or
 * detail::SequenceError if the tuple is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits = detail::SequenceIOTraits<>, class... TIOs>
[[nodiscard]] constexpr auto sequence(std::tuple<TIOs...> ios)
{
    using tuple_type = std::tuple<TIOs...>;
    static constexpr auto reason = TTraits::validate_sequence(^^tuple_type);
    static_assert(reason.empty(), detail::sequence_error_msg<^^tuple_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::TupleSequenceAction<TIOs...>;
        return io::create<TTraits>(action_type{std::move(ios)});
    }
    else
    {
        return detail::SequenceError{};
    }
}

/**
 * Sequences a tuple of IOs, naming the result (applicative).
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the elements. Overloads on the first template argument: a name
 * (`char const *` non-type parameter) or a traits type.
 *
 * See the defaulted-traits overload for the sequence's runtime and validation semantics, which are
 * identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My sequence"_ss`).
 * @tparam TIOs Element IO types, deduced (an element that is not an IO is rejected by validation).
 * @param ios Tuple of IOs to run, in the order their values are collected.
 * @return IO running the element IOs and producing the tuple of their values with
 * detail::SequenceIOTraits<name_cstr>, or detail::SequenceError if the tuple is invalid (which is
 * also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class... TIOs>
[[nodiscard]] constexpr auto sequence(std::tuple<TIOs...> ios)
{
    return io::sequence<detail::SequenceIOTraits<name_cstr>>(std::move(ios));
}

/**
 * Sequences a variadic pack of IOs into an IO of their collected values (applicative).
 *
 * Wraps the arguments in a `std::tuple` and forwards it to the tuple overload, so the runtime and
 * validation semantics, including the friendly static_assert, are identical.
 *
 * The constraint excludes a pack containing a `std::tuple` so that a tuple argument stays on the
 * tuple overload: without it, both overloads would match a tuple argument with equal conversion
 * sequences and the call would be ambiguous. Non-IO arguments are not excluded here; they are
 * rejected by the tuple overload's validation.
 *
 * @tparam TTraits Traits used to validate the wrapped tuple and for the result IO, defaulting to
 * detail::SequenceIOTraits.
 * @tparam TIOs Element IO types, deduced.
 * @param ios IOs to run, in the order their values are collected.
 * @return IO running the element IOs and producing the tuple of their values, or
 * detail::SequenceError if an element is not an IO (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits = detail::SequenceIOTraits<>, class... TIOs>
requires(((!detail::is_sequence_tuple(^^TIOs) && !detail::is_sequence_array(^^TIOs)) && ...))
    [[nodiscard]] constexpr auto sequence(TIOs... ios)
{
    return io::sequence<TTraits>(std::make_tuple(std::move(ios)...));
}

/**
 * Sequences a variadic pack of IOs, naming the result (applicative).
 *
 * Overloads on the first template argument: a name (`char const *` non-type parameter) or a traits
 * type.
 *
 * See the defaulted-traits variadic overload for the wrapping, and the defaulted-traits tuple
 * overload for the sequence's runtime and validation semantics, which are identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My sequence"_ss`).
 * @tparam TIOs Element IO types, deduced.
 * @param ios IOs to run, in the order their values are collected.
 * @return IO running the element IOs and producing the tuple of their values with
 * detail::SequenceIOTraits<name_cstr>, or detail::SequenceError if an element is not an IO (which
 * is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class... TIOs>
requires(((!detail::is_sequence_tuple(^^TIOs) && !detail::is_sequence_array(^^TIOs)) && ...))
    [[nodiscard]] constexpr auto sequence(TIOs... ios)
{
    return io::sequence<detail::SequenceIOTraits<name_cstr>>(std::make_tuple(std::move(ios)...));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
