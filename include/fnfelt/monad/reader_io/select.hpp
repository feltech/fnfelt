// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/select.hpp
 *
 * Selective application for the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/select.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Complete an expected result with a conditional reader effect.
 *
 * The choice produces `std::expected<T, E>`; an unexpected input runs the fallback to obtain a
 * function returning exactly `T`. Pair and tuple errors are spread only if direct invocation is
 * unavailable. Both effects receive the original environment, which must be copy constructible.
 * The fallback reader is materialised and its IO executed only for an unexpected choice.
 * Arguments are independently copied from lvalues and moved from rvalues.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TChoice Choice ReaderIO type.
 * @tparam TFallback Fallback ReaderIO type.
 * @param choice Reader producing a completed result or fallback input.
 * @param fallback Reader producing the function for an unexpected choice.
 * @return Selective ReaderIO, or a rejected-composition placeholder after a diagnostic.
 */
template <class TTraits, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback)
{
    using choice_type = std::remove_cvref_t<TChoice>;
    using fallback_type = std::remove_cvref_t<TFallback>;
    static constexpr auto reason = TTraits::validate_select(^^choice_type, ^^fallback_type);
    static_assert(
        reason.empty(),
        detail::select_error_msg<^^choice_type, ^^fallback_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::reader_io_state_meta(^^choice_type):];
        return reader_io::create<state_type, TTraits>(
            detail::SelectAction<state_type, choice_type, fallback_type>{FW(choice), FW(fallback)});
    }
    else
    {
        return detail::SelectError{};
    }
}

/**
 * Complete an expected result with a named conditional reader effect.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TChoice Choice ReaderIO type.
 * @tparam TFallback Fallback ReaderIO type.
 * @param choice Reader producing a completed result or fallback input.
 * @param fallback Reader producing the function for an unexpected choice.
 * @return Named selective ReaderIO.
 */
template <char const * name_cstr, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback)
{
    return reader_io::select<detail::SelectReaderIOTraits<name_cstr>>(FW(choice), FW(fallback));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
