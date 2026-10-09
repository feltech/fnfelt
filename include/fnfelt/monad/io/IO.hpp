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

#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/errors.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/traits.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * IO monad for wrapping side-effecting actions.
 *
 * Call `io().sync_wait()` to run an IO; the call operator yields a @ref RunProxy rather than
 * running the action directly.
 *
 * @tparam TAction Callable action. Must take no arguments and return a value, or an async proxy
 * deriving AsyncProxyTag that exposes a `value_type` alias naming its result type.
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
    explicit constexpr IO(action action) : action_{FW(action)} {}

    /**
     * Yield a run proxy holding this IO's action by value.
     *
     * Calling `sync_wait` on the returned proxy is the only execution path. The action is moved out
     * of an rvalue IO and copied out of an lvalue IO.
     *
     * @param self The IO instance (explicit object parameter).
     * @return RunProxy holding the wrapped action by value.
     */
    [[nodiscard]] constexpr RunProxy<action> operator()(this auto && self)
    {
        return RunProxy<action>{FW(self).action_};
    }

    /**
     * Chain a continuation to this IO, producing an IO of the continuation's value.
     *
     * The continuation takes the value this IO produces and returns an IO; the returned IO runs
     * this one, applies the continuation, then runs its IO. Values that are specialisations with
     * all-type template arguments (e.g. `std::pair`, `std::tuple`) are spread into the continuation
     * unless it accepts the value directly.
     *
     * The continuation is taken by value and moved into the free `and_then` as an rvalue: lvalues
     * are copied into the parameter, and the caller's lvalue is left untouched. It must be a
     * complete, non-reference, move constructible class or function pointer type, callable as a
     * const lvalue with the value (directly or spread), and must return an IO. It is validated by
     * the traits template argument's `validate_and_then` (default `IOTraits<>`), which may be
     * overridden by passing custom traits explicitly. The result IO also takes those traits; this
     * IO's own traits are not inherited. An invalid continuation is rejected by a friendly
     * static_assert and yields `detail::AndThenError`.
     *
     * @tparam TAndThenTraits Traits used to validate this IO and continuation pair, and for the
     * result IO, defaulting to IOTraits.
     * @param self The IO instance to chain to (explicit object parameter).
     * @param continuation Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO, or detail::AndThenError if the
     * continuation is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TAndThenTraits = IOTraits<>, class TContinuation>
    [[nodiscard]] constexpr auto and_then(this auto && self, TContinuation continuation)
    {
        return io::and_then<TAndThenTraits>(FW(self), FW(continuation));
    }

    /**
     * Chain a continuation to this IO, naming the result.
     *
     * See the defaulted-traits member overload for the full contract, which is identical: the
     * result IO takes `IOTraits<name_cstr>`, which is also used to validate this IO and the
     * continuation pair.
     *
     * @param self The IO instance to chain to (explicit object parameter).
     * @param continuation Continuation taking the value and returning an IO.
     * @return IO running this IO followed by the continuation's IO with IOTraits<name_cstr>, or
     * detail::AndThenError if the continuation is invalid (which is also rejected by a friendly
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
     * runs this one and produces the transformer's value. Values that are specialisations with
     * all-type template arguments (e.g. `std::pair`, `std::tuple`) are spread into the transformer
     * unless it accepts the value directly. Unlike `and_then`, the transformer's result is not run
     * as an IO.
     *
     * An invalid transformer is rejected by a friendly static_assert and yields
     * `detail::TransformError`.
     *
     * @tparam TTransformTraits Traits used to validate this IO and transformer pair, and for the
     * result IO, defaulting to IOTraits.
     * @param self The IO instance to transform (explicit object parameter).
     * @param transformer Transformer taking the value and returning a plain value.
     * @return New IO that encapsulates the value after transformation, or detail::TransformError if
     * the transformer is invalid (which is also rejected by a friendly static_assert).
     */
    template <class TTransformTraits = IOTraits<>, class TTransformer>
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
    action action_;
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
