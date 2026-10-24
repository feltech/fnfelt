// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/split.hpp
 *
 * Apply StateIO continuations to distinct tuple elements.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/state_io/detail/split.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Adapt continuations to consume corresponding tuple values and collect their StateIO results.
 *
 * Calling the adapter takes ownership of a tuple with the same size as the continuation pack,
 * then invokes each continuation with its moved corresponding element, preferring direct invocation
 * to pair/tuple spreading. Continuation invocation order is unspecified.
 * The resulting StateIOs execute sequentially in input order, threading updated state. An empty
 * adapter produces an empty tuple and preserves state. Lvalue adapter calls copy stored
 * continuations; rvalue calls move them. Input elements and state may be move-only.
 *
 * @param functions Continuations returning StateIOs over TState.
 * @return Callable producing the collected StateIO for its input tuple.
 */
template <class TState, class TTraits = detail::SplitStateIOTraits<>, class... TFunctions>
[[nodiscard]] constexpr auto split(TFunctions... functions)
{
    constexpr auto reason = TTraits::validate_split_factory(^^std::tuple<TFunctions...>, ^^TState);
    static_assert(reason.empty(), detail::sequence_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return detail::SplitAction<TState, TTraits, std::tuple<TFunctions...>>{
            std::tuple<TFunctions...>{std::move(functions)...}};
    }
    else
    {
        return detail::SplitError{};
    }
}

/**
 * Build an element-wise continuation adapter naming its result.
 *
 * @param functions StateIO-producing continuations.
 * @return Named split adapter.
 */
template <char const * name_cstr, class TState, class... TFunctions>
[[nodiscard]] constexpr auto split(TFunctions... functions)
{
    return state_io::split<TState, detail::SplitStateIOTraits<name_cstr>>(std::move(functions)...);
}
}  // namespace fnfelt::monad::state_io
