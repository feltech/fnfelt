// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/async.hpp
 *
 * Run proxies and asynchronous leaves for the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Run proxy binding a ReaderIO's action to a state for a single materialisation.
 *
 * Returned by `ReaderIO::build`; its members are consumed by rvalue, so an lvalue or const-bound
 * proxy is rejected by a friendly static_assert and yields `detail::ReaderRunError`.
 *
 * `io` materialises the composition by invoking the action with the state, producing the inner IO
 * to run or compose. The action's invocation consumes the bound state and the proxy's action by
 * move, so `io` must be consumed exactly once per proxy; the proxy itself holds no machinery, so
 * a discarded `io` (for example when only validating a composition) is fine.
 *
 * `sync_wait` is the execution path: it delegates to the inner IO's run proxy, forwarding
 * scheduler arguments, so synchronous and asynchronous inner IOs run alike.
 *
 * @tparam TState The bound state type.
 * @tparam TAction The ReaderIO's action type.
 * @tparam TTraits The ReaderIO's traits.
 */
template <class TState, class TAction, class TTraits>
struct ReaderRunProxy
{
    /// The ReaderIO's stored action.
    TAction action;
    /// The state the action is invoked with.
    TState state;

    /**
     * Materialise the composition: invoke the action with the state, producing the inner IO.
     *
     * Each proxy binds the state once and consumes its members by move, so this must be consumed
     * exactly once per proxy.
     *
     * @param self The run proxy to consume (explicit object parameter).
     * @return The action's inner IO for the bound state; `detail::ReaderRunError` if the proxy is
     * not consumed as an rvalue (which is also rejected by a friendly static_assert).
     */
    [[nodiscard]] constexpr auto io(this auto && self)
    {
        // `decltype(self)` is the declared explicit-object parameter type: an rvalue invocation
        // yields `ReaderRunProxy &&` (or const-qualified if the proxy was bound const).
        constexpr auto reason = TTraits::validate_sync_wait(^^decltype(self));
        static_assert(reason.empty(), detail::run_proxy_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            return std::invoke(FW(self).action, std::move(FW(self).state));
        }
        else
        {
            return detail::ReaderRunError{};
        }
    }

    /**
     * Materialise the composition and run the inner IO, forwarding scheduler arguments.
     *
     * Delegates to the inner IO's run proxy, so the run validates the inner IO's scheduler
     * arguments (e.g. a libfork scheduler for asynchronous actions).
     *
     * @param self The run proxy to consume (explicit object parameter).
     * @param scheduler Scheduler arguments forwarded to the inner IO's `sync_wait`.
     * @return The inner IO's value for the bound state; `detail::ReaderRunError` if the proxy is
     * not consumed as an rvalue (which is also rejected by a friendly static_assert).
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
            return detail::ReaderRunError{};
        }
    }
};

}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
