// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/async.hpp
 *
 * Async IO monad implementation details.
 *
 * Reflection helpers for asynchronous actions and their proxies.
 */
#pragma once

#include <meta>

#include <fnfelt/monad/io/detail.hpp>

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
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
