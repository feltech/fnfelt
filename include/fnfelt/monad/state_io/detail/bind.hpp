// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/bind.hpp
 *
 * Bind states to owned environments for execution inside IO compositions.
 */
#pragma once

#include <meta>

#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/state_io/StateIO.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/**
 * Whether a state's inner IO requires the scheduler.
 *
 * @tparam TStateIO Validated StateIO type.
 * @return True for asynchronous states.
 */
template <class TStateIO>
consteval bool bound_state_is_async()
{
    return io::detail::action_is_async(io::detail::io_action_meta(state_io_io_meta(^^TStateIO)));
}

/**
 * Owned asynchronous state invocation inside the awaiting IO tree.
 *
 * @tparam TState Environment type.
 * @tparam TStateIO StateIO type.
 */
template <class TState, class TStateIO>
struct BoundStateProxy : io::detail::TransportProxyTag
{
    /// Produced value.
    using value_type = std::pair<typename TStateIO::value_type, TState>;
    /// Owned state and environment.
    std::tuple<TStateIO, TState> arg;

    /// Materialise the state inside its child task and retain the bound proxy until completion.
    static constexpr auto fn =
        [](auto, TStateIO source, TState state) -> lf::task<io::detail::task_result_t<value_type>>
    {
        // Materialisation may throw: keep it inside the task so IO's fork/call slot owns the
        // exception and joins outstanding siblings before propagating it.
        auto bound = std::move(source).build(std::move(state));
        auto inner = std::move(bound).io();
        co_return co_await io::detail::just_child(std::move(inner));
    };

    /// Bind and execute the state computation inline during constant evaluation.
    static constexpr auto run_inline = [](TStateIO source, TState state) -> value_type
    {
        auto bound = std::move(source).build(std::move(state));
        return std::move(bound).io().build().sync_wait();
    };

    /**
     * Retain the state and its environment.
     *
     * @param stored Owned task arguments.
     */
    constexpr explicit BoundStateProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * IO action owning a state and its environment until execution.
 *
 * @tparam TState Environment type.
 * @tparam TStateIO StateIO type.
 */
template <class TState, class TStateIO>
struct BoundStateAction
{
    /// Plain result or owning asynchronous proxy.
    using result_type = std::conditional_t<
        bound_state_is_async<TStateIO>(),
        BoundStateProxy<TState, TStateIO>,
        std::pair<typename TStateIO::value_type, TState>>;
    /// Source state.
    TStateIO source;
    /// Owned environment.
    TState state;

    /**
     * Invoke the state inline or transfer ownership to an asynchronous child.
     *
     * @param self Bound action to consume.
     * @return Plain value or asynchronous proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (bound_state_is_async<TStateIO>())
        {
            return result_type{{FW(self).source, FW(self).state}};
        }
        else
        {
            auto bound = FW(self).source.build(FW(self).state);
            return std::move(bound).io().build().sync_wait();
        }
    }
};

/// Deduce owned members while preserving their required cv-qualification.
template <class TStateIO, class TState>
BoundStateAction(TStateIO &&, TState &&)
    -> BoundStateAction<std::remove_reference_t<TState>, std::remove_cvref_t<TStateIO>>;

/**
 * Move owned state into a lazy IO retaining the computation's value category.
 *
 * @tparam TState Environment type, preserving its cv-qualification.
 * @tparam TStateIO Deduced StateIO type.
 * @param source State to bind.
 * @param state State consumed by the computation.
 * @return IO invoking the state when executed.
 */
template <class TState, class TStateIO>
constexpr auto bind_state(TStateIO && source, TState state)
{
    return io::create(BoundStateAction{FW(source), std::move(state)});
}

/**
 * Reflect the IO wrapping a validated state and environment.
 *
 * @param state_meta Complete environment type.
 * @param computation_meta Validated StateIO type.
 * @return Bound IO type reflection.
 */
consteval std::meta::info bound_state_io_meta(
    std::meta::info state_meta, std::meta::info computation_meta)
{
    auto const action_meta = substitute(^^BoundStateAction, {state_meta, computation_meta});
    return substitute(^^io::IO, {action_meta, ^^io::detail::CreateIOTraits<>});
}
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
