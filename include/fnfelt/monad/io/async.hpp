// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file async.hpp
 *
 * Asynchronous execution support for the IO monad.
 *
 * Coroutine leaves return values satisfying libfork's movable result contract. Compositions also
 * support move-constructible values that cannot be assigned, including capturing callables.
 */
#pragma once

#include <meta>

#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <libfork/core/control_flow.hpp>
#include <libfork/core/impl/promise.hpp>
#include <libfork/core/just.hpp>
#include <libfork/core/sync_wait.hpp>
#include <libfork/core/task.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Proxy that runs an IO's action.
 *
 * Holds the IO's action by value, never a proxy; `sync_wait` invokes it. On the synchronous path
 * the invocation result is the value. On the asynchronous path the invocation result is an
 * @c AsyncProxyTag-derived proxy, whose coroutine entry then runs on a libfork scheduler.
 *
 * @tparam TAction Callable action held by value.
 * @tparam TTraits Traits used to validate execution, retained from the IO.
 */
template <class TAction, class TTraits>
struct IORunProxy
{
    /// The IO's action, held by value; `sync_wait` invokes it (see the class comment).
    TAction action;

    /**
     * Run the held action and return its value.
     *
     * The proxy must be consumed as an rvalue: the held action is invoked as an rvalue, matching
     * validation and result inference. Scheduler arguments are accepted uniformly at every call
     * site and ignored on the synchronous path. An asynchronous action is run on the given
     * scheduler, or on a default-constructed libfork lazy pool when none is given; at most one
     * scheduler argument is accepted.
     *
     * @param self The proxy to run (explicit object parameter).
     * @param scheduler Optional libfork scheduler servicing the entire async tree.
     * @return The value the action produces.
     */
    [[nodiscard]] constexpr auto sync_wait(this auto && self, [[maybe_unused]] auto &&... scheduler)
    {
        static constexpr auto reason =
            TTraits::validate_sync_wait(^^decltype(self), ^^TAction, {^^decltype(scheduler)...});
        static_assert(
            reason.empty(), detail::sync_wait_error_msg<^^TAction>(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            if constexpr (detail::action_is_async(^^TAction))
            {
                // Materialise the proxy, then dispatch on the scheduler pack.
                auto proxy = FW(self).action();
                if consteval
                {
                    using proxy_type = std::remove_cvref_t<decltype(proxy)>;
                    if constexpr (requires { proxy_type::run_inline; })
                    {
                        return std::apply(proxy_type::run_inline, std::move(proxy).arg);
                    }
                    else
                    {
                        throw std::logic_error{
                            "asynchronous leaves cannot run during constant evaluation"};
                    }
                }
                if constexpr (sizeof...(scheduler) == 0)
                {
                    return detail::run_async_proxy(lf::lazy_pool{}, FW(proxy));
                }
                else if constexpr (
                    sizeof...(scheduler) == 1 &&
                    (detail::scheduler_arg<decltype(scheduler)> && ...))
                {
                    return detail::run_async_proxy((FW(scheduler), ...), FW(proxy));
                }
            }
            else
            {
                return FW(self).action();
            }
        }
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
