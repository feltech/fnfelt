// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/create.hpp
 *
 * Construction of StateIO values from state-consuming callables.
 */
#pragma once

#include <type_traits>
#include <utility>

#include <fnfelt/monad/state_io/StateIO.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{

/**
 * Creates a StateIO wrapping action with the given traits (default detail::CreateStateIOTraits).
 *
 * The action is decayed and perfect-forwarded: lvalues are copied, rvalues moved, and function
 * lvalues decay to function pointers.
 *
 * The action/state pair is validated by TTraits before the StateIO is constructed; an invalid
 * pair is rejected by a readable static_assert.
 *
 * @tparam TState The environment the action consumes.
 * @tparam TTraits Traits used to validate the action/state pair, defaulting to
 * detail::CreateStateIOTraits.
 * @tparam TAction Deduced callable action type.
 * @param action The state-consuming action to wrap.
 * @return StateIO over the decayed action with TTraits.
 */
template <class TState, class TTraits, class TAction>
[[nodiscard]] constexpr auto create(TAction && action)
{
    using action_type = std::decay_t<TAction>;
    // Validate the action/state pair at compile time, failing with the traits' readable error.
    static constexpr auto reason = TTraits::validate_action(^^action_type, ^^TState);
    static_assert(
        reason.empty(), detail::create_error_msg<^^action_type, ^^TState>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return StateIO<TState, action_type, TTraits>{FW(action)};
    }
    else
    {
        return detail::CreateError{};
    }
}

/**
 * Creates a StateIO wrapping action, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the action. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The action/state pair is validated by detail::CreateStateIOTraits<name_cstr> before the StateIO
 * is constructed; an invalid pair is rejected by a readable static_assert.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My StateIO"_ss`).
 * @tparam TState The environment the action consumes.
 * @tparam TAction Deduced callable action type.
 * @param action The state-consuming action to wrap.
 * @return StateIO over the decayed action with detail::CreateStateIOTraits<name_cstr>.
 */
template <char const * name_cstr, class TState, class TAction>
[[nodiscard]] constexpr auto create(TAction && action)
{
    return state_io::create<TState, detail::CreateStateIOTraits<name_cstr>>(FW(action));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
