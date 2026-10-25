// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/tap.hpp
 *
 * Value-discarding ReaderIO composition.
 */
#pragma once
#include <meta>

#include <string_view>
#include <utility>

#include <fnfelt/monad/reader_io/detail/and_then.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/// Placeholder for rejected value-discarding composition.
struct TapError
{
};

/**
 * Replace a complete value with a stored next computation.
 *
 * @tparam TNext Next ReaderIO type.
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
     * @return Next ReaderIO.
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
struct TapReaderIOTraits : AndThenReaderIOTraits<name_cstr>
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
        if (!is_reader_io(source_meta) || !is_reader_io(next_meta))
        {
            return "source and next computations must be ReaderIOs";
        }
        return AndThenReaderIOTraits<name_cstr>::validate_and_then(
            source_meta, substitute(^^NextContinuation, {next_meta}));
    }
};
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
