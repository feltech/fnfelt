// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/repeat_until.hpp
 *
 * Repeat StateIO actions with an independent loop accumulator.
 */
#pragma once

#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/repeat_until.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Rerun a stateful computation until its stepper returns a successful expected value.
 *
 * Each iteration threads the previous updated state and invokes the source action afresh. The
 * source computation must be copyable; state, inner IOs and values may be move-only.
 * The stepper runs as an lvalue with exactly two rvalue arguments: the accumulator and source
 * value. Its std::unexpected result supplies the next accumulator. Threaded state is independent
 * of that accumulator. Synchronous loops support constant evaluation, and asynchronous loops use
 * the enclosing IO scheduler without recursion.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source StateIO type.
 * @tparam TInitial Initial accumulator type.
 * @tparam TStepper Stepper returning std::expected<Value, Accumulator>.
 * @param source StateIO to rerun.
 * @param initial Initial accumulator.
 * @param stepper Accumulator transition function.
 * @return StateIO producing the first successful value.
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
        return state_io::create<typename source_type::state_type, TTraits>(
            detail::RepeatUntilAction{FW(source), FW(initial), FW(stepper)});
    }
    else
    {
        return detail::RepeatUntilError{};
    }
}

/**
 * Repeat a stateful computation, naming the result.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TSource Source StateIO type.
 * @tparam TInitial Initial accumulator type.
 * @tparam TStepper Stepper type.
 * @param source StateIO to rerun.
 * @param initial Initial accumulator.
 * @param stepper Accumulator transition function.
 * @return Named StateIO producing the successful value.
 */
template <char const * name_cstr, class TSource, class TInitial, class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitial && initial, TStepper && stepper)
{
    return state_io::repeat_until<detail::RepeatUntilStateIOTraits<name_cstr>>(
        FW(source), FW(initial), FW(stepper));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
