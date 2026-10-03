// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file IO.hpp
 *
 * IO monad class with its delegating member `bind` forms.
 *
 * The free `bind` and `ap` functions are defined in their own headers, bind.hpp and ap.hpp, and
 * are provided by the umbrella `<fnfelt/monad/io.hpp>` (which includes this header). Free `bind`
 * is forward-declared before the IO class so the member overloads can delegate to it.
 */
#pragma once
#include <meta>

#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/errors.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>
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
    /// Name of this IO (defaults to "IO").
    static constexpr std::string_view name = traits::name;

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
     * The continuation is taken by value: lvalues are copied and rvalues are moved. It must be a
     * complete, non-reference, move constructible class or function pointer type, callable as a
     * const lvalue with the value (directly or spread), and must return an IO. It is validated by
     * the traits template argument's `validate_bind` (default `IOTraits<>`), which may be
     * overridden by passing custom traits explicitly. The result IO also takes those traits; this
     * IO's own traits are not inherited. An invalid continuation is rejected by a friendly
     * static_assert and yields `detail::BindError`.
     *
     * @tparam TBindTraits Traits used to validate this IO and continuation pair, and for the
     * result IO, defaulting to IOTraits.
     * @param self The IO instance to bind to (explicit object parameter).
     * @param kleisli Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO, or detail::BindError if the
     * continuation is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TBindTraits = IOTraits<>, class TKleisli>
    [[nodiscard]] constexpr auto bind(this auto && self, TKleisli kleisli)
    {
        return io::bind<TBindTraits>(FW(self), std::move(kleisli));
    }

    /**
     * Bind a continuation (kleisli) to this IO, naming the result.
     *
     * See the defaulted-traits member overload for the full contract, which is identical: the
     * result IO takes `IOTraits<name_cstr>`, which is also used to validate this IO and the
     * continuation pair.
     *
     * @param self The IO instance to bind to (explicit object parameter).
     * @param kleisli Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO with IOTraits<name_cstr>, or
     * detail::BindError if the continuation is invalid (which is also rejected by a friendly
     * static_assert).
     */
    template <char const * name_cstr, class TKleisli>
    [[nodiscard]] constexpr auto bind(this auto && self, TKleisli kleisli)
    {
        return io::bind<name_cstr>(FW(self), std::move(kleisli));
    }

private:
    action action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
