// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/get_state.hpp
 *
 * Read a copy of the current state as a logical value.
 */
#pragma once
#include <meta>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/get_state.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Inspect the current state without replacing it.
 *
 * Requires copyable state and makes one copy for the logical value during execution.
 *
 * @return StateIO producing the state copy and unchanged state.
 */
template <class TState, class TTraits = detail::GetStateStateIOTraits<>>
[[nodiscard]] constexpr auto get_state()
{
    constexpr auto reason = TTraits::validate_get_state(^^TState);
    static_assert(
        reason.empty(),
        detail::create_error_msg<^^detail::GetStateAction<TState>, ^^TState>(
            TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::create<TState, TTraits>(detail::GetStateAction<TState>{});
    }
    else
    {
        return detail::GetStateError{};
    }
}

/**
 * Inspect current state and name the computation.
 *
 * @return Named StateIO producing a state copy and unchanged state.
 */
template <char const * name_cstr, class TState>
[[nodiscard]] constexpr auto get_state()
{
    return state_io::get_state<TState, detail::GetStateStateIOTraits<name_cstr>>();
}
}  // namespace fnfelt::monad::state_io
