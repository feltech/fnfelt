// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/async.hpp
 *
 * Run proxies and asynchronous leaves for the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/reader_io/ReaderIO.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/async.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Run proxy binding a ReaderIO's action to a state for a single materialisation.
 *
 * Returned by `ReaderIO::build`; its members are consumed by rvalue, so an lvalue or const-bound
 * proxy is rejected by a friendly static_assert and yields `detail::ReaderRunError`.
 *
 * `io` materialises the composition by invoking the action with the state, producing the inner IO
 * to run or compose. The action's invocation consumes the bound state and the proxy's action by
 * move, so `io` must be consumed exactly once per proxy; the proxy itself holds no machinery, so
 * a discarded `io` (for example when only validating a composition) is fine.
 *
 * `sync_wait` is the execution path: it delegates to the inner IO's run proxy, forwarding
 * scheduler arguments, so synchronous and asynchronous inner IOs run alike.
 *
 * @tparam TState The bound state type.
 * @tparam TAction The ReaderIO's action type.
 * @tparam TTraits The ReaderIO's traits.
 */
template <class TState, class TAction, class TTraits>
struct ReaderRunProxy
{
    /// The ReaderIO's stored action.
    TAction action;
    /// The state the action is invoked with.
    TState state;

    /**
     * Materialise the composition: invoke the action with the state, producing the inner IO.
     *
     * Each proxy binds the state once and consumes its members by move, so this must be consumed
     * exactly once per proxy.
     *
     * @param self The run proxy to consume (explicit object parameter).
     * @return The action's inner IO for the bound state; `detail::ReaderRunError` if the proxy is
     * not consumed as an rvalue (which is also rejected by a friendly static_assert).
     */
    [[nodiscard]] constexpr auto io(this auto && self)
    {
        // `decltype(self)` is the declared explicit-object parameter type: an rvalue invocation
        // yields `ReaderRunProxy &&` (or const-qualified if the proxy was bound const).
        constexpr auto reason = TTraits::validate_sync_wait(^^decltype(self));
        static_assert(reason.empty(), detail::run_proxy_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            return std::invoke(FW(self).action, std::move(FW(self).state));
        }
        else
        {
            return detail::ReaderRunError{};
        }
    }

    /**
     * Materialise the composition and run the inner IO, forwarding scheduler arguments.
     *
     * Delegates to the inner IO's run proxy, so the run validates the inner IO's scheduler
     * arguments (e.g. a libfork scheduler for asynchronous actions).
     *
     * @param self The run proxy to consume (explicit object parameter).
     * @param scheduler Scheduler arguments forwarded to the inner IO's `sync_wait`.
     * @return The inner IO's value for the bound state; `detail::ReaderRunError` if the proxy is
     * not consumed as an rvalue (which is also rejected by a friendly static_assert).
     */
    [[nodiscard]] constexpr auto sync_wait(this auto && self, auto &&... scheduler)
    {
        constexpr auto reason = TTraits::validate_sync_wait(^^decltype(self));
        static_assert(reason.empty(), detail::run_proxy_error_msg(TTraits::name, reason));
        if constexpr (reason.empty())
        {
            return FW(self).io().build().sync_wait(FW(scheduler)...);
        }
        else
        {
            return detail::ReaderRunError{};
        }
    }
};

/**
 * Lifts a state-first asynchronous leaf into the ReaderIO monad with the given traits (default
 * detail::CreateAsyncReaderIOTraits).
 *
 * The coroutine function's signature is `(auto, TState, TArgs...) -> lf::task` - libfork's
 * generated self argument, then the ReaderIO's bound state as the first caller argument, then the
 * stored arguments. The returned ReaderIO yields, per invocation, an asynchronous leaf IO whose
 * coroutine receives the bound state and the stored arguments; the leaf is validated like io's
 * own (stateless function, constant-expression entry, `lf::task` result) on the validation tuple
 * `(TState, TArgs...)`, and the state/type pair is validated by the inherited action validation.
 *
 * The function and arguments are decayed and perfect-forwarded on input: lvalues are copied,
 * rvalues moved. An invalid combination is rejected by a friendly static_assert and yields
 * `detail::CreateAsyncError`.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TTraits Traits used to validate the coroutine/argument combination and for the result
 * ReaderIO, defaulting to detail::CreateAsyncReaderIOTraits.
 * @tparam TFn Deduced stateless coroutine function type.
 * @tparam TArgs Deduced stored argument types.
 * @param fn The stateless coroutine function.
 * @param args Arguments passed to the coroutine after the state (moved/copied per value
 * category).
 * @return ReaderIO yielding the async leaf IO with TTraits, or detail::CreateAsyncError if the
 * combination is invalid (which is also rejected by a friendly static_assert).
 */
template <
    class TState,
    class TTraits = detail::CreateAsyncReaderIOTraits<>,
    class TFn,
    class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    using fn_type = std::decay_t<TFn>;
    using arg_tuple_type = std::tuple<TState, std::decay_t<TArgs>...>;
    static constexpr auto reason = TTraits::validate_create_async(^^fn_type, ^^arg_tuple_type);
    static_assert(reason.empty(), detail::create_async_error_msg<^^fn_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::CreateAsyncAction<TState, fn_type, std::decay_t<TArgs>...>;
        // Qualified so the stored argument types' namespaces cannot also find a foreign create of
        // the same shape and bind the call ambiguously.
        return reader_io::create<TState, TTraits>(
            action_type{FW(fn), std::tuple<std::decay_t<TArgs>...>{FW(args)...}});
    }
    else
    {
        return detail::CreateAsyncError{};
    }
}

/**
 * Lifts a state-first asynchronous leaf into the ReaderIO monad, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the function and arguments. Overloads on the first template
 * argument: a name (`char const *` non-type parameter) or a traits type.
 *
 * The result ReaderIO takes detail::CreateAsyncReaderIOTraits<name_cstr>, which is also used to
 * validate the combination.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ReaderIO"_ss`).
 * @tparam TState The ReaderIO's state type.
 * @tparam TFn Deduced stateless coroutine function type.
 * @tparam TArgs Deduced stored argument types.
 * @param fn The stateless coroutine function.
 * @param args Arguments passed to the coroutine after the state (moved/copied per value
 * category).
 * @return ReaderIO yielding the async leaf IO with detail::CreateAsyncReaderIOTraits<name_cstr>, or
 * detail::CreateAsyncError if the combination is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TState, class TFn, class... TArgs>
[[nodiscard]] constexpr auto create_async(TFn fn, TArgs &&... args)
{
    return reader_io::create_async<TState, detail::CreateAsyncReaderIOTraits<name_cstr>>(
        std::move(fn), FW(args)...);
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
