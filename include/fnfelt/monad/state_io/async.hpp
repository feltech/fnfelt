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
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/async.hpp>
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
/**
 * Create a StateIO from a state-first asynchronous leaf.
 *
 * The stateless coroutine receives libfork's self argument, owned state and stored arguments.
 * It returns a task producing a pair of logical value and updated state. Execution uses the
 * enclosing scheduler.
 *
 * @param fn Stateless coroutine function.
 * @param args Stored arguments following the state.
 * @return StateIO wrapping the asynchronous leaf.
 */
template <
    class TState,
    class TTraits = detail::CreateAsyncStateIOTraits<>,
    class TFn,
    class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    using fn_type = std::decay_t<TFn>;
    using arg_tuple_type = std::tuple<TState, std::decay_t<TArgs>...>;
    static constexpr auto reason = TTraits::validate_create_async(^^fn_type, ^^arg_tuple_type);
    static_assert(reason.empty(), detail::create_async_error_msg<^^fn_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::create<TState, TTraits>(
            detail::CreateAsyncAction<TState, fn_type, std::decay_t<TArgs>...>{
                std::move(fn), std::tuple<std::decay_t<TArgs>...>{FW(args)...}});
    }
    else
    {
        return detail::CreateAsyncError{};
    }
}

/**
 * Create and name a state-first asynchronous leaf.
 *
 * @param fn Stateless coroutine function.
 * @param args Stored arguments following the state.
 * @return Named asynchronous StateIO.
 */
template <char const * name_cstr, class TState, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    return state_io::create_async<TState, detail::CreateAsyncStateIOTraits<name_cstr>>(
        std::move(fn), FW(args)...);
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
