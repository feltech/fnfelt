// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file create.hpp
 *
 * Construction of IO values from callables.
 */
#pragma once

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

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
 * The action is validated by detail::CreateIOTraits<name_cstr> before the IO is constructed; an
 * invalid action is rejected by a readable static_assert.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My IO"_ss`).
 * @tparam TAction Deduced callable action type.
 * @param action The side-effecting action to wrap.
 * @return IO over the decayed action with detail::CreateIOTraits<name_cstr>.
 */
template <char const * name_cstr, class TAction>
[[nodiscard]] constexpr auto create(TAction && action)
{
    return io::create<detail::CreateIOTraits<name_cstr>>(FW(action));
}

/**
 * Creates an IO wrapping action with the given traits (default detail::CreateIOTraits).
 *
 * The action is validated by TTraits before the IO is constructed; an invalid action is rejected by
 * a readable static_assert.
 *
 * @tparam TTraits Traits used to validate the action, defaulting to detail::CreateIOTraits.
 * @tparam TAction Deduced callable action type.
 * @param action The side-effecting action to wrap.
 * @return IO over the decayed action with TTraits.
 */
template <class TTraits, class TAction>
[[nodiscard]] constexpr auto create(TAction && action)
{
    using action_type = std::decay_t<TAction>;
    // Validate the action at compile time, failing with the traits' readable error.
    constexpr auto reason = TTraits::validate_action(^^action_type);
    static_assert(reason.empty(), detail::create_error_msg<^^action_type>(TTraits::name, reason));
    return IO<action_type, TTraits>{FW(action)};
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
