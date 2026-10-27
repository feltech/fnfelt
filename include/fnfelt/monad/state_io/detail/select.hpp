// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/select.hpp
 *
 * Conditional effects with sequential state threading.
 */
#pragma once

#include <meta>

#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/select.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for a rejected selective composition.
struct SelectError
{
};

/**
 * Whether either declared state computation requires asynchronous execution.
 *
 * @tparam TChoice Choice StateIO type.
 * @tparam TFallback Fallback StateIO type.
 * @return True if either computation is asynchronous.
 */
template <class TChoice, class TFallback>
consteval bool select_is_async()
{
    return bound_state_is_async<TChoice>() || bound_state_is_async<TFallback>();
}

/**
 * Sequential selective execution under the enclosing scheduler.
 *
 * @tparam TState Owned state type.
 * @tparam TChoice Choice StateIO type.
 * @tparam TFallback Fallback StateIO type.
 */
template <class TState, class TChoice, class TFallback>
struct SelectProxy : io::detail::TransportProxyTag
{
    /// Completed result and final state.
    using value_type = std::pair<typename TChoice::value_type::value_type, TState>;
    /// Declared computations and initial state.
    std::tuple<TChoice, TFallback, TState> arg;
    /// Bind the fallback only when the choice needs its function.
    static constexpr auto fn = [](auto,
                                  TChoice choice_source,
                                  TFallback fallback_source,
                                  TState state) -> lf::task<io::detail::task_result_t<value_type>>
    {
        auto choice = io::detail::unpack_result(
            co_await io::detail::just_child(
                bind_state<TState>(std::move(choice_source), std::move(state))));
        if (choice.first.has_value())
        {
            co_return io::detail::pack_result<value_type>(
                value_type{*std::move(choice.first), std::move(choice.second)});
        }
        auto fallback = io::detail::unpack_result(
            co_await io::detail::just_child(
                bind_state<TState>(std::move(fallback_source), std::move(choice.second))));
        co_return io::detail::pack_result<value_type>(value_type{
            fnfelt::detail::invoke_dispatch(
                std::move(fallback.first), std::move(choice.first).error()),
            std::move(fallback.second)});
    };

    /// Thread state through only the required effects during constant evaluation.
    static constexpr auto run_inline =
        [](TChoice choice_source, TFallback fallback_source, TState state) -> value_type
    {
        auto choice =
            bind_state<TState>(std::move(choice_source), std::move(state)).build().sync_wait();
        if (choice.first.has_value())
        {
            return {*std::move(choice.first), std::move(choice.second)};
        }
        auto fallback = bind_state<TState>(std::move(fallback_source), std::move(choice.second))
                            .build()
                            .sync_wait();
        return {
            fnfelt::detail::invoke_dispatch(
                std::move(fallback.first), std::move(choice.first).error()),
            std::move(fallback.second)};
    };

    /**
     * Retain both computations and their state until execution.
     *
     * @param stored Owned task arguments.
     */
    constexpr explicit SelectProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Execute a selective state computation inside its inner IO.
 *
 * @tparam TState Owned state type.
 * @tparam TChoice Choice StateIO type.
 * @tparam TFallback Fallback StateIO type.
 */
template <class TState, class TChoice, class TFallback>
struct SelectIOAction
{
    /// Completed result and final state, or an asynchronous proxy.
    using result_type = std::conditional_t<
        select_is_async<TChoice, TFallback>(),
        SelectProxy<TState, TChoice, TFallback>,
        std::pair<typename TChoice::value_type::value_type, TState>>;
    /// Choice computation.
    TChoice choice;
    /// Conditional fallback computation.
    TFallback fallback;
    /// Initial state retained until execution.
    TState state;

    /**
     * Preserve the choice's state or pass it into the required fallback.
     *
     * @param self Owned selective invocation.
     * @return Completed result and state, or an asynchronous proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (select_is_async<TChoice, TFallback>())
        {
            return result_type{{FW(self).choice, FW(self).fallback, FW(self).state}};
        }
        else
        {
            auto choice = bind_state<TState>(FW(self).choice, FW(self).state).build().sync_wait();
            if (choice.first.has_value())
            {
                return {*std::move(choice.first), std::move(choice.second)};
            }
            auto fallback =
                bind_state<TState>(FW(self).fallback, std::move(choice.second)).build().sync_wait();
            return {
                fnfelt::detail::invoke_dispatch(
                    std::move(fallback.first), std::move(choice.first).error()),
                std::move(fallback.second)};
        }
    }
};

/**
 * Bind initial state to the declared selective computations.
 *
 * @tparam TState Owned state type.
 * @tparam TChoice Choice StateIO type.
 * @tparam TFallback Fallback StateIO type.
 */
template <class TState, class TChoice, class TFallback>
struct SelectAction
{
    /// Choice computation.
    TChoice choice;
    /// Conditional fallback computation.
    TFallback fallback;

    /**
     * Defer both state computations until the inner IO executes.
     *
     * @param self Declared effects.
     * @param state Initial state.
     * @return Selective IO producing the completed result and final state.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return io::create(
            SelectIOAction<TState, TChoice, TFallback>{
                FW(self).choice, FW(self).fallback, std::move(state)});
    }
};

/**
 * Validate selective state computations and their state agreement.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct SelectStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Check matching states and the expected/function contract.
     *
     * @param choice_meta Choice StateIO reflection.
     * @param fallback_meta Fallback StateIO reflection.
     * @return Empty on success, otherwise a diagnostic reason.
     */
    static consteval std::string_view validate_select(
        std::meta::info choice_meta, std::meta::info fallback_meta)
    {
        if (!is_state_io(choice_meta) || !is_state_io(fallback_meta))
        {
            return "choice and fallback sources must be StateIOs";
        }
        if (!is_same_type(state_io_state_meta(choice_meta), state_io_state_meta(fallback_meta)))
        {
            return "choice and fallback sources must have the same state type";
        }
        auto const choice_value_meta = state_io_value_meta(choice_meta);
        auto const reason = io::detail::validate_select_values(
            choice_value_meta, state_io_value_meta(fallback_meta));
        if (!reason.empty())
        {
            return reason;
        }
        auto const value_meta = template_arguments_of(dealias(remove_cvref(choice_value_meta)))[0];
        if (io::detail::is_io(value_meta) || is_state_io(value_meta))
        {
            return "choice value must not be an IO or StateIO";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
