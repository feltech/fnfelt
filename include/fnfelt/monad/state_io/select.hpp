// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/select.hpp
 *
 * Selective application with state threading.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/select.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Complete an expected result, running a fallback only for an unexpected value.
 *
 * The choice produces `std::expected<T, E>`. A value completes the computation with the choice's
 * updated state. Otherwise the fallback receives that state and produces a function accepting
 * `E` and returning exactly `T` by value. Its updated state becomes the final state. Direct
 * invocation takes precedence over spreading a pair or tuple error.
 *
 * @param choice StateIO producing a completed result or fallback input.
 * @param fallback StateIO producing the function for an unexpected choice.
 * @return Selective StateIO producing the completed result and final state.
 */
template <class TTraits, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback)
{
    using choice_type = std::remove_cvref_t<TChoice>;
    using fallback_type = std::remove_cvref_t<TFallback>;
    static constexpr auto reason = TTraits::validate_select(^^choice_type, ^^fallback_type);
    static_assert(
        reason.empty(),
        detail::create_error_msg<^^choice_type, ^^fallback_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::state_io_state_meta(^^choice_type):];
        return state_io::create<state_type, TTraits>(
            detail::SelectAction<state_type, choice_type, fallback_type>{FW(choice), FW(fallback)});
    }
    else
    {
        return detail::SelectError{};
    }
}

/**
 * Complete an expected result with a named selective StateIO.
 *
 * @param choice StateIO producing a completed result or fallback input.
 * @param fallback StateIO producing the function for an unexpected choice.
 * @return Named selective StateIO.
 */
template <char const * name_cstr, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback)
{
    return state_io::select<detail::SelectStateIOTraits<name_cstr>>(FW(choice), FW(fallback));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
