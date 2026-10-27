// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/select.hpp
 *
 * Selective application for the IO monad.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/select.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Complete an expected result, running a fallback effect only for an unexpected value.
 *
 * The choice produces `std::expected<T, E>`. A value completes the computation immediately;
 * otherwise the fallback produces a function accepting `E` and returning exactly `T` by value.
 * Direct invocation takes precedence over spreading a pair or tuple error. Neither effect runs
 * during composition. Arguments are independently copied from lvalues and moved from rvalues.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TChoice Choice IO type.
 * @tparam TFallback Fallback IO type.
 * @param choice IO producing a completed result or fallback input.
 * @param fallback IO producing the function for an unexpected choice.
 * @return Selective IO, or a rejected-composition placeholder after a diagnostic.
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
        return io::create<TTraits>(
            detail::SelectAction<choice_type, fallback_type>{FW(choice), FW(fallback)});
    }
    else
    {
        return detail::SelectError{};
    }
}

/**
 * Complete an expected result with a named selective IO.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TChoice Choice IO type.
 * @tparam TFallback Fallback IO type.
 * @param choice IO producing a completed result or fallback input.
 * @param fallback IO producing the function for an unexpected choice.
 * @return Named selective IO.
 */
template <char const * name_cstr, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback)
{
    return io::select<detail::SelectIOTraits<name_cstr>>(FW(choice), FW(fallback));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
