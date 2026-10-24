// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/tap.hpp
 *
 * Discard a logical result and run the next StateIO.
 */
#pragma once
#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/detail/tap.hpp>
#include <fnfelt/monad/state_io/and_then.hpp>
#include <fnfelt/monad/state_io/detail/tap.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Discard the source value and run the next computation with updated state.
 *
 * @param source Source computation.
 * @param next Next computation over the same state type.
 * @return StateIO producing the next value and final state.
 */
template <class TTraits, class TSource, class TNext>
[[nodiscard]] constexpr auto tap(TSource && source, TNext && next)
{
    using source_type = std::remove_cvref_t<TSource>;
    using next_type = std::remove_cvref_t<TNext>;
    constexpr auto reason = TTraits::validate_tap(^^source_type, ^^next_type);
    static_assert(
        reason.empty(),
        detail::create_error_msg<^^source_type, ^^next_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::and_then<TTraits>(FW(source), io::detail::NextContinuation{FW(next)});
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
 * @return Named StateIO.
 */
template <char const * name_cstr, class TSource, class TNext>
[[nodiscard]] constexpr auto tap(TSource && source, TNext && next)
{
    return state_io::tap<detail::TapStateIOTraits<name_cstr>>(FW(source), FW(next));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
