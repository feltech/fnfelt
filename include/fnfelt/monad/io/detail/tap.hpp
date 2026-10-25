// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/tap.hpp
 *
 * Value-discarding IO composition.
 */
#pragma once
#include <meta>

#include <string_view>
#include <utility>

#include <fnfelt/monad/io/detail/and_then.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io::detail
{
/// Placeholder for rejected value-discarding composition.
struct TapError
{
};

/**
 * Replace a complete value with a stored next computation.
 *
 * @tparam TNext Next IO type.
 */
template <class TNext>
struct NextContinuation
{
    /// Next computation.
    TNext next;
    /**
     * Discard the complete source value.
     *
     * @param self Stored continuation.
     * @param value Discarded value.
     * @return Next IO.
     */
    constexpr auto operator()(this auto && self, [[maybe_unused]] auto && value)
    {
        return FW(self).next;
    }
};

/**
 * Validate value-discarding composition.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr>
struct TapIOTraits : AndThenIOTraits<name_cstr>
{
    /**
     * Validate source and next computations.
     *
     * @param source_meta Source computation.
     * @param next_meta Next computation.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_tap(
        std::meta::info source_meta, std::meta::info next_meta)
    {
        if (!is_io(source_meta) || !is_io(next_meta))
        {
            return "source and next computations must be IOs";
        }
        return AndThenIOTraits<name_cstr>::validate_and_then(
            source_meta, substitute(^^NextContinuation, {next_meta}));
    }
};
}  // namespace fnfelt::monad::io::detail

#include <fnfelt/macros_pop.hpp>
