// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file async.hpp
 *
 * Asynchronous IO: run proxy, async leaves and async/mixed `and_then` composition.
 *
 * `IO::operator()` yields a `RunProxy` holding the IO's action; calling `sync_wait` on it is the
 * only execution path. Asynchronous actions run on libfork; `create_async` wraps a stateless
 * coroutine function into an IO and `and_then` composes sync and async IOs transparently.
 */
#pragma once

#include <meta>

#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <libfork/core/impl/promise.hpp>
#include <libfork/core/just.hpp>
#include <libfork/core/sync_wait.hpp>
#include <libfork/core/task.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/detail/errors.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Build the invocation argument metas of an async function: a placeholder first argument followed
 * by the stored argument tuple's element types.
 *
 * @param arg_tuple_meta Reflection of the stored argument tuple type.
 * @return Reflection metas of the invocation arguments.
 */
consteval std::vector<std::meta::info> async_leaf_arg_metas(std::meta::info arg_tuple_meta)
{
    std::vector<std::meta::info> args{^^int};
    for (std::meta::info const arg : template_arguments_of(dealias(arg_tuple_meta)))
    {
        args.push_back(arg);
    }
    return args;
}

/**
 * Reflection of the value an async leaf's coroutine produces.
 *
 * The async function's first parameter is a libfork-generated argument, so a placeholder type
 * stands in for it; the return type must not depend on it.
 *
 * @param fn_meta Reflection of the async function type.
 * @param arg_tuple_meta Reflection of the stored argument tuple type.
 * @return Reflection of the task's value, or `void` if the function does not produce an `lf::task`.
 */
consteval std::meta::info async_leaf_value_meta(
    std::meta::info fn_meta, std::meta::info arg_tuple_meta)
{
    std::vector<std::meta::info> const args = async_leaf_arg_metas(arg_tuple_meta);
    fn_meta = dealias(fn_meta);
    // Invocability queries are a hard error for incomplete types.
    if (!is_complete_type(fn_meta) || !is_invocable_type(fn_meta, args))
    {
        return ^^void;
    }
    std::meta::info const result = dealias(invoke_result(fn_meta, args));
    // `template_of` comparison is the same idiom as `is_io`; `lf::task` is an inline-namespace
    // name.
    if (!has_template_arguments(result) || template_of(result) != ^^lf::task)
    {
        return ^^void;
    }
    return template_arguments_of(result)[0];
}

/**
 * Reflection of the proxy type an async action's invocation produces.
 *
 * @tparam TAction Action whose invocation materialises the proxy.
 * @return Reflection of the proxy type.
 */
template <class TAction>
consteval std::meta::info async_proxy_meta()
{
    return remove_cvref(invoke_result(^^TAction, {}));
}

/**
 * Reason why a function cannot be used as an async leaf, or empty if valid.
 *
 * The function must be a default-constructible stateless class (stateful async functions dangle in
 * libfork), invocable with the given arguments, and produce a non-void `lf::task`.
 *
 * @param fn_meta Reflection of the async function type.
 * @param arg_tuple_meta Reflection of the stored argument tuple type.
 * @return Empty string if valid, otherwise a human-readable reason.
 */
consteval std::string_view async_leaf_reason(
    std::meta::info fn_meta, std::meta::info arg_tuple_meta)
{
    std::meta::info const target = dealias(remove_cvref(fn_meta));
    // The proxy stores the function as a `static constexpr TFn`, which requires a class type.
    if (!is_complete_type(target) || !is_class_type(target))
    {
        return "provided async function must be a class type";
    }
    if (!is_empty_object(target))
    {
        return "provided async function must be stateless";
    }
    if (!is_default_constructible_type(target))
    {
        return "provided async function must be default-constructible";
    }
    std::vector<std::meta::info> const args = async_leaf_arg_metas(arg_tuple_meta);
    if (!is_invocable_type(target, args))
    {
        return "provided async function is not invocable with the given arguments";
    }
    std::meta::info const result = dealias(invoke_result(target, args));
    if (!has_template_arguments(result) || template_of(result) != ^^lf::task)
    {
        return "provided async function must return an lf::task";
    }
    if (is_void_type(template_arguments_of(result)[0]))
    {
        return "provided async function must not return lf::task<void>";
    }
    return {};
}

/**
 * Whether a type satisfies libfork's scheduler concept.
 */
template <class T>
concept scheduler_arg = lf::scheduler<std::remove_cvref_t<T>>;

/**
 * Run an async proxy on a scheduler, spreading its stored arguments into its coroutine entry.
 *
 * @param sched Scheduler to run on.
 * @param proxy Async proxy holding the arguments to spread.
 * @return The proxy's plain value.
 */
template <class TScheduler, class TProxy>
auto run_async_proxy(TScheduler && sched, TProxy && proxy)
{
    return std::apply(
        [&sched](auto &&... args)
        { return lf::sync_wait(FW(sched), std::remove_cvref_t<TProxy>::fn, FW(args)...); },
        FW(proxy).arg);
}

/**
 * Run a child IO to completion inside a libfork coroutine.
 *
 * Async children are scheduled as libfork children via `lf::just`; sync children run inline with no
 * libfork machinery. This is the only child-spawning path, so a single top-level
 * `RunProxy::sync_wait` scheduler services the whole tree; children never call `lf::sync_wait`
 * recursively (which would throw `schedule_in_worker` on a worker thread).
 *
 * @param self libfork-generated first argument (unused).
 * @param child_io Child IO to run.
 * @return libfork task producing the child's value.
 */
