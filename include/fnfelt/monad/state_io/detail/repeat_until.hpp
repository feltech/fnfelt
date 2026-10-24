// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/repeat_until.hpp
 *
 * Validation and state threading for StateIO loops.
 */
#pragma once

#include <meta>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/detail/repeat_until.hpp>
#include <fnfelt/monad/io/repeat_until.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{
/// Placeholder for a rejected loop.
struct RepeatUntilError
{
};

/**
 * Model the logical value for existing IO loop validation without constructing a computation.
 *
 * @tparam TValue Logical source value.
 */
template <class TValue>
struct LogicalValueAction
{
    /**
     * Validation-only invocation signature.
     *
     * @return Logical source value.
     */
    constexpr TValue operator()() const;
};

/**
 * Own and execute an asynchronous state-threading loop.
 *
 * @tparam TSource Reusable source computation.
 * @tparam TAccumulator Independent loop accumulator.
 * @tparam TStepper Expected-producing transition.
 */
template <class TSource, class TAccumulator, class TStepper>
struct RepeatUntilProxy : io::detail::TransportProxyTag
{
    /// Threaded state type.
    using state_type = typename TSource::state_type;
    /// Stepper expected type.
    using expected_type =
        std::invoke_result_t<TStepper &, TAccumulator, typename TSource::value_type>;
    /// Successful logical value and final state.
    using value_type = std::pair<typename expected_type::value_type, state_type>;
    /// Owned loop arguments.
    std::tuple<TSource, TAccumulator, TStepper, state_type> arg;
    /// Sequential driver retaining one scheduler tree.
    static constexpr auto fn =
        [](auto, TSource source, TAccumulator initial, TStepper stepper, state_type state)
        -> lf::task<io::detail::task_result_t<value_type>>
    {
        // Reconstruct loop values without requiring move assignment.
        std::optional<TAccumulator> accumulator{std::move(initial)};
        std::optional<state_type> current{std::move(state)};
        while (true)
        {
            auto produced = io::detail::unpack_result(
                co_await io::detail::just_child(
                    bind_state<state_type>(source, std::move(*current))));
            auto result = std::invoke(stepper, std::move(*accumulator), std::move(produced.first));
            if (result.has_value())
            {
                co_return io::detail::pack_result<value_type>(
                    value_type{*std::move(result), std::move(produced.second)});
            }
            accumulator.emplace(std::move(result).error());
            current.emplace(std::move(produced.second));
        }
    };

    /// Run sequential iterations inline during constant evaluation.
    static constexpr auto run_inline =
        [](TSource source, TAccumulator initial, TStepper stepper, state_type state) -> value_type
    {
        std::optional<TAccumulator> accumulator{std::move(initial)};
        std::optional<state_type> current{std::move(state)};
        while (true)
        {
            auto produced = bind_state<state_type>(source, std::move(*current)).build().sync_wait();
            auto result = std::invoke(stepper, std::move(*accumulator), std::move(produced.first));
            if (result.has_value())
            {
                return {*std::move(result), std::move(produced.second)};
            }
            accumulator.emplace(std::move(result).error());
            current.emplace(std::move(produced.second));
        }
    };
    /**
     * Retain loop inputs for scheduler execution.
     *
     * @param stored Owned arguments.
     */
    constexpr explicit RepeatUntilProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Execute a state loop inline or yield its asynchronous driver.
 *
 * @tparam TSource Reusable source computation.
 * @tparam TAccumulator Loop accumulator.
 * @tparam TStepper Expected-producing transition.
 */
template <class TSource, class TAccumulator, class TStepper>
struct RepeatUntilIOAction
{
    /// Reusable source computation.
    TSource source;
    /// Initial loop accumulator.
    TAccumulator initial;
    /// Lvalue-invoked transition function.
    TStepper stepper;
    /// Initial state.
    TSource::state_type state;
    /**
     * Run sequential iterations until the first successful expected value.
     *
     * @param self Owned loop inputs.
     * @return Successful value/state pair or async proxy.
     */
    constexpr auto operator()(this auto && self)
    {
        using proxy_type = RepeatUntilProxy<TSource, TAccumulator, TStepper>;
        if constexpr (bound_state_is_async<TSource>())
        {
            return proxy_type{
                {FW(self).source, FW(self).initial, FW(self).stepper, FW(self).state}};
        }
        else
        {
            // Reconstruct loop values without requiring move assignment.
            std::optional accumulator{FW(self).initial};
            std::optional current{FW(self).state};
            while (true)
            {
                auto produced = bind_state(self.source, std::move(*current)).build().sync_wait();
                auto result =
                    std::invoke(self.stepper, std::move(*accumulator), std::move(produced.first));
                if (result.has_value())
                {
                    return typename proxy_type::value_type{
                        *std::move(result), std::move(produced.second)};
                }
                accumulator.emplace(std::move(result).error());
                current.emplace(std::move(produced.second));
            }
        }
    }
};

/**
 * Format a rejected loop.
 *
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Diagnostic string.
 */
consteval std::string repeat_until_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg{"fnfelt: "};
    msg += name;
    msg += " repeat_until error: ";
    msg += reason;
    return msg;
}

/**
 * Bind initial state to a loop with an independent accumulator.
 *
 * @tparam TSource Source StateIO type.
 * @tparam TAccumulator Loop accumulator type.
 * @tparam TStepper Loop stepper type.
 */
template <class TSource, class TAccumulator, class TStepper>
struct RepeatUntilAction
{
    /// Reusable source computation.
    TSource source;
    /// First accumulator value.
    TAccumulator initial;
    /// Accumulator transition function.
    TStepper stepper;

    /**
     * Materialise a loop threading updated state through source invocations.
     *
     * @param self Loop action to consume.
     * @param state Initial state.
     * @return IO running the loop under its enclosing scheduler.
     */
    constexpr auto operator()(this auto && self, typename TSource::state_type state)
    {
        return io::create(
            RepeatUntilIOAction{
                FW(self).source, FW(self).initial, FW(self).stepper, std::move(state)});
    }
};

/**
 * Validate StateIO loops with an accumulator separate from threaded state.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct RepeatUntilStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Validate the reusable source, accumulator and stepper.
     *
     * @param source_meta Source StateIO type.
     * @param accumulator_meta Decayed accumulator type.
     * @param stepper_meta Decayed stepper type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_repeat_until(
        std::meta::info source_meta, std::meta::info accumulator_meta, std::meta::info stepper_meta)
    {
        if (!detail::is_state_io(source_meta))
        {
            return "source is not a StateIO";
        }
        if (!is_copy_constructible_type(source_meta))
        {
            return "source StateIO must be copy constructible";
        }
        auto const logical_meta = state_io_value_meta(source_meta);
        auto const logical_action = substitute(^^LogicalValueAction, {logical_meta});
        auto const logical_io =
            substitute(^^io::IO, {logical_action, ^^io::detail::CreateIOTraits<>});
        return io::detail::RepeatUntilIOTraits<>::validate_repeat_until(
            logical_io, accumulator_meta, stepper_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
