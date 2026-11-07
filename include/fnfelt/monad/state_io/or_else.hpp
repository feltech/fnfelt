// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/or_else.hpp
 *
 * Conditional error recovery for StateIO.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/or_else.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Recover a Result error using the source state computation's environment.
 *
 * Success and stop bypass recovery. The error payload is passed directly or spread to a
 * continuation returning a StateIO over the same environment and successful payload type.
 * Recovery may change the error type. Infallible sources do not inspect the handler's signature.
 * Recovery receives the state returned by the failing computation.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source StateIO expression type.
 * @tparam TContinuation Recovery callable expression type.
 * @param source Deferred Result source.
 * @param continuation Error-taking continuation returning a StateIO.
 * @return Recovered state computation, or a diagnostic placeholder for invalid composition.
 */
template <class TTraits, class TSource, class TContinuation>
[[nodiscard]] constexpr auto or_else(TSource && source, TContinuation && continuation)
{
    using source_type = std::remove_cvref_t<TSource>;
    using continuation_type = std::decay_t<TContinuation>;
    static constexpr auto reason = TTraits::validate_or_else(^^source_type, ^^continuation_type);
    static_assert(
        reason.empty(),
        detail::or_else_error_msg<^^source_type, ^^continuation_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::state_io_state_meta(^^source_type):];
        return state_io::create<state_type, TTraits>(
            detail::OrElseAction<state_type, source_type, continuation_type>{
                FW(source), FW(continuation)});
    }
    else
    {
        return detail::OrElseError{};
    }
}

/**
 * Recover an error, naming the resulting StateIO.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TSource Source StateIO expression type.
 * @tparam TContinuation Recovery callable expression type.
 * @param source Deferred Result source.
 * @param continuation Error-taking continuation returning a StateIO.
 * @return Named recovered state computation.
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto or_else(TSource && source, TContinuation && continuation)
{
    return state_io::or_else<detail::OrElseStateIOTraits<name_cstr>>(FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
