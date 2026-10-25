// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/split.hpp
 *
 * Apply ReaderIO continuations to distinct tuple elements.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/reader_io/detail/split.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Adapt continuations to consume corresponding tuple values and collect their ReaderIO results.
 *
 * Calling the adapter takes ownership of a tuple with the same size as the continuation pack,
 * then invokes each continuation with its moved corresponding element, preferring direct invocation
 * to pair/tuple spreading. Continuation invocation order is unspecified.
 * The resulting ReaderIO is lazy. Branches overlap under one runtime scheduler; constant evaluation
 * runs them inline. Results retain input order.
 * Each branch receives an independent environment copy, so
 * nonempty adapters require a copyable environment. An empty adapter accepts a move-only
 * environment. Lvalue adapter calls copy stored continuations; rvalue calls move them. Input
 * elements may be move-only.
 *
 * @param functions Continuations returning ReaderIOs over TState.
 * @return Callable producing the collected ReaderIO for its input tuple.
 */
template <class TState, class TTraits = detail::SplitReaderIOTraits<>, class... TFunctions>
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
 * @param functions ReaderIO-producing continuations.
 * @return Named split adapter.
 */
template <char const * name_cstr, class TState, class... TFunctions>
[[nodiscard]] constexpr auto split(TFunctions... functions)
{
    return reader_io::split<TState, detail::SplitReaderIOTraits<name_cstr>>(
        std::move(functions)...);
}
}  // namespace fnfelt::monad::reader_io
