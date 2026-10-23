// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/traverse.hpp
 *
 * Applicative traversal of a tuple through an IO-producing continuation.
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
 * Traverses a tuple of values with a continuation, producing an IO of their collected results
 * (applicative `traverse`).
 *
 * The continuation maps each value to an IO, and the mapped IOs are collected with `sequence` into
 * an IO of a `std::tuple` in input order, so the identity continuation recovers `sequence`. An
 * empty tuple yields an IO of an empty tuple; the continuation is never applied, though its shape
 * checks still run.
 *
 * The continuation is applied once per element when the composition is built, and the mapped IOs
 * are stored and re-run on each run. It is taken by value and invoked as a non-const lvalue, never
 * moved, so move-only continuations (passed as an rvalue) and mutable functors work. Application
 * order across elements is unspecified (gcc 16.2 applies it right-to-left), but the mapped IOs
 * retain their input positions in the result.
 *
 * It must be callable with each value directly or spread (a `std::pair` or `std::tuple` element is
 * spread when direct invocation does not work, and direct invocation wins) and return an IO that
 * produces a value. A first argument that is not a `std::tuple`, or a tuple with an element the
 * continuation cannot accept or whose mapped IO produces no value, is rejected by the friendly
 * static_assert and yields `detail::TraverseError`.
 *
 * The tuple is taken by value and moved into the composition: an rvalue moves move-only element
 * values into the continuation, an lvalue is copied.
 *
 * Mapped IOs overlap under one runtime scheduler. Constant evaluation runs them inline.
 *
 * @tparam TTraits Traits used to validate the tuple and continuation and for the result IO,
 * defaulting to detail::TraverseIOTraits. It must provide validate_traverse, and - because the
 * result is built by composing sequence<TTraits> - validate_sequence and validate_action, plus a
 * static name member used in the diagnostic; detail::TraverseIOTraits provides all four.
 * @tparam TTuple Tuple of values to traverse, deduced (a non-tuple argument is rejected by
 * validation).
 * @tparam TContinuation Continuation mapping each value to an IO, applied once per element when
 * the composition is built.
 * @param values Tuple of values to traverse, in the order their results are collected.
 * @param continuation Continuation applied to each value, producing the IO run for it.
 * @return IO running the mapped IOs and producing the tuple of their values, or
 * detail::TraverseError if the tuple and continuation are invalid (which is also rejected by a
 * friendly static_assert).
 */
template <class TTraits, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation)
{
    // The tuple is taken by value: an lvalue argument is copied and an rvalue is moved.
    // The by-value parameter is already decayed: an lvalue is copied, an rvalue is moved, and a
    // function lvalue deduces to a function pointer. Deducing a single type rather than a
    // `std::tuple` specialisation keeps a non-tuple argument viable for overload resolution, so
    // it reaches `validate_traverse` and its friendly static_assert.
    using tuple_type = TTuple;
    using continuation_type = TContinuation;
    static constexpr auto reason = TTraits::validate_traverse(^^tuple_type, ^^continuation_type);
    static_assert(
        reason.empty(),
        detail::traverse_error_msg<^^tuple_type, ^^continuation_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        // Apply the continuation to every value now (once per element), then collect the mapped
        // IOs. The continuation is passed as an lvalue (it is applied once per element and never
        // moved), matching the lvalue receiver the validation and diagnostics model; the
        // direct-vs-spread dispatch is `fnfelt::detail::invoke_dispatch`'s.
        return io::sequence<TTraits>(std::apply(
            [&](auto &&... value)
            {
                return std::make_tuple(fnfelt::detail::invoke_dispatch(continuation, FW(value))...);
            },
            std::move(values)));
    }
    else
    {
        return detail::TraverseError{};
    }
}

/**
 * Traverses a tuple of values with a continuation, naming the result (applicative `traverse`).
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the tuple and continuation. Overloads on the first template
 * argument: a name (`char const *` non-type parameter) or a traits type.
 *
 * See the defaulted-traits overload for the traversal's runtime and validation semantics, which are
 * identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My traverse"_ss`).
 * @tparam TTuple Tuple of values to traverse, deduced (a non-tuple argument is rejected by
 * validation).
 * @tparam TContinuation Continuation mapping each value to an IO, applied once per element when
 * the composition is built.
 * @param values Tuple of values to traverse, in the order their results are collected.
 * @param continuation Continuation applied to each value, producing the IO run for it.
 * @return IO running the mapped IOs and producing the tuple of their values with
 * detail::TraverseIOTraits<name_cstr>, or detail::TraverseError if the tuple and continuation are
 * invalid (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation)
{
    return io::traverse<detail::TraverseIOTraits<name_cstr>>(
        std::move(values), std::move(continuation));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
