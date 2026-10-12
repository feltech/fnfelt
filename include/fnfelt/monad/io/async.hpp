// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file async.hpp
 *
 * Asynchronous IO: run proxy, async leaves and async/mixed `and_then`, `ap` and `transform`
 * composition.
 *
 * `IO::build()` yields a `RunProxy` holding the IO's action; calling `sync_wait` on it is the
 * only execution path. Asynchronous actions run on libfork; `create_async` wraps a stateless
 * coroutine function into an IO and `and_then`, `ap` and `transform` compose sync and async IOs
 * transparently. Values produced inside asynchronous composition must be movable (move
 * constructible and assignable), since they are returned through `lf::task`; this includes the sync
 * sides composed into an async `ap` (e.g. a capturing lambda cannot be such a value).
 */
#pragma once

#include <meta>

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
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Async leaf proxy: stores a leaf's arguments and exposes the stateless coroutine entry.
 *
 * @tparam TFn User's stateless coroutine function type.
 * @tparam TArgTuple Stored argument tuple type.
 */
template <class TFn, class TArgTuple>
struct AsyncLeafProxy : AsyncProxyTag
{
    /// Value the coroutine produces.
    using value_type = [:detail::async_leaf_value_meta(^^TFn, ^^TArgTuple):];
    /// Arguments spread into @ref fn.
    TArgTuple arg;
    /// Stateless coroutine entry (the user's async function), as libfork requires.
    // `fn` is the libfork-imposed entry-point name, exempted from the identifier-length convention.
    static constexpr TFn fn{};

    /**
     * Construct from the stored argument tuple.
     *
     * @param arg_in Arguments to store.
     */
    constexpr explicit AsyncLeafProxy(TArgTuple arg_in) : arg{std::move(arg_in)} {}
};

/**
 * Action wrapping an async leaf's arguments; invoking it materialises the proxy.
 *
 * @tparam TFn User's stateless coroutine function type.
 * @tparam TArgTuple Stored argument tuple type.
 */
template <class TFn, class TArgTuple>
struct AsyncLeafAction
{
    /// Arguments stored for the leaf.
    TArgTuple arg;

    /**
     * Construct from the stored argument tuple.
     *
     * @param arg_in Arguments to store.
     */
    constexpr explicit AsyncLeafAction(TArgTuple arg_in) : arg{std::move(arg_in)} {}

    /**
     * Materialise the async leaf proxy.
     *
     * @param self The action to invoke (explicit object parameter).
     * @return The async leaf proxy.
     */
    constexpr AsyncLeafProxy<TFn, TArgTuple> operator()(this auto && self)
    {
        return AsyncLeafProxy<TFn, TArgTuple>{FW(self).arg};
    }
};

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
 * CreateAsyncIOTraits.
 * @tparam TFn Deduced stateless coroutine function type.
 * @tparam TArgs Deduced argument types (stored by value).
 * @param fn The stateless coroutine function.
 * @param args Arguments passed to the coroutine (moved/copied per value category).
 * @return IO over the async leaf with TTraits.
 */
template <class TTraits = CreateAsyncIOTraits<>, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async([[maybe_unused]] TFn fn, TArgs &&... args)
{
    using arg_tuple_type = std::tuple<std::decay_t<TArgs>...>;
    static constexpr auto reason = TTraits::validate_create_async(^^TFn, ^^arg_tuple_type);
    static_assert(reason.empty(), detail::create_async_error_msg<^^TFn>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = AsyncLeafAction<TFn, arg_tuple_type>;
        return create<TTraits>(action_type{arg_tuple_type{FW(args)...}});
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
 * @return IO over the async leaf with CreateAsyncIOTraits<name_cstr>.
 */
template <char const * name_cstr, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    return create_async<CreateAsyncIOTraits<name_cstr>>(std::move(fn), FW(args)...);
}

/**
 * Proxy that runs an IO's action.
 *
 * Holds the IO's action by value, never a proxy; `sync_wait` invokes it. On the synchronous path
 * the invocation result is the value. On the asynchronous path the invocation result is an
 * @c AsyncProxyTag-derived proxy, whose coroutine entry then runs on a libfork scheduler.
 *
 * @tparam TAction Callable action held by value.
 */
template <class TAction>
struct RunProxy
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
        static_assert(
            !std::is_lvalue_reference_v<decltype(self)>,
            "sync_wait must be called on an rvalue run proxy: the stored callable is invoked as an "
            "rvalue. Consume the proxy, e.g. std::move(proxy).sync_wait()");
        if constexpr (detail::action_is_async(^^TAction))
        {
            // Materialise the proxy, then dispatch on the scheduler pack.
            auto proxy = FW(self).action();
            if constexpr (sizeof...(scheduler) == 0)
            {
                return detail::run_async_proxy(lf::lazy_pool{}, FW(proxy));
            }
            if constexpr (
                sizeof...(scheduler) == 1 && (detail::scheduler_arg<decltype(scheduler)> && ...))
            {
                return detail::run_async_proxy((FW(scheduler), ...), FW(proxy));
            }
            static_assert(
                sizeof...(scheduler) == 0 ||
                    (sizeof...(scheduler) == 1 &&
                     (detail::scheduler_arg<decltype(scheduler)> && ...)),
                "sync_wait accepts at most one scheduler argument");
        }
        if constexpr (!detail::action_is_async(^^TAction))
        {
            return FW(self).action();
        }
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
