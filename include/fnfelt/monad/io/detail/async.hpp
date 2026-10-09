// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/async.hpp
 *
 * Implementation details for asynchronous IO execution.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <libfork/core/control_flow.hpp>
#include <libfork/core/just.hpp>
#include <libfork/core/scheduler.hpp>
#include <libfork/core/sync_wait.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Check whether an action is asynchronous, i.e. its invocation result derives from
 * @c AsyncProxyTag.
 *
 * @param action_meta Reflection of the action type.
 * @return True if the action's invocation result derives from @c AsyncProxyTag.
 */
consteval bool action_is_async(std::meta::info action_meta)
{
    action_meta = dealias(remove_cvref(action_meta));
    // Invocability queries are a hard, non-catchable gcc error for incomplete types.
    if (!is_complete_type(action_meta) || !is_invocable_type(action_meta, {}))
    {
        return false;
    }
    std::meta::info const result_meta = remove_cvref(invoke_result(action_meta, {}));
    // `is_base_of_type` is a hard error for an incomplete derived type.
    if (!is_complete_type(result_meta) || !is_class_type(result_meta))
    {
        return false;
    }
    // Belt-and-braces for the remaining throwing paths.
    try
    {
        return is_base_of_type(^^AsyncProxyTag, result_meta);
    }
    catch (std::meta::exception const &)
    {
        return false;
    }
}

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
 * Find the `value_type` alias an async proxy exposes.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return Reflection of the alias member, or `void` if no such alias is found. The `void` is the
 * diagnostic placeholder described on `action_value_meta`.
 */
consteval std::meta::info async_proxy_value_alias(std::meta::info proxy_meta)
{
    // Querying members of an incomplete type is a hard error, so guard completeness first.
    if (!is_complete_type(proxy_meta) || !is_class_type(proxy_meta))
    {
        return ^^void;
    }
    // `members_of` does not list inherited members, so bases are walked too.
    for (std::meta::info const member :
         members_of(proxy_meta, std::meta::access_context::unprivileged()))
    {
        if (is_type_alias(member) && has_identifier(member) &&
            identifier_of(member) == "value_type")
        {
            return member;
        }
    }
    for (std::meta::info const base :
         bases_of(proxy_meta, std::meta::access_context::unprivileged()))
    {
        std::meta::info const found = async_proxy_value_alias(type_of(base));
        if (is_type_alias(found))
        {
            return found;
        }
    }
    return ^^void;
}

/// Normal member lookup also handles aliases omitted by gcc's reflected member list.
template <class TProxy>
using async_proxy_value_t = typename TProxy::value_type;

/**
 * Check whether an async proxy exposes a `value_type` alias.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return True if the proxy (or a base) has a `value_type` alias.
 */
consteval bool async_proxy_has_value(std::meta::info proxy_meta)
{
    if (is_type_alias(async_proxy_value_alias(proxy_meta)))
    {
        return true;
    }
    // gcc omits aliases targeting capturing lambda types from `members_of`, even on a complete
    // class. Ordinary dependent member lookup still resolves them correctly.
    return is_complete_type(proxy_meta) && is_class_type(proxy_meta) &&
        can_substitute(^^async_proxy_value_t, {proxy_meta});
}

/**
 * Reflection of the value an async proxy produces when run.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return Reflection of the `value_type` alias target, or `void` if no alias is found. The `void`
 * is the diagnostic placeholder described on `action_value_meta`.
 */
consteval std::meta::info async_proxy_value_meta(std::meta::info proxy_meta)
{
    if (is_complete_type(proxy_meta) && is_class_type(proxy_meta) &&
        can_substitute(^^async_proxy_value_t, {proxy_meta}))
    {
        return dealias(substitute(^^async_proxy_value_t, {proxy_meta}));
    }
    // `type_of` throws for alias reflections on gcc 16.2, so dealias before use.
    auto const alias = async_proxy_value_alias(proxy_meta);
    if (is_type_alias(alias))
    {
        return dealias(alias);
    }
    return ^^void;
}

