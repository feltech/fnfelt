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
 * Call `io.build().sync_wait()` to run an IO; `build` yields a @ref RunProxy rather than
 * running the action directly.
 *
 * @tparam TAction Callable action. Must take no arguments and return a value, or an async proxy
 * deriving AsyncProxyTag that exposes a `value_type` alias naming its result type.
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
     * Yield a run proxy holding this IO's action by value.
     *
     * Calling `sync_wait` on the returned proxy is the only execution path. The action is moved out
     * of an rvalue IO and copied out of an lvalue IO.
     *
     * @param self The IO instance (explicit object parameter).
     * @return RunProxy holding the wrapped action by value.
     */
    [[nodiscard]] constexpr RunProxy<action_type> build(this auto && self)
    {
        return RunProxy<action_type>{FW(self).action_};
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
     * result IO, defaulting to TransformIOTraits.
     * @param self The IO instance to transform (explicit object parameter).
     * @param transformer Transformer taking the value and returning a plain value.
     * @return New IO that encapsulates the value after transformation, or detail::TransformError if
     * the transformer is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TTransformTraits = TransformIOTraits<>, class TTransformer>
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

private:
    action_type action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
