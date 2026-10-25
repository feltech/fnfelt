// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/fanout.hpp
 *
 * Apply IO continuations to a shared logical input.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/detail/fanout.hpp>

namespace fnfelt::monad::io
{
/**
 * Adapt continuations to consume a shared stable input and collect their IO results.
 *
 * Calling the adapter eagerly invokes each continuation with the same input as an lvalue,
 * preferring direct invocation to tuple spreading. Continuation invocation order is unspecified.
 * The resulting IO is lazy. Branches overlap under one runtime scheduler; constant evaluation
 * runs them inline. Results retain input order.
 * An empty adapter produces an empty tuple.
 * Lvalue adapter calls copy stored continuations; rvalue calls move them. The shared input may be
 * move-only.
 *
 * @param functions Continuations returning IOs.
 * @return Callable producing the collected IO for its shared input.
 */
template <class TTraits = detail::FanoutIOTraits<>, class... TFunctions>
[[nodiscard]] constexpr auto fanout(TFunctions... functions)
{
    constexpr auto reason = TTraits::validate_fanout_factory(^^std::tuple<TFunctions...>);
    static_assert(
        reason.empty(),
        detail::sequence_error_msg<^^std::tuple<TFunctions...>>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return detail::FanoutContinuation<TTraits, std::tuple<TFunctions...>>{
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
 * @param functions IO-producing continuations.
 * @return Named fanout adapter.
 */
template <char const * name_cstr, class... TFunctions>
[[nodiscard]] constexpr auto fanout(TFunctions... functions)
{
    return io::fanout<detail::FanoutIOTraits<name_cstr>>(std::move(functions)...);
}
}  // namespace fnfelt::monad::io
