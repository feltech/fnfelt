// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file invoke.hpp
 *
 * Invocation helpers.
 *
 * Deliberately not ported from the source repo (deferred to Stage 3): specialisation_of and
 * unspecialise_t - the latter is superseded by fnfelt::Unspecialise and fnfelt::rebind_t.
 */
#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include <fnfelt/macros_push.hpp>

namespace fnfelt
{
namespace detail
{
/**
 * Detects whether applying a tuple-like type's elements to a callable is well-formed.
 *
 * Primary template - false for anything that is not a type-parameter-only template specialization.
 *
 * @tparam TFunc Callable to apply the tuple elements to.
 * @tparam TValue Tuple-like type whose elements are spread as arguments.
 */
template <class TFunc, class TValue>
struct IsApplicableTo : std::false_type
{
};

/**
 * True when TFunc is invocable with the elements of TTuple. (detail)
 *
 * @tparam TFunc Callable to apply the tuple elements to.
 * @tparam TTuple Tuple-like type whose elements are spread as arguments.
 * @tparam TArgs Element types of TTuple.
 */
template <class TFunc, template <class...> class TTuple, class... TArgs>
struct IsApplicableTo<TFunc, TTuple<TArgs...>> : std::is_invocable<TFunc, TArgs...>
{
};

/**
 * Always false, dependent on its template arguments. (detail)
 *
 * Used to make a static_assert fire only when its enclosing template is instantiated.
 *
 * @tparam T Any type.
 */
template <class... T>
inline constexpr bool always_false = false;

/**
 * Result of applying a tuple-like type's elements to a callable. (detail)
 *
 * Primary template - asserts with a readable message when the callable cannot be invoked with the
 * tuple elements.
 *
 * @tparam TFunc Callable to apply the tuple elements to.
 * @tparam TValue Tuple-like type whose elements are spread as arguments.
 */
template <class TFunc, class TValue>
struct ApplyResult
{
    static_assert(
        always_false<TFunc, TValue>,
        "fnfelt: invoke_or_apply_result_t requires a value that is directly callable, or a "
        "callable and a tuple of arguments it accepts");
};

/**
 * Result of invoking a callable with the elements of a tuple-like type. (detail)
 *
 * @tparam TFunc Callable to apply the tuple elements to.
 * @tparam TTuple Tuple-like type whose elements are spread as arguments.
 * @tparam TArgs Element types of TTuple.
 */
template <class TFunc, template <class...> class TTuple, class... TArgs>
requires std::is_invocable_v<TFunc, TArgs...> struct ApplyResult<TFunc, TTuple<TArgs...>>
    : std::invoke_result<TFunc, TArgs...>
{
};

/**
 * Computes a callable's result when invoked with a value, or with a tuple's elements. (detail)
 *
 * Dispatch struct behind the public invoke_or_apply_result_t alias - direct invocability wins over
 * tuple-spreading (see the public alias for the precedence contract).
 *
 * @tparam TFunc Callable to invoke.
 * @tparam TValue Value(s) to invoke TFunc with.
 */
template <class TFunc, class TValue>
struct InvokeOrApplyResult : ApplyResult<TFunc, TValue>
{
};

/**
 * Invokes TFunc with TValue directly. (specialization)
 *
 * @tparam TFunc Callable to invoke.
 * @tparam TValue Value to invoke TFunc with.
 */
template <class TFunc, class TValue>
requires std::invocable<TFunc, TValue> struct InvokeOrApplyResult<TFunc, TValue>
    : std::invoke_result<TFunc, TValue>
{
};
}  // namespace detail

/**
 * Concept satisfied when a callable's arguments are the elements of a tuple-like value.
 *
 * Used to dispatch between invoking a callable directly with a value and spreading a tuple-like
 * value's elements as arguments. Both the callable and value are decayed first.
 *
 * Matches ANY type-parameter-only template specialization, not just std::tuple - ALL type arguments
 * are spread, e.g. applicable_with<F, std::pair<int, double>> tests F with (int, double), while
 * applicable_with<F, std::vector<int>> tests F with (int, std::allocator<int>). Templates with
 * non-type parameters, e.g. std::array<T, N>, do not match.
 *
 * @tparam TFunc Callable to test.
 * @tparam TValue Tuple-like value whose elements are spread as arguments.
 */
template <class TFunc, class TValue>
concept applicable_with = detail::IsApplicableTo<std::decay_t<TFunc>, std::decay_t<TValue>>::value;

/**
 * The result type of invoking a callable with a value, or with a tuple's elements.
 *
 * TValue is decayed before dispatch. If TFunc is directly invocable with the decayed TValue, yields
 * @c std::invoke_result_t<TFunc, TValue> - direct invocability wins over tuple-spreading.
 * Otherwise, if the decayed TValue is a type-parameter-only template specialization whose
 * elements TFunc accepts, yields @c std::invoke_result_t<TFunc, TArgs...>. Otherwise instantiating
 * @c type triggers a readable static_assert.
 *
 * @par Dispatch precedence
 * When a callable is invocable both directly with a tuple-like value and with that value's
 * elements, the direct (non-spreading) branch is chosen.
 *
 * @tparam TFunc Callable to invoke.
 * @tparam TValue Value(s) to invoke TFunc with.
 */
template <class TFunc, class TValue>
using invoke_or_apply_result_t =
    detail::InvokeOrApplyResult<TFunc, std::remove_cvref_t<TValue>>::type;
}  // namespace fnfelt

#include <fnfelt/macros_pop.hpp>
