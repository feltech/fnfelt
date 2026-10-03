// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file create.hpp
 *
 * Free `create` factory functions for the IO monad.
 *
 * This header includes IO.hpp, so the IO class is complete, and defines the free `create`
 * functions.
 */
#pragma once

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/traits.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Creates an IO wrapping action, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the action. Overloads on the first template argument: a name
 * (`char const *` non-type parameter) or a traits type.
 *
 * The action is decayed and perfect-forwarded: lvalues are copied, rvalues moved, and function
 * lvalues decay to function pointers.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My IO"_ss`).
 * @tparam TAction Deduced callable action type.
 * @param action The side-effecting action to wrap.
 * @return IO over the decayed action with IOTraits<name_cstr>.
 */
template <char const * name_cstr, class TAction>
[[nodiscard]] constexpr auto create(TAction && action)
{
    return IO<std::decay_t<TAction>, IOTraits<name_cstr>>{FW(action)};
}

/**
 * Creates an IO wrapping action with the given traits (default IOTraits).
 *
 * @tparam TTraits Traits used to validate the action, defaulting to IOTraits.
 * @tparam TAction Deduced callable action type.
 * @param action The side-effecting action to wrap.
 * @return IO over the decayed action with TTraits.
 */
template <class TTraits = IOTraits<>, class TAction>
[[nodiscard]] constexpr auto create(TAction && action)
{
    return IO<std::decay_t<TAction>, TTraits>{FW(action)};
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
