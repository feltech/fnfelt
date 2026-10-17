// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file sequence.hpp
 *
 * Free variadic `sequence` operation for the IO monad.
 *
 * The fold and validation machinery the operation is built from live in `detail/sequence.hpp`.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/sequence.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Sequences a tuple of IOs into an IO of their collected values (applicative).
 *
 * The returned IO runs the element IOs and produces `std::tuple` of their values in the same order
 * as the tuple. An empty tuple yields an IO of an empty tuple. Each element contributes exactly one
 * element to the result, so a value that is itself a `std::pair` or `std::tuple` is collected whole
 * rather than spread.
 *
 * The elements are composed through `ap`, so an asynchronous element makes the whole sequence
 * asynchronous and the elements overlap on the pool under a single top-level scheduler; a fully
 * synchronous sequence runs inline.
 *
 * The tuple is taken by value and moved into the result IO's action, so the element IOs move with
 * it and a move-only element is supported. An rvalue tuple moves its elements into that parameter
 * and an lvalue tuple is copied, so an lvalue tuple of move-only IOs is not accepted.
 *
 * An invalid tuple (one that is not a `std::tuple`, or with an element that is not an IO) is
 * rejected by a friendly static_assert and yields `detail::SequenceError`.
 *
 * @tparam TTraits Traits used to validate the tuple and for the result IO, defaulting to
 * SequenceIOTraits.
 * @tparam TIOs Element IO types, deduced (an element that is not an IO is rejected by validation).
 * @param ios Tuple of IOs to run, in the order their values are collected.
 * @return IO running the element IOs and producing the tuple of their values, or
 * detail::SequenceError if the tuple is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits = SequenceIOTraits<>, class... TIOs>
[[nodiscard]] constexpr auto sequence(std::tuple<TIOs...> ios)
{
    using tuple_type = std::tuple<TIOs...>;
    static constexpr auto reason = TTraits::validate_sequence(^^tuple_type);
    static_assert(reason.empty(), detail::sequence_error_msg<^^tuple_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::SequenceAction<TIOs...>;
        return create<TTraits>(action_type{std::move(ios)});
    }
    if constexpr (!reason.empty())
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
 * SequenceIOTraits<name_cstr>, or detail::SequenceError if the tuple is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <char const * name_cstr, class... TIOs>
[[nodiscard]] constexpr auto sequence(std::tuple<TIOs...> ios)
{
    return sequence<SequenceIOTraits<name_cstr>>(std::move(ios));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
