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

    /**
     * Chain a continuation to this IO, producing an IO of the continuation's value.
     *
     * The continuation takes the value this IO produces and returns an IO; the returned IO runs
     * this one, applies the continuation, then runs its IO. Values that are specialisations of
     * `std::pair` or `std::tuple` are spread into the continuation unless it accepts the value
     * directly.
     *
     * The continuation is taken by value and moved into the free `and_then` as an rvalue: lvalues
     * are copied into the parameter, and the caller's lvalue is left untouched. It must be a
     * complete, non-reference, move constructible class or function pointer type, callable as an
     * rvalue with the value (directly or spread), and must return an IO. It is validated by
     * the traits template argument's `validate_and_then` (default `AndThenIOTraits<>`), which may
     * be overridden by passing custom traits explicitly. The result IO also takes those traits;
     * this IO's own traits are not inherited. An invalid continuation is rejected by a friendly
     * static_assert and yields `detail::AndThenError`.
     *
     * @tparam TAndThenTraits Traits used to validate this IO and continuation pair, and for the
     * result IO, defaulting to AndThenIOTraits.
     * @param self The IO instance to chain to (explicit object parameter).
     * @param continuation Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO, or detail::AndThenError if the
     * continuation is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TAndThenTraits = AndThenIOTraits<>, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return io::and_then<TAndThenTraits>(FW(self), FW(continuation));
    }

    /**
     * Chain a continuation to this IO, naming the result.
     *
     * See the defaulted-traits member overload for the full contract, which is identical: the
     * result IO takes `AndThenIOTraits<name_cstr>`, which is also used to validate this IO and the
     * continuation pair.
     *
     * @param self The IO instance to chain to (explicit object parameter).
     * @param continuation Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO with AndThenIOTraits<name_cstr>,
     * or detail::AndThenError if the continuation is invalid (which is also rejected by a friendly
     * static_assert).
     */
    template <char const * name_cstr, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return io::and_then<name_cstr>(FW(self), FW(continuation));
    }

private:
    action_type action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