/**
 * Check whether an async proxy inherits conflicting `value_type` aliases.
 *
 * An own `value_type` alias hides any inherited alias, so it is never ambiguous. Otherwise, if two
 * or more bases expose different `value_type` alias targets, the aliases are ambiguous; identical
 * targets from several bases are not.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return True if the proxy inherits differing `value_type` aliases from more than one base.
 */
consteval bool async_proxy_ambiguous_value(std::meta::info proxy_meta)
{
    if (!is_complete_type(proxy_meta) || !is_class_type(proxy_meta))
    {
        return false;
    }
    if (can_substitute(^^async_proxy_value_t, {proxy_meta}))
    {
        return false;
    }
    // An own alias hides inherited ones, resolving any ambiguity.
    for (std::meta::info const member :
         members_of(proxy_meta, std::meta::access_context::unprivileged()))
    {
        if (is_type_alias(member) && has_identifier(member) &&
            identifier_of(member) == "value_type")
        {
            return false;
        }
    }
    // Compare the dealiased alias targets exposed by the direct bases; `async_proxy_value_alias`
    // already walks each base's own bases, so indirect aliases count as well.
    bool have_target = false;
    std::meta::info first_target = ^^void;
    for (std::meta::info const base :
         bases_of(proxy_meta, std::meta::access_context::unprivileged()))
    {
        std::meta::info const alias = async_proxy_value_alias(type_of(base));
        if (!is_type_alias(alias))
        {
            continue;
        }
        std::meta::info const target = dealias(alias);
        if (!have_target)
        {
            first_target = target;
            have_target = true;
            continue;
        }
        if (!is_same_type(first_target, target))
        {
            return true;
        }
    }
    return false;
}

/**
 * Probe for whether a type can be value-initialised in a constant expression.
 *
 * Instantiating this template requires its non-type default argument `T{}` to be a constant
 * expression, so a `consteval` `can_substitute` with just the type argument distinguishes types
 * whose default constructor is usable in constant expressions from those whose is not. This models
 * the `static constexpr TFn fn{}` entry point stored by an async proxy.
 *
 * @tparam T Type to probe.
 * @tparam value Unused; instantiated only when `T{}` is a constant expression.
 */
template <class T, T value = T{}>
struct ConstexprDefaultProbe
{
};

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
    auto result = std::apply(
        [&sched](auto &&... args)
        { return lf::sync_wait(FW(sched), std::remove_cvref_t<TProxy>::fn, FW(args)...); },
        FW(proxy).arg);
    if constexpr (is_base_of_type(^^TransportProxyTag, ^^std::remove_cvref_t<TProxy>))
    {
        return unpack_result(std::move(result));
    }
    else
    {
        return result;
    }
}

/**
 * Run a synchronous child IO inline on the awaiting worker.
 *
 * A plain callable, not a coroutine: `lf::just` invokes it inline with no libfork machinery, so a
 * sync child runs on the awaiting worker thread with no suspension (and no steal opportunity for
 * the awaiting task).
 *
 * @param child_io Child IO to run.
 * @return The child's value.
 */
inline constexpr auto run_sync_inline = [](auto child_io)
{
    using value_type = typename std::remove_cvref_t<decltype(child_io)>::value_type;
    return pack_result<value_type>(std::move(child_io).build().sync_wait());
};

/**
 * Run a synchronous child IO inside a task wrapper, for an `lf::fork`/`lf::call` slot.
 *
 * `lf::fork`/`lf::call` accept only coroutine functions, so a slot-bound sync child needs one task
 * frame; the child's `sync_wait` runs inline inside it. Never re-enter `lf::sync_wait` on a worker
 * thread - it throws `schedule_in_worker`, so the whole tree is serviced by the one top-level
 * scheduler. libfork's `lf::lift` wraps a plain callable in one extra task frame; an equivalent
 * minimal wrapper is used here instead.
 *
 * @param self libfork-generated first argument (unused).
 * @param child_io Child IO to run.
 * @return libfork task producing the child's value.
 */
