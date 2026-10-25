// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/get_state.hpp
 *
 * Read the ReaderIO environment as a value.
 */
#pragma once
#include <meta>

#include <string>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/get_state.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Produce the owned environment as the logical value.
 *
 * Move-only environments are supported. A copyable reader can be built repeatedly with
 * independent environments; each resulting IO owns the environment passed to that build.
 *
 * @return ReaderIO producing its environment.
 */
template <class TState, class TTraits = detail::GetStateReaderIOTraits<>>
[[nodiscard]] constexpr auto get_state()
{
    constexpr auto reason = TTraits::validate_get_state(^^TState);
    static_assert(
        reason.empty(),
        std::string{"fnfelt: "} + std::string{TTraits::name} +
            " get_state error: " + std::string{reason});
    if constexpr (reason.empty())
    {
        return reader_io::create<TState, TTraits>(detail::GetStateAction<TState>{});
    }
    else
    {
        return detail::GetStateError{};
    }
}

/**
 * Read the environment and name the computation.
 *
 * @return Named ReaderIO producing its environment.
 */
template <char const * name_cstr, class TState>
[[nodiscard]] constexpr auto get_state()
{
    return reader_io::get_state<TState, detail::GetStateReaderIOTraits<name_cstr>>();
}
}  // namespace fnfelt::monad::reader_io
