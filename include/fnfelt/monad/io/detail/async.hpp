// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/async.hpp
 *
 * Implementation-detail helpers for asynchronous IO: async leaves and sync/async composition
 * drivers.
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

/**
 * Check whether an async proxy exposes a `value_type` alias.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return True if the proxy (or a base) has a `value_type` alias.
 */
consteval bool async_proxy_has_value(std::meta::info proxy_meta)
{
    // The `void` placeholder is not an alias member, so `is_type_alias` discriminates it.
    return is_type_alias(async_proxy_value_alias(proxy_meta));
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
    // `type_of` throws for alias reflections on gcc 16.2, so dealias before use.
    return dealias(async_proxy_value_alias(proxy_meta));
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
    return std::apply(
        [&sched](auto &&... args)
        { return lf::sync_wait(FW(sched), std::remove_cvref_t<TProxy>::fn, FW(args)...); },
        FW(proxy).arg);
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
{ return std::move(child_io).build().sync_wait(); };

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
inline constexpr auto run_sync_child =
    [](auto /*self*/,
       auto child_io) -> lf::task<typename std::remove_cvref_t<decltype(child_io)>::value_type>
{ co_return std::move(child_io).build().sync_wait(); };

/**
 * Awaitable that runs a child IO to completion inside a libfork coroutine.
 *
 * Async children are scheduled as their own libfork task (exactly one child frame, the child's
 * own); sync children run inline on the awaiting worker with no suspension. A single top-level
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
        // Materialise the child proxy and spread its arguments into its coroutine entry; the child
        // frame copies the spread arguments, so `proxy` need not outlive this call. One child frame
        // - the child's own - with no intermediary driver task.
        auto proxy = std::move(child_io).build().action();
        using proxy_type = std::remove_cvref_t<decltype(proxy)>;
        return std::apply(
            [](auto &&... args) { return lf::just[proxy_type::fn](FW(args)...); },
            std::move(proxy).arg);
    }
    if constexpr (!action_is_async(^^typename io_type::action_type))
    {
        // Sync children run inline on the awaiting worker, with no suspension.
        return lf::just[run_sync_inline](std::move(child_io));
    }
};

/**
 * Awaitable that forks a child IO into an `lf::eventually` slot.
 *
 * Async children are forked as their own task (one child frame, the child's own, no intermediary
 * driver). Sync children need one wrapper task because `lf::fork` accepts only coroutine functions,
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
    if constexpr (!action_is_async(^^typename io_type::action_type))
    {
        return lf::fork[&slot, run_sync_child](std::move(child_io));
    }
};

/**
 * Awaitable that calls a child IO into an `lf::eventually` slot.
 *
 * Async children are called as their own task (one child frame, the child's own, no intermediary
 * driver). Sync children need one wrapper task because `lf::call` accepts only coroutine functions,
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
    if constexpr (!action_is_async(^^typename io_type::action_type))
    {
        return lf::call[&slot, run_sync_child](std::move(child_io));
    }
};

/**
 * Constructs a detailed error message for an invalid `io::create_async` function.
 *
 * The message includes the IO name, the specific validation failure reason, and the offending
 * function's display string, so the reason stays readable before any compiler truncation of the
 * message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam fn_meta Reflection of the (decayed) async function type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_create_async`.
 * @return Formatted error message string.
 */
template <std::meta::info fn_meta>
consteval std::string create_async_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    append_string_view(msg, io_name);
    msg += ' ';
    msg += "async action is invalid: ";
    append_string_view(msg, reason);
    msg += ": ";
    append_string_view(msg, display_string_of(fn_meta));
    return msg;
}

}  // namespace detail

/**
 * Traits validating the stateless coroutine function provided to `io::create_async`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct CreateAsyncIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the stateless coroutine function provided to @ref create_async.
     *
     * The function must be a stateless class usable in constant expressions as
     * `static constexpr TFn fn{}` (stateful coroutine functions dangle in libfork, which stores
     * only a copy of the function object), invocable as an rvalue with a libfork-generated first
     * argument and the stored argument tuple's element types, and produce a non-void `lf::task`.
     * The rvalue receiver matches libfork's `std::move(fun)(self, args...)` entry invocation, so a
     * captureless mutable coroutine lambda is accepted.
     *
     * This is the overridable default implementation used by `create_async`: custom traits may
     * replace it to change which coroutine functions are accepted.
     *
     * @param fn_meta Reflection of the async function type.
     * @param arg_tuple_meta Reflection of the stored argument tuple type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_create_async(
        std::meta::info fn_meta, std::meta::info arg_tuple_meta)
    {
        std::meta::info const target = dealias(remove_cvref(fn_meta));
        // The proxy stores the function as a `static constexpr TFn`, which requires a class type.
        if (!is_complete_type(target) || !is_class_type(target))
        {
            return "provided async function must be a class type";
        }
        if (!detail::is_empty_object(target))
        {
            return "provided async function must be stateless";
        }
        if (!is_default_constructible_type(target))
        {
            return "provided async function must be default-constructible";
        }
        // `static constexpr TFn fn{}` must be constant-initialisable, and the entry is also passed
        // to libfork as a class-type non-type template argument, so the type must be usable in
        // constant expressions and structural.
        if (!can_substitute(^^detail::ConstexprDefaultProbe, {target}))
        {
            return "provided async function must be usable in constant expressions";
        }
        std::vector<std::meta::info> const args = detail::async_leaf_arg_metas(arg_tuple_meta);
        if (!is_invocable_type(target, args))
        {
            return "provided async function is not invocable with the given arguments";
        }
        std::meta::info const result = dealias(invoke_result(target, args));
        // `template_of` comparison is the same idiom as `is_io`; `lf::task` is an inline-namespace
        // name.
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
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
