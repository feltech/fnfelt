// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
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
 * @tparam TTraits Traits used to validate the action and bind continuations, and naming the
 * instantiation in diagnostics.
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
    using value = [:detail::action_value_meta(^^TAction):];
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
    explicit constexpr IO(action action) : action_{std::move(action)} {}

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

    /**
     * Bind a continuation (kleisli) to this IO, producing an IO of the continuation's value.
     *
     * The continuation takes the value this IO produces and returns an IO; the returned IO runs
     * this one, applies the continuation, then runs its IO. Values that are specialisations with
     * all-type template arguments (e.g. `std::pair`, `std::tuple`) are spread into the continuation
     * unless it accepts the value directly.
     *
     * The continuation must be a complete, non-reference, move constructible class or function
     * pointer type, callable as a const lvalue with the value (directly or spread), and must
     * return an IO. It is validated by `traits::validate_kleisli`, which may be overridden via the
     * traits parameter. The continuation is taken by value: lvalues are copied and rvalues are
     * moved.
     *
     * @param self The IO instance to bind to (explicit object parameter).
     * @param kleisli Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO, or detail::BindError if the
     * continuation is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TKleisli>
    [[nodiscard]] constexpr auto bind(this auto && self, TKleisli kleisli)
    {
        static constexpr auto reason = traits::validate_kleisli(^^value, ^^TKleisli);
        static_assert(
            reason.empty(),
            fnfelt::detail::construct_type_error_msg(
                ^^TKleisli, traits::name, "bind continuation is invalid: ", reason));
        if constexpr (reason.empty())
        {
            using action_t = detail::BindAction<std::decay_t<decltype(self)>, TKleisli>;
            return IO<action_t, traits>{action_t{FW(self), std::move(kleisli)}};
        }
        if constexpr (!reason.empty())
        {
            return detail::BindError{};
        }
    }

private:
    action action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
