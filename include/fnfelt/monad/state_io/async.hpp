// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/async.hpp
 *
 * Execution proxies for synchronous and asynchronous StateIO computations.
 */
#pragma once
#include <meta>

#include <functional>
#include <utility>

#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Owns a bound action and state for a single execution.
 *
 * @tparam TState Bound state type.
 * @tparam TAction Stored action type.
 * @tparam TTraits Consumption validation traits.
 */
template <class TState, class TAction, class TTraits>
struct StateRunProxy
{
    /// Stored action.
    TAction action;
    /// Bound initial state.
    TState state;

    /**
     * Consume the proxy and materialise its inner IO.
     *
     * @param self Proxy consumed as a non-const rvalue.
     * @return IO producing the logical value and updated state.
     */
    [[nodiscard]] constexpr auto io(this auto && self)
    {
        constexpr auto reason = TTraits::validate_sync_wait(^^decltype(self));
        static_assert(reason.empty(), detail::run_proxy_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            return std::invoke(FW(self).action, std::move(self.state));
        }
        else
        {
            return detail::StateRunError{};
        }
    }

    /**
     * Execute the inner IO under the supplied scheduler, when asynchronous.
     *
     * @param self Proxy consumed as a non-const rvalue.
     * @param scheduler Scheduler arguments forwarded to the inner IO.
     * @return Pair of logical value and final state.
     */
    [[nodiscard]] constexpr auto sync_wait(this auto && self, auto &&... scheduler)
    {
        constexpr auto reason = TTraits::validate_sync_wait(^^decltype(self));
        static_assert(reason.empty(), detail::run_proxy_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            return FW(self).io().build().sync_wait(FW(scheduler)...);
        }
        else
        {
            return detail::StateRunError{};
        }
    }
};
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
