// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/tap.hpp
 *
 * Value-discarding StateIO composition.
 */
#pragma once
#include <meta>

#include <string_view>

#include <fnfelt/monad/io/detail/tap.hpp>
#include <fnfelt/monad/state_io/detail/and_then.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for rejected value-discarding composition.
struct TapError
{
};

/**
 * Validate value-discarding composition.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr>
struct TapStateIOTraits : AndThenStateIOTraits<name_cstr>
{
    /**
     * Validate source and next computations over the same state.
     *
     * @param source_meta Source computation.
     * @param next_meta Next computation.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_tap(
        std::meta::info source_meta, std::meta::info next_meta)
    {
        if (!is_state_io(source_meta) || !is_state_io(next_meta))
        {
            return "source and next computations must be StateIOs";
        }
        return AndThenStateIOTraits<name_cstr>::validate_and_then(
            source_meta, substitute(^^io::detail::NextContinuation, {next_meta}));
    }
};
}  // namespace fnfelt::monad::state_io::detail
