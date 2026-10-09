// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/fwd.hpp
 *
 * Implementation-detail forward declarations for the IO monad.
 */
#pragma once

#include <meta>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/// Check whether a reflection is of a (cv-ref-stripped) specialisation of IO, with any traits.
consteval bool is_io(std::meta::info type_meta);

/**
 * Check whether an action is asynchronous, i.e. its invocation result derives from
 * @c AsyncProxyTag.
 *
 * @param action_meta Reflection of the action type.
 * @return True if the action's invocation result derives from @c AsyncProxyTag.
 */
consteval bool action_is_async(std::meta::info action_meta);

/**
 * Check whether an async proxy exposes a `value_type` alias.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return True if the proxy (or a base) has a `value_type` alias.
 */
consteval bool async_proxy_has_value(std::meta::info proxy_meta);

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
consteval bool async_proxy_ambiguous_value(std::meta::info proxy_meta);

/**
 * Reflection of the value an async proxy produces when run.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return Reflection of the `value_type` alias target, or `void` if no alias is found. The `void`
 * is the diagnostic placeholder described on `action_value_meta`.
 */
consteval std::meta::info async_proxy_value_meta(std::meta::info proxy_meta);
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
