// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file invocability.hpp
 *
 * Reflection predicates for callable invocability, and a run-time dispatch matching them.
 */
#pragma once

#include <meta>

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::detail
{
/**
 * Check whether a callable is directly invocable with a value as given by the receiver.
 *
 * The receiver's value category is whatever the caller passes in `receiver_meta`: a bare type
 * models an rvalue receiver, such as a stored callable moved out and invoked as an rvalue, while
 * `add_lvalue_reference(...)` models an lvalue receiver, such as a continuation applied to each
 * element in turn.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type.
 * @return True if the function accepts the value as a single argument.
 */
consteval bool is_directly_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    return is_invocable_type(receiver_meta, {value_meta});
}

/**
 * Reflect pair/tuple element arguments with the tuple expression's qualifiers.
 *
 * @param value_meta Pair or tuple expression type.
 * @return Element argument types matching std::get on that expression.
 */
consteval std::vector<std::meta::info> spread_argument_metas(std::meta::info value_meta)
{
    auto elements = template_arguments_of(dealias(remove_cvref(value_meta)));
    for (auto & element : elements)
    {
        if (is_const_type(remove_reference(value_meta)))
        {
            element = substitute(^^std::add_const_t, {element});
        }
        if (is_lvalue_reference_type(value_meta))
        {
            element = add_lvalue_reference(element);
        }
    }
    return elements;
}

/**
 * Check whether a callable is invocable with a spread value (std::apply) as given by the receiver.
 *
 * The receiver's value category is whatever the caller passes in `receiver_meta`: a bare type
 * models an rvalue receiver, while `add_lvalue_reference(...)` models an lvalue receiver.
 *
 * Only `std::pair` and `std::tuple` specialisations are spread. `std::apply` is the run-time
 * mechanism, so eligibility must match what it can actually unpack; these are also the tuple-like
 * types whose elements the reflection models. Other tuple-like types (e.g. `std::array`) are not
 * supported yet.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the continuation accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    // Alias reflections report `has_template_arguments` false, so dealias first.
    auto const bare_value = dealias(remove_cvref(value_meta));
    if (is_volatile_type(remove_reference(value_meta)) || !has_template_arguments(bare_value))
    {
        return false;
    }
    // `template_of` comparison is the same idiom as `is_io`; restrict spreading to the pair/tuple
    // protocol that `std::apply` actually unpacks.
    std::meta::info const value_template = template_of(bare_value);
    if (value_template != ^^std::pair && value_template != ^^std::tuple)
    {
        return false;
    }
    return is_invocable_type(receiver_meta, spread_argument_metas(value_meta));
}

/**
 * Check whether a callable is invocable with a value under the shared dispatch.
 *
 * This is the single predicate matching the run-time `invoke_dispatch` dispatch: the value is
 * passed directly when the callable accepts it, otherwise it is spread as if by `std::apply`
 * (direct invocation wins over spreading). The receiver's value category follows the rule
 * documented on `is_directly_invocable`.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type.
 * @return True if the callable accepts the value in either way.
 */
consteval bool is_dispatch_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    return is_directly_invocable(receiver_meta, value_meta) ||
        is_spread_invocable(receiver_meta, value_meta);
}

/**
 * Reflection of the result of invoking a callable with a value under the shared dispatch.
 *
 * Direct invocation wins, otherwise the spread invocation is reflected. The receiver's value
 * category follows the rule documented on `is_directly_invocable`.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type.
 * @return Reflection of the dispatch's invocation result, or `void` if the callable accepts the
 * value in neither way. The `void` is a diagnostic placeholder: it keeps invalid reflections
 * well-formed while the companion validators remain the single, friendly error.
 */
consteval std::meta::info dispatch_result_meta(
    std::meta::info receiver_meta, std::meta::info value_meta)
{
    if (is_directly_invocable(receiver_meta, value_meta))
    {
        return invoke_result(receiver_meta, {value_meta});
    }
    if (is_spread_invocable(receiver_meta, value_meta))
    {
        // `template_arguments_of` throws in a constant-evaluated context for a type without
        // template arguments (a hard error); `is_spread_invocable` has checked that guard.
        return invoke_result(receiver_meta, spread_argument_metas(value_meta));
    }
    return ^^void;
}

/**
 * Invoke a callable with a value: directly when that is invocable, otherwise spread the value as
 * if by `std::apply` (direct invocation wins over spreading).
 *
 * The receiver's value category is taken from `TCallable` deduction: an lvalue callable models an
 * lvalue receiver and an rvalue callable an rvalue receiver, so call sites must choose it
 * deliberately to match the reflection receiver they validated with. `std::invoke`/`std::apply`
 * are the mechanisms the matching predicates (`is_directly_invocable`/`is_spread_invocable`)
 * model, so the dispatch and its validation cannot drift apart.
 *
 * @tparam TCallable Callable type; its deduced value category is the receiver's.
 * @tparam TValue Value type.
 * @param callable Callable to invoke.
 * @param value Value (its `std::pair`/`std::tuple` elements are spread when direct invocation is
 * not possible).
 * @return The callable's invocation result.
 */
template <class TCallable, class TValue>
constexpr decltype(auto) invoke_dispatch(TCallable && callable, TValue && value)
{
    // The dispatch lives in a function template rather than inline in callers because gcc 16.2
    // rejects reflection queries inside a lambda's own `if constexpr` condition, but accepts
    // them in a function template instantiated from such a lambda.
    if constexpr (is_directly_invocable(^^TCallable, ^^TValue))
    {
        return std::invoke(FW(callable), FW(value));
    }
    else if constexpr (is_spread_invocable(^^TCallable, ^^TValue))
    {
        return std::apply(FW(callable), FW(value));
    }
    // Not reachable: validation guarantees one of the dispatches above is taken.
    std::unreachable();
}
}  // namespace fnfelt::detail

#include <fnfelt/macros_pop.hpp>
