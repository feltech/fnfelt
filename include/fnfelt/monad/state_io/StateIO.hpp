// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/StateIO.hpp
 *
 * StateIO monad transformer and its state-binding interface.
 */
#pragma once
#include <meta>

#include <utility>

#include <fnfelt/monad/state_io/async.hpp>
#include <fnfelt/monad/state_io/detail.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Wraps a state-consuming action returning an IO of a value and updated state.
 *
 * Actions return an IO producing `std::pair<value, state>` by value. Compositions pass the updated
 * state forward; the logical value alone reaches continuations and transformers. Factories validate
 * actions, while direct construction trusts its arguments. Bind a state with `build` and consume
 * the resulting proxy once with `io` or `sync_wait`.
 *
 * @tparam TState Owned state threaded through the computation.
 * @tparam TAction Stored state-consuming action.
 * @tparam TTraits Composition and consumption validation traits.
 */
template <class TState, class TAction, class TTraits>
class StateIO
{
public:
    /// State threaded through this computation.
    using state_type = TState;
    /// Stored action type.
    using action_type = TAction;
    /// Validation traits.
    using traits = TTraits;
    /// Logical produced value, excluding the updated state.
    using value_type = [:detail::action_value_meta(^^TAction, ^^TState):];

    /**
     * Store an action, bypassing factory validation.
     *
     * @param action Action copied or moved into storage.
     */
    template <class TActionArg>
    requires(!detail::is_state_io(^^TActionArg)) explicit constexpr StateIO(TActionArg && action)
        : action_{FW(action)}
    {
    }

    /**
     * Bind owned state for a single execution.
     *
     * Lvalue computations copy their action; rvalues move it. The supplied state is copied or
     * moved into the proxy according to its argument category.
     *
     * @tparam TBuildTraits Action-transfer validation traits.
     * @param self Computation to bind.
     * @param state Initial state.
     * @return Single-use run proxy.
     */
    template <class TBuildTraits = TTraits>
    [[nodiscard]] constexpr auto build(this auto && self, TState state)
    {
        constexpr auto reason = TBuildTraits::validate_build(^^decltype((FW(self).action_)));
        static_assert(reason.empty(), io::detail::build_error_msg(TBuildTraits::name, reason));
        if constexpr (reason.empty())
        {
            return StateRunProxy<TState, TAction, TTraits>{FW(self).action_, std::move(state)};
        }
        else
        {
            return detail::StateBuildError{};
        }
    }

    /**
     * Chain a continuation receiving the logical value and returning a StateIO.
     *
     * Updated state is moved into the returned computation. Pair and tuple values are spread
     * only when direct invocation is unavailable.
     *
     * @param self Source computation.
     * @param continuation Continuation returning a StateIO over the same state type.
     * @return Composed StateIO.
     */
    template <class TAndThenTraits = detail::AndThenStateIOTraits<>, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return state_io::and_then<TAndThenTraits>(FW(self), std::move(continuation));
    }

    /**
     * Chain a continuation and name the result for diagnostics.
     *
     * @param self Source computation.
     * @param continuation Continuation returning a StateIO.
     * @return Named composition.
     */
    template <char const * name_cstr, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return state_io::and_then<name_cstr>(FW(self), std::move(continuation));
    }

    /**
     * Transform the logical value while preserving updated state.
     *
     * @param self Source computation.
     * @param transformer Plain-value transformer, directly invoked or tuple-spread.
     * @return Transformed StateIO.
     */
    template <class TTransformTraits = detail::TransformStateIOTraits<>, class TTransformer>
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    [[nodiscard]] constexpr auto transform(this auto && self, TTransformer transformer)
    {
        return state_io::transform<TTransformTraits>(FW(self), std::move(transformer));
    }

    /**
     * Transform the logical value and name the result.
     *
     * @param self Source computation.
     * @param transformer Plain-value transformer.
     * @return Named transformed StateIO.
     */
    template <char const * name_cstr, class TTransformer>
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    [[nodiscard]] constexpr auto transform(this auto && self, TTransformer transformer)
    {
        return state_io::transform<name_cstr>(FW(self), std::move(transformer));
    }

    /**
     * Complete an expected result with a conditional fallback computation.
     *
     * @param self Choice computation.
     * @param fallback Computation producing a function for an unexpected choice.
     * @return Selective StateIO.
     */
    template <class TSelectTraits = detail::SelectStateIOTraits<>, class TFallback>
    [[nodiscard]] constexpr auto select(this auto && self, TFallback && fallback)
    {
        return state_io::select<TSelectTraits>(FW(self), FW(fallback));
    }

    /**
     * Complete an expected result and name the selective computation.
     *
     * @param self Choice computation.
     * @param fallback Computation producing a function for an unexpected choice.
     * @return Named selective StateIO.
     */
    template <char const * name_cstr, class TFallback>
    [[nodiscard]] constexpr auto select(this auto && self, TFallback && fallback)
    {
        return state_io::select<name_cstr>(FW(self), FW(fallback));
    }

    /**
     * Repeat with an independent accumulator while threading updated state.
     *
     * @param self Reusable source computation.
     * @param initial Initial accumulator.
     * @param stepper Transition returning expected<value, accumulator>.
     * @return Loop StateIO.
     */
    template <
        class TRepeatTraits = detail::RepeatUntilStateIOTraits<>,
        class TInitial,
        class TStepper>
    [[nodiscard]] constexpr auto repeat_until(
        this auto && self, TInitial && initial, TStepper && stepper)
    {
        return state_io::repeat_until<TRepeatTraits>(FW(self), FW(initial), FW(stepper));
    }

    /**
     * Repeat and name the result.
     *
     * @param self Reusable source computation.
     * @param initial Initial accumulator.
     * @param stepper Expected-producing transition.
     * @return Named loop StateIO.
     */
    template <char const * name_cstr, class TInitial, class TStepper>
    [[nodiscard]] constexpr auto repeat_until(
        this auto && self, TInitial && initial, TStepper && stepper)
    {
        return state_io::repeat_until<name_cstr>(FW(self), FW(initial), FW(stepper));
    }

private:
    action_type action_;
};
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
