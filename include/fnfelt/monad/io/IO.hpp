// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file IO.hpp
 *
 * IO monad class.
 */
#pragma once
#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/detail/errors.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/traits.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * IO monad for wrapping side-effecting actions.
 *
 * @tparam TAction Callable action. Must take no arguments and return a value.
 * @tparam TTraits Traits used for validation and diagnostics.
 */
template <class TAction, class TTraits>
class IO
{
public:
    /// Type alias for the wrapped action.
    using action = TAction;
    /// Type alias for the traits used to validate the action.
    using traits = TTraits;
    /// Type alias for the value the action produces when run.
    using value_type = [:detail::action_value_meta(^^TAction):];

    /// Validation
    consteval
    {
        constexpr auto reason = traits::validate_action(^^TAction);
        static_assert(
            reason.empty(),
            fnfelt::detail::construct_type_error_msg(
                ^^TAction, traits::name, "action is invalid: ", reason));
    }

    /**
     * Constructs an IO monad wrapping the given action.
     *
     * @param action The side-effecting action to wrap.
     */
    explicit constexpr IO(action action) : action_{FW(action)} {}

    /**
     * Run the wrapped action and return its value.
     *
     * @param self The IO instance to run (explicit object parameter).
     * @return The value the action produces.
     */
    constexpr auto operator()(this auto && self)
    {
        return FW(self).action_();
    }

private:
    action action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
