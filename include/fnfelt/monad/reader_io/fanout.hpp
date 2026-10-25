// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/fanout.hpp
 *
 * Apply ReaderIO continuations to a shared logical input.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/reader_io/detail/fanout.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Adapt continuations to consume a shared stable input and collect their ReaderIO results.
 *
 * Calling the adapter eagerly invokes each continuation with the same input as an lvalue,
 * preferring direct invocation to tuple spreading. Continuation invocation order is unspecified.
 * The resulting ReaderIO is lazy. Branches overlap under one runtime scheduler; constant evaluation
 * runs them inline. Results retain input order.
 * Each branch receives an independent environment copy, so
 * nonempty adapters require a copyable environment. An empty adapter accepts a move-only
 * environment. Lvalue adapter calls copy stored continuations; rvalue calls move them. The shared
 * input may be move-only.
 *
 * @param functions Continuations returning ReaderIOs over TState.
 * @return Callable producing the collected ReaderIO for its shared input.
 */
template <class TState, class TTraits = detail::FanoutReaderIOTraits<>, class... TFunctions>
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
 * @param functions ReaderIO-producing continuations.
 * @return Named fanout adapter.
 */
template <char const * name_cstr, class TState, class... TFunctions>
[[nodiscard]] constexpr auto fanout(TFunctions... functions)
{
    return reader_io::fanout<TState, detail::FanoutReaderIOTraits<name_cstr>>(
        std::move(functions)...);
}
}  // namespace fnfelt::monad::reader_io
