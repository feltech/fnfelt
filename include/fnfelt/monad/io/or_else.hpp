// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/or_else.hpp
 *
 * Conditional error recovery for the IO monad.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/or_else.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Recover a Result's reported errors.
 *
 * The source must produce a Result. The continuation handles its opaque error directly or spread.
 * Standard optional and expected values inside its payload or error remain data. Successful and
 * stopped computations bypass recovery. A nullary callback cannot recover stopping.
 *
 * Recovery returns an IO with the same successful payload type as the source and retains Result
 * mode. Handling an error permits replacing its error domain. An Infallible source bypasses
 * recovery without inspecting callback invocation signatures.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source IO expression type.
 * @tparam TContinuation Recovery continuation expression type.
 * @param source Deferred source.
 * @param continuation Error-taking continuation returning an IO.
 * @return Deferred recovered IO, or a diagnostic placeholder for invalid composition.
 */
template <class TTraits, class TSource, class TContinuation>
[[nodiscard]] constexpr auto or_else(TSource && source, TContinuation && continuation)
{
    using source_type = std::remove_cvref_t<TSource>;
    using continuation_type = std::decay_t<TContinuation>;
    static constexpr auto reason = TTraits::validate_or_else(^^source_type, ^^continuation_type);
    static_assert(reason.empty(), detail::or_else_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return io::create<TTraits>(
            detail::OrElseAction<source_type, continuation_type>{FW(source), FW(continuation)});
    }
    else
    {
        return detail::OrElseError{};
    }
}

/**
 * Recover an error, naming the returned IO.
 *
 * @tparam name_cstr Diagnostic name with static storage.
 * @tparam TSource Source IO expression type.
 * @tparam TContinuation Recovery continuation expression type.
 * @param source Deferred source.
 * @param continuation Error-taking continuation returning an IO.
 * @return Named deferred recovered IO.
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto or_else(TSource && source, TContinuation && continuation)
{
    return io::or_else<detail::OrElseIOTraits<name_cstr>>(FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
