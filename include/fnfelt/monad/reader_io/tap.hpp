// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/tap.hpp
 *
 * Discard a result and run the next ReaderIO.
 */
#pragma once
#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/reader_io/and_then.hpp>
#include <fnfelt/monad/reader_io/detail/tap.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Discard the complete source value and run the next computation.
 *
 * Both computations receive independent copies of the environment, which must be copyable.
 * Execution is lazy and ordered, including when either computation is asynchronous.
 * Tuple and pair source values are discarded whole. The result is the next ReaderIO's value.
 *
 * @param source Source computation.
 * @param next Next computation.
 * @return ReaderIO producing the next value.
 */
template <class TTraits, class TSource, class TNext>
[[nodiscard]] constexpr auto tap(TSource && source, TNext && next)
{
    using source_type = std::remove_cvref_t<TSource>;
    using next_type = std::remove_cvref_t<TNext>;
    constexpr auto reason = TTraits::validate_tap(^^source_type, ^^next_type);
    static_assert(
        reason.empty(),
        detail::and_then_error_msg<^^source_type, ^^next_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return reader_io::and_then<TTraits>(FW(source), detail::NextContinuation{FW(next)});
    }
    else
    {
        return detail::TapError{};
    }
}

/**
 * Discard a value, run the next computation and name the result.
 *
 * @param source Source computation.
 * @param next Next computation.
 * @return Named ReaderIO.
 */
template <char const * name_cstr, class TSource, class TNext>
[[nodiscard]] constexpr auto tap(TSource && source, TNext && next)
{
    return reader_io::tap<detail::TapReaderIOTraits<name_cstr>>(FW(source), FW(next));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