inline constexpr auto run_sync_child = [](auto /*self*/, auto child_io)
    -> lf::task<task_result_t<typename std::remove_cvref_t<decltype(child_io)>::value_type>>
{
    using value_type = std::remove_cvref_t<decltype(child_io)>::value_type;
    co_return pack_result<value_type>(std::move(child_io).build().sync_wait());
};

/**
 * Awaitable that runs a child IO to completion inside a libfork coroutine.
 *
 * Async children are scheduled through their proxy entry; sync children run inline on the awaiting
 * worker with no suspension. Async leaves retain bound arguments in an owning adapter task. One
 * scheduler services the whole tree (never re-enter `lf::sync_wait` on a worker).
 *
 * @param child_io Child IO to run.
 * @return Awaitable producing the child's value.
 */
inline constexpr auto just_child = [](auto child_io)
{
    using io_type = std::remove_cvref_t<decltype(child_io)>;
    if constexpr (action_is_async(^^typename io_type::action_type))
    {
        // The proxy entry owns its arguments by value. Async leaves wrap the user's coroutine so
        // reference parameters point into that owning frame, rather than this local proxy.
        auto proxy = std::move(child_io).build().action();
        using proxy_type = std::remove_cvref_t<decltype(proxy)>;
        return std::apply(
            [](auto &&... args) { return lf::just[proxy_type::fn](FW(args)...); },
            std::move(proxy).arg);
    }
    else
    {
        // Sync children run inline on the awaiting worker, with no suspension.
        return lf::just[run_sync_inline](std::move(child_io));
    }
};

/**
 * Awaitable that forks a child IO into an `lf::eventually` slot.
 *
 * Async children are forked through their owning proxy entry. Sync children need one wrapper task
 * because `lf::fork` accepts only coroutine functions,
 * and the child's exception must stay stashed in the slot until the join, never surfacing while the
 * other side is still outstanding.
 *
 * @param slot `lf::eventually` slot receiving the child's value.
 * @param child_io Child IO to run.
 * @return Awaitable delivering the child's value to @c slot.
 */
inline constexpr auto fork_child = [](auto & slot, auto child_io)
{
    using io_type = std::remove_cvref_t<decltype(child_io)>;
    if constexpr (action_is_async(^^typename io_type::action_type))
    {
        auto proxy = std::move(child_io).build().action();
        using proxy_type = std::remove_cvref_t<decltype(proxy)>;
        return std::apply(
            [&slot](auto &&... args) { return lf::fork[&slot, proxy_type::fn](FW(args)...); },
            std::move(proxy).arg);
    }
    else
    {
        return lf::fork[&slot, run_sync_child](std::move(child_io));
    }
};

/**
 * Awaitable that calls a child IO into an `lf::eventually` slot.
 *
 * Async children are called through their owning proxy entry. Sync children need one wrapper task
 * because `lf::call` accepts only coroutine functions,
 * and the child's exception must stay stashed in the slot until the join, never surfacing while the
 * other side is still outstanding.
 *
 * @param slot `lf::eventually` slot receiving the child's value.
 * @param child_io Child IO to run.
 * @return Awaitable delivering the child's value to @c slot.
 */
inline constexpr auto call_child = [](auto & slot, auto child_io)
{
    using io_type = std::remove_cvref_t<decltype(child_io)>;
    if constexpr (action_is_async(^^typename io_type::action_type))
    {
        auto proxy = std::move(child_io).build().action();
        using proxy_type = std::remove_cvref_t<decltype(proxy)>;
        return std::apply(
            [&slot](auto &&... args) { return lf::call[&slot, proxy_type::fn](FW(args)...); },
            std::move(proxy).arg);
    }
    else
    {
        return lf::call[&slot, run_sync_child](std::move(child_io));
    }
};

}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