inline constexpr auto co_run =
    [](auto /*self*/,
       auto child_io) -> lf::task<typename std::remove_cvref_t<decltype(child_io)>::value_type>
{
    using io_type = std::remove_cvref_t<decltype(child_io)>;
    if constexpr (action_is_async(^^typename io_type::action))
    {
        using proxy_type = [:async_proxy_meta<typename io_type::action>():];
        // Materialise the child proxy and spread its arguments into its coroutine entry.
        auto proxy = std::move(child_io)().action();
        co_return co_await std::apply(
            [](auto &&... args) { return lf::just[proxy_type::fn](FW(args)...); }, FW(proxy).arg);
    }
    if constexpr (!action_is_async(^^typename io_type::action))
    {
        // Sync children run inline; their `sync_wait` never reaches libfork's scheduler entry.
        co_return std::move(child_io)().sync_wait();
    }
};

}  // namespace detail

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
 * argument types are decayed and stored by value.
 *
 * @tparam TTraits Traits used to validate the action and for the result IO, defaulting to IOTraits.
 * @tparam TFn Deduced stateless coroutine function type.
 * @tparam TArgs Deduced argument types (stored by value).
 * @param fn The stateless coroutine function.
 * @param args Arguments passed to the coroutine (moved/copied per value category).
 * @return IO over the async leaf with TTraits.
 */
template <class TTraits = IOTraits<>, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async([[maybe_unused]] TFn fn, TArgs &&... args)
{
    using arg_tuple_type = std::tuple<std::decay_t<TArgs>...>;
    static constexpr auto reason = detail::async_leaf_reason(^^TFn, ^^arg_tuple_type);
    static_assert(
        reason.empty(),
        fnfelt::detail::construct_type_error_msg(
            ^^TFn, TTraits::name, "async action is invalid: ", reason));
    if constexpr (reason.empty())
    {
        using action_type = AsyncLeafAction<TFn, arg_tuple_type>;
        return IO<action_type, TTraits>{action_type{arg_tuple_type{FW(args)...}}};
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
 * @return IO over the async leaf with IOTraits<name_cstr>.
 */
template <char const * name_cstr, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    return create_async<IOTraits<name_cstr>>(std::move(fn), FW(args)...);
}

/**
 * Async proxy composing a source IO with a continuation (async/mixed `and_then`).
 *
 * The driver runs the source IO, applies the continuation to its value with the same direct-vs-
 * spread dispatch as the synchronous action, then runs the continuation's IO.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TContinuation>
struct AndThenProxy : AsyncProxyTag
{
    /// Value the composed IO produces.
    using value_type = [:detail::and_then_result_meta<TSourceIO, TContinuation>():];
    /// Source IO and continuation spread into @ref fn.
    std::tuple<TSourceIO, TContinuation> arg;
    /// Reflection of the source's value.
    // Dispatch is computed here, at class scope, because gcc 16.2 rejects reflection queries in a
    // lambda's `if constexpr` condition.
    static constexpr std::meta::info source_value_meta = detail::io_value_meta(^^TSourceIO);
    /// Whether the continuation is applied directly to the source's value.
    static constexpr bool direct_dispatch =
        detail::is_directly_invocable(^^TContinuation, source_value_meta);
    /// Whether the continuation is applied by spreading the value's template arguments.
    static constexpr bool spread_dispatch =
        !direct_dispatch && detail::is_spread_invocable(^^TContinuation, source_value_meta);
    /// Async/mixed composition driver.
    static constexpr auto fn =
        [](auto, TSourceIO source, TContinuation continuation) -> lf::task<value_type>
    {
        auto input = co_await lf::just[detail::co_run](std::move(source));
        // Direct invocation wins over spreading, mirroring the synchronous action.
        if constexpr (direct_dispatch)
        {
            auto next = continuation(std::move(input));
            co_return co_await lf::just[detail::co_run](std::move(next));
        }
        if constexpr (spread_dispatch)
        {
            auto next = std::apply(continuation, std::move(input));
            co_return co_await lf::just[detail::co_run](std::move(next));
        }
    };

    /**
     * Construct from the source IO and continuation.
     *
     * @param arg_in Source IO and continuation to store.
     */
    constexpr explicit AndThenProxy(std::tuple<TSourceIO, TContinuation> arg_in)
        : arg{std::move(arg_in)}
    {
    }
};

/**
 * Proxy that runs an IO's action.
 *
 * `sync_wait` dispatches on whether the action is asynchronous (its invocation result derives from
 * @c AsyncProxyTag): synchronous actions are run inline and are constant-evaluable, while
 * asynchronous actions run on a libfork scheduler.
 *
 * @tparam TAction Callable action held by value.
 */
template <class TAction>
struct RunProxy
{
    /// The action to run.
    TAction action;

    /**
     * Run the held action and return its value.
     *
     * Scheduler arguments are accepted uniformly at every call site and ignored on the synchronous
     * path. An asynchronous action is run on the given scheduler, or on a default-constructed
     * libfork lazy pool when none is given; at most one scheduler argument is accepted.
     *
     * @param self The proxy to run (explicit object parameter).
     * @param scheduler Optional libfork scheduler servicing the entire async tree.
     * @return The value the action produces.
     */
    [[nodiscard]] constexpr auto sync_wait(this auto && self, [[maybe_unused]] auto &&... scheduler)
    {
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
