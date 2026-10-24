// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/repeat_until.hpp
 *
 * Validation and environment binding for ReaderIO loops.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

#include <fnfelt/monad/io/detail/repeat_until.hpp>
#include <fnfelt/monad/io/repeat_until.hpp>
#include <fnfelt/monad/reader_io/detail/bind.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{
/// Placeholder for a rejected loop.
struct RepeatUntilError
{
};

/**
 * Format a rejected loop.
 *
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Diagnostic string.
 */
consteval std::string repeat_until_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg{"fnfelt: "};
    msg += name;
    msg += " repeat_until error: ";
    msg += reason;
    return msg;
}

/**
 * Bind a source to a reusable environment and delegate the accumulator loop to IO.
 *
 * @tparam TSource Source ReaderIO type.
 * @tparam TAccumulator Loop accumulator type.
 * @tparam TStepper Loop stepper type.
 */
template <class TSource, class TAccumulator, class TStepper>
struct RepeatUntilAction
{
    /// Reusable source reader.
    TSource source;
    /// First accumulator value.
    TAccumulator initial;
    /// Accumulator transition function.
    TStepper stepper;

    /**
     * Materialise a loop retaining the environment for every source invocation.
     *
     * @param self Loop action to consume.
     * @param state Environment copied for each iteration.
     * @return IO running the loop under its enclosing scheduler.
     */
    constexpr auto operator()(this auto && self, typename TSource::state_type state)
    {
        return io::repeat_until(
            bind_reader<typename TSource::state_type>(FW(self).source, state),
            FW(self).initial,
            FW(self).stepper);
    }
};

/**
 * Validate ReaderIO loops with an accumulator separate from their environment.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct RepeatUntilReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Validate the reusable source, accumulator and stepper.
     *
     * @param source_meta Source ReaderIO type.
     * @param accumulator_meta Decayed accumulator type.
     * @param stepper_meta Decayed stepper type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_repeat_until(
        std::meta::info source_meta, std::meta::info accumulator_meta, std::meta::info stepper_meta)
    {
        if (!detail::is_reader_io(source_meta))
        {
            return "source is not a ReaderIO";
        }
        auto const state_meta = detail::reader_io_state_meta(source_meta);
        if (!is_copy_constructible_type(state_meta))
        {
            return "state must be copy constructible";
        }
        if (!is_copy_constructible_type(source_meta))
        {
            return "source ReaderIO must be copy constructible";
        }
        return io::detail::RepeatUntilIOTraits<>::validate_repeat_until(
            detail::bound_reader_io_meta(state_meta, source_meta), accumulator_meta, stepper_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
