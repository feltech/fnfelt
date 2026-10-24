// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/repeat_until.hpp
 *
 * Repeat ReaderIO actions with an independent loop accumulator.
 */
#pragma once

#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/repeat_until.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Rerun a reader until its stepper returns a successful expected value.
 *
 * Each iteration copies the same environment and invokes the source action afresh. The source
 * reader and environment must be copyable; freshly produced inner IOs and values may be move-only.
 * The stepper runs as an lvalue with exactly two rvalue arguments: the accumulator and source
 * value. Its std::unexpected result supplies the next accumulator. The environment is independent
 * of that accumulator. Synchronous loops support constant evaluation, and asynchronous loops use
 * the enclosing IO scheduler without recursion.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source ReaderIO type.
 * @tparam TInitial Initial accumulator type.
 * @tparam TStepper Stepper returning std::expected<Value, Accumulator>.
 * @param source Reader to rerun.
 * @param initial Initial accumulator.
 * @param stepper Accumulator transition function.
 * @return ReaderIO producing the first successful value.
 */
template <class TTraits, class TSource, class TInitial, class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitial && initial, TStepper && stepper)
{
    using source_type = std::remove_cvref_t<TSource>;
    using accumulator_type = std::decay_t<TInitial>;
    using stepper_type = std::decay_t<TStepper>;
    constexpr auto reason =
        TTraits::validate_repeat_until(^^source_type, ^^accumulator_type, ^^stepper_type);
    static_assert(reason.empty(), detail::repeat_until_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return reader_io::create<typename source_type::state_type, TTraits>(
            detail::RepeatUntilAction<source_type, accumulator_type, stepper_type>{
                FW(source), FW(initial), FW(stepper)});
    }
    else
    {
        return detail::RepeatUntilError{};
    }
}

/**
 * Repeat a reader, naming the result.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TSource Source ReaderIO type.
 * @tparam TInitial Initial accumulator type.
 * @tparam TStepper Stepper type.
 * @param source Reader to rerun.
 * @param initial Initial accumulator.
 * @param stepper Accumulator transition function.
 * @return Named ReaderIO producing the successful value.
 */
template <char const * name_cstr, class TSource, class TInitial, class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitial && initial, TStepper && stepper)
{
    return reader_io::repeat_until<detail::RepeatUntilReaderIOTraits<name_cstr>>(
        FW(source), FW(initial), FW(stepper));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
