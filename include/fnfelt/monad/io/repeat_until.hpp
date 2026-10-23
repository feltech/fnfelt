// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/repeat_until.hpp
 *
 * Repeat-until loop operation for the IO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/repeat_until.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Repeats a source IO in a loop until a stepper reports success, producing an IO of the final
 * value.
 *
 * The returned IO, when run, loops: the source IO is re-run on EVERY iteration (its action is
 * copied out of the stored source, so the source's action must be copy constructible), the stepper
 * is invoked with the current state and the freshly produced value as exactly two arguments, and
 * the state is threaded only through the stepper's `std::expected` result: an `std::unexpected`
 * value becomes the next iteration's state, and the first `std::expected` value becomes the loop's
 * result. The loop is a run-time loop, never recursion.
 *
 * The stepper must be a complete, non-reference, move constructible class or function pointer type,
 * callable as `stepper(std::move(state), std::move(value))` (both arguments passed as rvalues, the
 * receiver as an lvalue, no spreading), and must return `std::expected<NewValue, State>` where the
 * unexpected type is exactly the state type. The state must be a complete, movable type. An invalid
 * combination (including a non-IO source or a non-re-runnable source action) is rejected by a
 * friendly static_assert and yields `detail::RepeatUntilError`.
 *
 * The source IO is taken by forwarding reference and perfect-forwarded into the result IO's action,
 * so an rvalue source moves (rvalue chains move with no extra copies) and an lvalue source is
 * copied (leaving the caller's IO untouched). The state and stepper are decayed, so lvalues are
 * copied and rvalues moved into the result.
 *
 * @tparam TTraits Traits used to validate the combination and for the result IO, defaulting to
 * detail::RepeatUntilIOTraits.
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TInitialState Initial state threaded into the stepper.
 * @tparam TStepper Stepper taking the state and value, returning an expected.
 * @param source Source IO to re-run on every iteration (a non-IO source is rejected by validation).
 * @param initial_state Initial state threaded into the stepper.
 * @param stepper Stepper applied to the state and each produced value.
 * @return IO re-running the source until the stepper reports success, or detail::RepeatUntilError
 * if the combination is invalid (which is also rejected by a friendly static_assert).
 */
template <class TTraits, class TSource, class TInitialState, class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitialState && initial_state, TStepper && stepper)
{
    using source_io_type = std::remove_cvref_t<TSource>;
    // Decay the state and stepper for by-value validation and storage: lvalues are copied, rvalues
    // are moved, and function lvalues become function pointers.
    using state_type = std::decay_t<TInitialState>;
    using stepper_type = std::decay_t<TStepper>;
    static constexpr auto reason =
        TTraits::validate_repeat_until(^^source_io_type, ^^state_type, ^^stepper_type);
    static_assert(
        reason.empty(),
        detail::repeat_until_error_msg<^^source_io_type, ^^state_type, ^^stepper_type>(
            TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::RepeatUntilAction<source_io_type, state_type, stepper_type>;
        // Qualified so a state/stepper type mentioning other monads (e.g. a co-included ReaderIO)
        // cannot make the enclosing namespace's factories reachable by ADL and bind ambiguously.
        return io::create<TTraits>(action_type{FW(source), FW(initial_state), FW(stepper)});
    }
    else
    {
        return detail::RepeatUntilError{};
    }
}

/**
 * Repeats a source IO in a loop until a stepper reports success, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the source IO, state and stepper. Overloads on the first
 * template argument: a name (`char const *` non-type parameter) or a traits type.
 *
 * The result IO takes detail::RepeatUntilIOTraits<name_cstr>, which is also used to validate the
 * source IO, state and stepper, overriding the source IO's traits.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g.
 * `"My repeat_until"_ss`).
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TInitialState Initial state threaded into the stepper.
 * @tparam TStepper Stepper taking the state and value, returning an expected.
 * @param source Source IO to re-run on every iteration (a non-IO source is rejected by validation).
 * @param initial_state Initial state threaded into the stepper.
 * @param stepper Stepper applied to the state and each produced value.
 * @return IO re-running the source until the stepper reports success with
 * detail::RepeatUntilIOTraits<name_cstr>, or detail::RepeatUntilError if the combination is invalid
 * (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TSource, class TInitialState, class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitialState && initial_state, TStepper && stepper)
{
    return io::repeat_until<detail::RepeatUntilIOTraits<name_cstr>>(
        FW(source), FW(initial_state), FW(stepper));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
