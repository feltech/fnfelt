// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/ReaderIO.hpp
 *
 * ReaderIO monad class and its run proxy.
 */
#pragma once
#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>

#include <fnfelt/monad/reader_io/async.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
// The defaulted traits declarations and the free-function declarations the member templates
// delegate to come from the forward declarations.
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * ReaderIO monad (a monad transformer) wrapping a `state -> IO` action with the state part of the
 * type.
 *
 * The state is a named template parameter (there is no deduced state), so a composition's types
 * carry their environment: `and_then` compositions are typed over the same state end to end, and
 * `value_type` is the value the action produces for that state.
 *
 * Validation runs entirely at compile time in two phases. The factories (`create`, `lift`,
 * `just`, and the compositions) validate the action/state pair and the composition through their
 * traits; the run validates consumption: `build` checks that the action can reach the run proxy
 * (a non-copyable action may only be built from a non-const rvalue) and the proxy checks that it
 * is consumed as an rvalue. The class trusts its arguments, so direct construction skips the
 * composition phase. An invalid run is rejected by a friendly static_assert and yields
 * `detail::ReaderBuildError`/`detail::ReaderRunError`.
 *
 * There is no invocation entry point on the class: bind a state with `build`, then either run the
 * proxy with `sync_wait` (forwarding scheduler arguments for asynchronous inner IOs) or take its
 * inner IO with `io` to compose other machinery around it.
 *
 * @tparam TState The environment the action consumes.
 * @tparam TAction Callable action taking the state and returning an IO.
 * @tparam TTraits Traits used to validate the ReaderIO's composition and consumption.
 */
template <class TState, class TAction, class TTraits>
class ReaderIO
{
public:
    /// Type alias for the environment the action consumes.
    using state_type = TState;
    /// Type alias for the wrapped action.
    using action_type = TAction;
    /// Type alias for the traits associated with the monad.
    using traits = TTraits;
    /// Type alias for the value the action's IO produces for the state (`void` if the action/state
    /// pair is invalid).
    using value_type = [:detail::action_value_meta(^^TAction, ^^TState):];

    /**
     * Constructs a ReaderIO wrapping the given action.
     *
     * The action is perfect-forwarded into the stored member, so wrapping it costs no extra move
     * over storing it directly.
     *
     * Direct construction bypasses the validation the factories (`create`, `lift`, `just`, and
     * the compositions) perform; prefer the factories. The class trusts its arguments.
     *
     * Constrained to exclude the class itself: an unconstrained converting constructor is a
     * better match than the implicit copy constructor for a non-const lvalue ReaderIO, which
     * would shadow copying. Copying and moving a ReaderIO therefore use the copy and move
     * constructors.
     *
     * @tparam TActionArg Deduced action argument type (an rvalue is moved, an lvalue copied).
     * @param action The state-consuming action to wrap.
     */
    template <class TActionArg>
    requires(!detail::is_reader_io(^^TActionArg)) explicit constexpr ReaderIO(TActionArg && action)
        : action_{std::forward<TActionArg>(action)}
    {
    }

    /**
     * Bind the state to this ReaderIO's action, yielding a run proxy.
     *
     * The state is bound by value: an lvalue call copies the state, an rvalue call moves it. The
     * action is forwarded into the proxy with the ReaderIO's value category, so an lvalue or
     * const-bound ReaderIO may only be built from when its action is copy constructible (the
     * proxy consumes the action by rvalue); an invalid combination is rejected by a friendly
     * static_assert and yields `detail::ReaderBuildError`.
     *
     * @tparam TBuildTraits Action-transfer validation traits, defaulting to this monad's traits.
     * @param state The environment to bind.
     * @param self The ReaderIO instance (explicit object parameter).
     * @return The run proxy binding the action to the state; detail::ReaderBuildError if the
     * action cannot reach the proxy as consumed (which is also rejected by a friendly
     * static_assert).
     */
    template <class TBuildTraits = TTraits>
    [[nodiscard]] constexpr auto build(this auto && self, TState state)
    {
        constexpr auto reason = TBuildTraits::validate_build(^^decltype((FW(self).action_)));
        static_assert(reason.empty(), io::detail::build_error_msg(TBuildTraits::name, reason));
        if constexpr (reason.empty())
        {
            return ReaderRunProxy<TState, TAction, TTraits>{FW(self).action_, std::move(state)};
        }
        else
        {
            return detail::ReaderBuildError{};
        }
    }


private:
    action_type action_;
};
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
