// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/or_else.hpp
 *
 * Conditional error recovery for ReaderIO.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/or_else.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Recover a Result error using the source reader's environment.
 *
 * Success and stop bypass recovery. The error payload is passed directly or spread to a
 * continuation returning a ReaderIO over the same environment and successful payload type.
 * Recovery may change the error type. Infallible sources do not inspect the handler's signature.
 * Source and recovery receive independent copies of the environment, as with and_then.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source ReaderIO expression type.
 * @tparam TContinuation Recovery callable expression type.
 * @param source Deferred Result source.
 * @param continuation Error-taking continuation returning a ReaderIO.
 * @return Recovered reader, or a diagnostic placeholder for invalid composition.
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
        using state_type = [:detail::reader_io_state_meta(^^source_type):];
        return reader_io::create<state_type, TTraits>(
            detail::OrElseAction<state_type, source_type, continuation_type>{
                FW(source), FW(continuation)});
    }
    else
    {
        return detail::OrElseError{};
    }
}

/**
 * Recover an error, naming the resulting ReaderIO.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TSource Source ReaderIO expression type.
 * @tparam TContinuation Recovery callable expression type.
 * @param source Deferred Result source.
 * @param continuation Error-taking continuation returning a ReaderIO.
 * @return Named recovered reader.
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto or_else(TSource && source, TContinuation && continuation)
{
    return reader_io::or_else<detail::OrElseReaderIOTraits<name_cstr>>(
        FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
