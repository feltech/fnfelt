// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/fanout.hpp
 *
 * Apply StateIO continuations to a shared logical input.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/state_io/detail/fanout.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Adapt continuations to consume a shared stable input and collect their StateIO results.
 *
 * Calling the adapter eagerly invokes each continuation with the same input as an lvalue,
 * preferring direct invocation to tuple spreading. Continuation invocation order is unspecified.
 * The resulting StateIOs execute sequentially in input order, threading updated state. An empty
 * adapter produces an empty tuple and preserves state. Lvalue adapter calls copy stored
 * continuations; rvalue calls move them. The shared input may be move-only.
 *
 * @param functions Continuations returning StateIOs over TState.
 * @return Callable producing the collected StateIO for its shared input.
 */
template <class TState, class TTraits = detail::FanoutStateIOTraits<>, class... TFunctions>
[[nodiscard]] constexpr auto fanout(TFunctions... functions)
{
    constexpr auto reason = TTraits::validate_fanout_factory(^^std::tuple<TFunctions...>, ^^TState);
    static_assert(reason.empty(), detail::sequence_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return detail::FanoutContinuation<TState, TTraits, std::tuple<TFunctions...>>{
            std::tuple<TFunctions...>{std::move(functions)...}};
    }
    else
    {
        return detail::FanoutError{};
    }
}

/**
 * Build a shared-input continuation adapter naming its result.
 *
 * @param functions StateIO-producing continuations.
 * @return Named fanout adapter.
 */
template <char const * name_cstr, class TState, class... TFunctions>
[[nodiscard]] constexpr auto fanout(TFunctions... functions)
{
    return state_io::fanout<TState, detail::FanoutStateIOTraits<name_cstr>>(
        std::move(functions)...);
}
}  // namespace fnfelt::monad::state_io
