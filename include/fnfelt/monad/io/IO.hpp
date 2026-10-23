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

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/detail.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * IO monad for wrapping side-effecting actions.
 *
 * Call `io.build().sync_wait()` to run an IO; `build` yields a @ref IORunProxy rather than
 * running the action directly.
 *
 * @tparam TAction Callable action. Must take no arguments and return a value, or an async proxy
 * deriving AsyncProxyTag that exposes a `value_type` alias naming its result type.
 * @tparam TTraits Traits used to validate creation and execution of the IO.
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
     * Yield a run proxy holding this IO's action by value.
     *
     * Calling `sync_wait` on the returned proxy is the only execution path. The action is moved out
     * of an rvalue IO and copied out of an lvalue IO.
     *
     * @tparam TBuildTraits Action-transfer validation traits, defaulting to this monad's traits.
     * @param self The IO instance (explicit object parameter).
     * @return IORunProxy holding the wrapped action by value.
     */
    template <class TBuildTraits = TTraits>
    [[nodiscard]] constexpr auto build(this auto && self)
    {
        constexpr auto reason = TBuildTraits::validate_build(^^decltype((FW(self).action_)));
        static_assert(reason.empty(), detail::build_error_msg(TBuildTraits::name, reason));
        if constexpr (reason.empty())
        {
            return IORunProxy<action_type, TTraits>{FW(self).action_};
        }
        else
        {
            return detail::BuildError{};
        }
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
     * rvalue with the value (directly or spread), and must return an IO that produces a value. It
     * is validated by the traits template argument's `validate_and_then` (default
     * `detail::AndThenIOTraits<>`). The result IO also takes those traits; this IO's own traits are
     * not inherited. An invalid continuation is rejected by a friendly static_assert and yields
     * `detail::AndThenError`.
     *
     * @tparam TAndThenTraits Traits used to validate this IO and continuation pair, and for the
     * result IO, defaulting to detail::AndThenIOTraits.
     * @param self The IO instance to chain to (explicit object parameter).
     * @param continuation Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO, or detail::AndThenError if the
     * continuation is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TAndThenTraits = detail::AndThenIOTraits<>, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return io::and_then<TAndThenTraits>(FW(self), FW(continuation));
    }

    /**
     * Chain a continuation to this IO, naming the result.
     *
     * See the defaulted-traits member overload for the full contract, which is identical: the
     * result IO takes `detail::AndThenIOTraits<name_cstr>`, which is also used to validate this IO
     * and the continuation pair.
     *
     * @param self The IO instance to chain to (explicit object parameter).
     * @param continuation Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO with
     * detail::AndThenIOTraits<name_cstr>, or detail::AndThenError if the continuation is invalid
     * (which is also rejected by a friendly static_assert).
     */
    template <char const * name_cstr, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return io::and_then<name_cstr>(FW(self), FW(continuation));
    }

    /**
     * Transform this IO's value with a transformer, re-wrapping the plain result in an IO.
     *
     * The transformer takes the value this IO produces and returns a plain value; the returned IO
     * runs this one and produces the transformer's value. Values that are specialisations of
     * `std::pair` or `std::tuple` are spread into the transformer unless it accepts the value
     * directly. Unlike `and_then`, the transformer's result is not run as an IO.
     *
     * An invalid transformer is rejected by a friendly static_assert and yields
     * `detail::TransformError`.
     *
     * @tparam TTransformTraits Traits used to validate this IO and transformer pair, and for the
     * result IO, defaulting to detail::TransformIOTraits.
     * @param self The IO instance to transform (explicit object parameter).
     * @param transformer Transformer taking the value and returning a plain value.
     * @return New IO that encapsulates the value after transformation, or detail::TransformError if
     * the transformer is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TTransformTraits = detail::TransformIOTraits<>, class TTransformer>
    [[nodiscard]] constexpr auto transform(this auto && self, TTransformer transformer)
    {
        return io::transform<TTransformTraits>(FW(self), FW(transformer));
    }

    /**
     * Transform this IO's value with a transformer, re-wrapping the plain result in an IO.
     *
     * This overload allows the returned IO type to be named, for use in diagnostics.
     *
     * See the defaulted-traits member overload for the full contract, which is identical.
     *
     * @param self The IO instance to transform (explicit object parameter).
     * @param transformer Transformer taking the value and returning a plain value.
     * @return New IO that encapsulates the value after transformation, or detail::TransformError if
     * the transformer is invalid (which is also rejected by a friendly static_assert).
     */
    template <char const * name_cstr, class TTransformer>
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    [[nodiscard]] constexpr auto transform(this auto && self, TTransformer transformer)
    {
        return io::transform<name_cstr>(FW(self), FW(transformer));
    }

    /**
     * Complete this expected result with a conditional fallback effect.
     *
     * @tparam TSelectTraits Validation and result traits.
     * @param self Choice IO.
     * @param fallback IO producing a function for the unexpected input.
     * @return Selective IO.
     */
    template <class TSelectTraits = detail::SelectIOTraits<>, class TFallback>
    [[nodiscard]] constexpr auto select(this auto && self, TFallback && fallback)
    {
        return io::select<TSelectTraits>(FW(self), FW(fallback));
    }

    /**
     * Complete this expected result with a named conditional fallback effect.
     *
     * @tparam name_cstr Diagnostic name with static storage duration.
     * @param self Choice IO.
     * @param fallback IO producing a function for the unexpected input.
     * @return Named selective IO.
     */
    template <char const * name_cstr, class TFallback>
    [[nodiscard]] constexpr auto select(this auto && self, TFallback && fallback)
    {
        return io::select<name_cstr>(FW(self), FW(fallback));
    }

    /**
     * Repeat this IO in a loop until a stepper reports success, producing an IO of the final value.
     *
     * The returned IO, when run, loops: this IO is re-run on EVERY iteration (its action is copied
     * out of the stored source, so this IO's action must be copy constructible), the stepper is
     * invoked with the current state and the freshly produced value as exactly two arguments, and
     * the state is threaded only through the stepper's `std::expected` result: an `std::unexpected`
     * value becomes the next iteration's state, and the first `std::expected` value becomes the
     * loop's result.
     *
     * The stepper is taken by value and moved into the free `repeat_until` as an rvalue: lvalues
     * are copied into the parameter, and the caller's lvalue is left untouched. It must be a
     * complete, non-reference, move constructible class or function pointer type, callable as an
     * lvalue receiver with the state and value (exactly two arguments, no spreading), and must
     * return `std::expected<NewValue, State>` where the unexpected type is exactly the state type.
     * The state must be a complete, movable type. It is validated by the traits template argument's
     * `validate_repeat_until` (default `detail::RepeatUntilIOTraits<>`). The result IO also takes
     * those traits; this IO's own traits are not inherited. An invalid stepper is rejected by a
     * friendly static_assert and yields `detail::RepeatUntilError`.
     *
     * @tparam TRepeatUntilTraits Traits used to validate this IO and the state and stepper
     * combination, and for the result IO, defaulting to detail::RepeatUntilIOTraits.
     * @param self The IO instance to repeat (explicit object parameter).
     * @param initial_state Initial state threaded into the stepper.
     * @param stepper Stepper taking the state and value, returning an expected.
     * @return IO re-running this IO until the stepper reports success, or detail::RepeatUntilError
     * if the combination is invalid (which is also rejected by a friendly static_assert).
     */
    template <
        class TRepeatUntilTraits = detail::RepeatUntilIOTraits<>,
        class TInitialState,
        class TStepper>
    [[nodiscard]] constexpr auto repeat_until(
        this auto && self, TInitialState initial_state, TStepper stepper)
    {
        return io::repeat_until<TRepeatUntilTraits>(FW(self), FW(initial_state), FW(stepper));
    }

    /**
     * Repeat this IO in a loop until a stepper reports success, naming the result.
     *
     * This overload allows the returned IO type to be named, for use in diagnostics.
     *
     * See the defaulted-traits member overload for the full contract, which is identical: the
     * result IO takes `detail::RepeatUntilIOTraits<name_cstr>`, which is also used to validate this
     * IO and the state and stepper combination.
     *
     * @param self The IO instance to repeat (explicit object parameter).
     * @param initial_state Initial state threaded into the stepper.
     * @param stepper Stepper taking the state and value, returning an expected.
     * @return IO re-running this IO until the stepper reports success with
     * detail::RepeatUntilIOTraits<name_cstr>, or detail::RepeatUntilError if the combination is
     * invalid (which is also rejected by a friendly static_assert).
     */
    template <char const * name_cstr, class TInitialState, class TStepper>
    [[nodiscard]] constexpr auto repeat_until(
        this auto && self, TInitialState initial_state, TStepper stepper)
    {
        return io::repeat_until<name_cstr>(FW(self), FW(initial_state), FW(stepper));
    }

private:
    action_type action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
