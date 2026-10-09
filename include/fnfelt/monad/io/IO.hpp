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

#include <fnfelt/monad/io/detail.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * IO monad for wrapping side-effecting actions.
 *
 * @tparam TAction Callable action. Must take no arguments and return a value.
 * @tparam TTraits Traits type stored by the monad. Not used by IO itself.
 */
template <class TAction, class TTraits>
class IO
{
public:
    /// Type alias for the wrapped action.
    using action_type = TAction;
    /// Type alias for the traits associated with the monad.
    using traits = TTraits;
    /// Type alias for the value the action produces when run.
    using value_type = [:detail::action_value_meta(^^TAction):];

    /**
     * Constructs an IO monad wrapping the given action.
     *
     * The action is perfect-forwarded into the stored member, so wrapping it costs no extra move
     * over storing it directly.
     *
     * Constrained to exclude the class itself: an unconstrained converting constructor is a better
     * match than the implicit copy constructor for a non-const lvalue IO, which would shadow
     * copying. Copying and moving an IO therefore use the copy and move constructors.
     *
     * @tparam TActionArg Deduced action argument type (an rvalue is moved, an lvalue copied).
     * @param action The side-effecting action to wrap.
     */
    template <class TActionArg>
    requires(!detail::is_io(^^TActionArg)) explicit constexpr IO(TActionArg && action)
        : action_{std::forward<TActionArg>(action)}
    {
    }

    /**
     * Run the wrapped action and return its value.
     *
     * @param self The IO instance to run (explicit object parameter).
     * @return The value the action produces.
     */
    constexpr auto build(this auto && self)
    {
        return FW(self).action_();
    }

private:
    action_type action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
