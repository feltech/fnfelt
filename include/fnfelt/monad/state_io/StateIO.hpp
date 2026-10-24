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

private:
    action_type action_;
};
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
