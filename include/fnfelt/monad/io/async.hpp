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

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Creates an IO wrapping a stateless asynchronous coroutine function.
 *
 * The function is invoked as `fn(self, args...)`, where `self` is a libfork-generated first
 * argument, and must return a non-void `lf::task`. The function must be stateless: libfork stores
 * only a copy of the function object at run time, so a stateful function would dangle. The
 * argument types are decayed, stored by value in the async proxy, and spread into `fn` when the
 * proxy's coroutine entry runs.
 *
 * Plain side effects stay synchronous and still compose concurrently under `ap`; `create_async` is
 * for leaves that must genuinely suspend into the pool (a forked blocking synchronous side still
 * pins a worker).
 *
 * @tparam TTraits Traits used to validate the action and for the result IO, defaulting to
 * detail::CreateAsyncIOTraits.
 * @tparam TFn Deduced stateless coroutine function type.
 * @tparam TArgs Deduced argument types (stored by value).
 * @param fn The stateless coroutine function.
 * @param args Arguments passed to the coroutine (moved/copied per value category).
 * @return IO over the async leaf with TTraits.
 */
template <class TTraits = detail::CreateAsyncIOTraits<>, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async([[maybe_unused]] TFn fn, TArgs &&... args)
{
    using arg_tuple_type = std::tuple<std::decay_t<TArgs>...>;
    static constexpr auto reason = TTraits::validate_create_async(^^TFn, ^^arg_tuple_type);
    static_assert(reason.empty(), detail::create_async_error_msg<^^TFn>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = AsyncLeafAction<TFn, arg_tuple_type>;
        // Qualified so a stored argument type mentioning other monads (e.g. a co-included
        // ReaderIO) cannot make the enclosing namespace's factories reachable by ADL and bind
        // ambiguously.
        return io::create<TTraits>(action_type{arg_tuple_type{FW(args)...}});
    }
}

/**
 * Creates an IO wrapping a stateless asynchronous coroutine function, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the function and arguments. Overloads on the first template
 * argument: a name (`char const *` non-type parameter) or a traits type.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My IO"_ss`).
 * @tparam TFn Deduced stateless coroutine function type.
 * @tparam TArgs Deduced argument types (stored by value).
 * @param fn The stateless coroutine function.
 * @param args Arguments passed to the coroutine (moved/copied per value category).
 * @return IO over the async leaf with detail::CreateAsyncIOTraits<name_cstr>.
 */
template <char const * name_cstr, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    return io::create_async<detail::CreateAsyncIOTraits<name_cstr>>(std::move(fn), FW(args)...);
}

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
