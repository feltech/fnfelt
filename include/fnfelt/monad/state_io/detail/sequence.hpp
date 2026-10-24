// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/sequence.hpp
 *
 * Validation and state binding for StateIO collections.
 */
#pragma once

#include <meta>

#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/sequence.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/state_io/ap.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>
#include <fnfelt/monad/state_io/just.hpp>
#include <fnfelt/monad/state_io/transform.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{
/// Placeholder for a rejected collection.
struct SequenceError
{
};

/**
 * Append a whole logical value to collected tuple values.
 *
 * @tparam TValues Collected tuple type.
 */
template <class TValues>
struct AppendValue
{
    /// Previously collected values.
    TValues values;
    /**
     * Append one value without spreading it.
     *
     * @param self Tuple holder.
     * @param value Next logical value.
     * @return Extended tuple.
     */
    constexpr auto operator()(this auto && self, auto && value)
    {
        return std::tuple_cat(
            FW(self).values, std::tuple<std::remove_reference_t<decltype(value)>>{FW(value)});
    }
};

/**
 * Fold sequential application over a tuple prefix.
 *
 * @param sources Owned computations.
 * @return StateIO collecting the prefix values.
 */
template <std::size_t count, class TState, class TSources>
constexpr auto tuple_sequence_fold(TSources & sources)
{
    if constexpr (count == 0)
    {
        return state_io::just<TState>(std::tuple{});
    }
    else
    {
        auto prefix = tuple_sequence_fold<count - 1, TState>(sources);
        auto functions =
            std::move(prefix).transform([](auto values) { return AppendValue{std::move(values)}; });
        return state_io::ap(std::move(functions), std::move(std::get<count - 1>(sources)));
    }
}

/**
 * Format a rejected collection operation.
 *
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Diagnostic string.
 */
consteval std::string sequence_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg{"fnfelt: "};
    msg += name;
    msg += ' ';
    msg += "sequence error: ";
    msg += reason;
    return msg;
}

/**
 * Collect stateful computations sequentially in input order.
 *
 * @tparam TState Environment type.
 * @tparam TStateIOs Collection type.
 */
template <class TState, class TStateIOs>
struct TupleSequenceAction
{
    /// Source computations.
    TStateIOs computations;

    /**
     * Bind the environment and collect the inner IOs.
     *
     * @param self Collection action.
     * @param state Initial state threaded through elements.
     * @return IO collecting all results in input order.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        auto sources = FW(self).computations;
        auto collected = tuple_sequence_fold<std::tuple_size_v<TStateIOs>, TState>(sources);
        return bind_state<TState>(std::move(collected), std::move(state));
    }
};

/**
 * Collection action consuming immutable array elements through copy-on-write storage.
 *
 * @tparam TState Environment type.
 * @tparam TStateIO Element StateIO type.
 * @tparam TMemoryPolicy Immutable storage policy.
 */
template <class TState, class TStateIO, class TMemoryPolicy>
struct ArraySequenceProxy : io::detail::TransportProxyTag
{
    /// Result array with the input memory policy and final state.
    using value_type =
        std::pair<immer::array<typename TStateIO::value_type, TMemoryPolicy>, TState>;
    /// Owned inputs.
    std::tuple<immer::array<TStateIO, TMemoryPolicy>, TState> arg;
    /// Sequential asynchronous driver; later actions materialise only after prior completion.
    static constexpr auto fn = [](auto,
                                  immer::array<TStateIO, TMemoryPolicy> computations,
                                  TState state) -> lf::task<io::detail::task_result_t<value_type>>
    {
        auto elements = std::move(computations).transient();
        auto sources = std::span{elements.data_mut(), elements.size()};
        // Reconstruct each state without requiring move assignment.
        std::optional<TState> current{std::move(state)};
        std::vector<typename TStateIO::value_type> values;
        values.reserve(sources.size());
        for (auto & source : sources)
        {
            auto result = io::detail::unpack_result(
                co_await io::detail::just_child(
                    bind_state<TState>(std::move(source), std::move(*current))));
            values.push_back(std::move(result.first));
            current.emplace(std::move(result.second));
        }
        co_return io::detail::pack_result<value_type>(value_type{
            immer::array<typename TStateIO::value_type, TMemoryPolicy>{
                std::make_move_iterator(values.begin()), std::make_move_iterator(values.end())},
            std::move(*current)});
    };
    /**
     * Retain owned input array and state.
     *
     * @param stored Owned arguments.
     */
    constexpr explicit ArraySequenceProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Execute an owned immutable array sequentially.
 *
 * @tparam TState State type.
 * @tparam TStateIO StateIO element type.
 * @tparam TMemoryPolicy Immutable storage policy.
 */
template <class TState, class TStateIO, class TMemoryPolicy>
struct ArraySequenceIOAction
{
    /// Source computations.
    immer::array<TStateIO, TMemoryPolicy> computations;
    /// Initial state.
    TState state;

    /**
     * Bind the environment and collect the inner IOs.
     *
     * @param self Collection action.
     * @return IO collecting an immutable array of results.
     */
    auto operator()(this auto && self)
    {
        if constexpr (bound_state_is_async<TStateIO>())
        {
            return ArraySequenceProxy<TState, TStateIO, TMemoryPolicy>{
                {FW(self).computations, FW(self).state}};
        }
        else
        {
            auto elements = FW(self).computations.transient();
            auto sources = std::span{elements.data_mut(), elements.size()};
            // Reconstruct each state without requiring move assignment.
            std::optional<TState> current{FW(self).state};
            std::vector<typename TStateIO::value_type> values;
            values.reserve(sources.size());
            for (auto & source : sources)
            {
                auto result =
                    bind_state<TState>(std::move(source), std::move(*current)).build().sync_wait();
                values.push_back(std::move(result.first));
                current.emplace(std::move(result.second));
            }
            return std::pair{
                immer::array<typename TStateIO::value_type, TMemoryPolicy>{
                    std::make_move_iterator(values.begin()), std::make_move_iterator(values.end())},
                std::move(*current)};
        }
    }
};

/// Deduce owned members while preserving their required cv-qualification.
template <class TStateIO, class TMemoryPolicy, class TState>
ArraySequenceIOAction(immer::array<TStateIO, TMemoryPolicy>, TState &&)
    -> ArraySequenceIOAction<std::remove_reference_t<TState>, TStateIO, TMemoryPolicy>;

/**
 * Materialise a lazy immutable-array driver.
 *
 * @tparam TState State type.
 * @tparam TStateIO StateIO element type.
 * @tparam TMemoryPolicy Immutable storage policy.
 */
template <class TState, class TStateIO, class TMemoryPolicy>
struct ArraySequenceAction
{
    /// Owned input array.
    immer::array<TStateIO, TMemoryPolicy> computations;
    /**
     * Bind initial state without executing elements.
     *
     * @param self Collection action.
     * @param state Initial state.
     * @return IO collecting values and final state.
     */
    auto operator()(this auto && self, TState state)
    {
        return io::create(ArraySequenceIOAction{FW(self).computations, std::move(state)});
    }
};

/**
 * Validation of collections of computations over one environment.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct SequenceStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Check one collection element.
     *
     * @param computation_meta Element type.
     * @param state_meta Environment type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_element(
        std::meta::info computation_meta, std::meta::info state_meta)
    {
        if (is_reference_type(computation_meta))
        {
            return "element must not be a reference type";
        }
        if (!detail::is_state_io(computation_meta))
        {
            return "element is not a StateIO";
        }
        if (!is_same_type(detail::state_io_state_meta(computation_meta), state_meta))
        {
            return "element StateIO must have the provided state type";
        }
        if (is_void_type(detail::state_io_value_meta(computation_meta)))
        {
            return "element StateIO produces no value";
        }
        return {};
    }

    /**
     * Validate a tuple or immutable array and its common environment.
     *
     * @param computations_meta Collection type.
     * @param state_meta Environment type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_sequence(
        std::meta::info computations_meta, std::meta::info state_meta)
    {
        if (!is_type(state_meta) || is_reference_type(state_meta) || !is_complete_type(state_meta))
        {
            return "state must be a complete non-reference type";
        }
        if (!is_move_constructible_type(state_meta))
        {
            return "state must be move constructible";
        }
        if (io::detail::is_sequence_array(computations_meta))
        {
            auto const element_meta = template_arguments_of(dealias(computations_meta))[0];
            if (auto const reason = validate_element(element_meta, state_meta); !reason.empty())
            {
                return reason;
            }
            if (!is_copy_constructible_type(element_meta))
            {
                return "array element StateIO must be copy constructible";
            }
            if (is_trivially_copyable_type(element_meta) && !is_copy_assignable_type(element_meta))
            {
                return "trivially copyable array elements must be copy assignable for Immer "
                       "storage";
            }
            return {};
        }
        if (!io::detail::is_sequence_tuple(computations_meta))
        {
            return "provided collection is not a std::tuple or immer::array";
        }
        auto const elements = template_arguments_of(dealias(computations_meta));
        for (auto const element_meta : elements)
        {
            if (auto const reason = validate_element(element_meta, state_meta); !reason.empty())
            {
                return reason;
            }
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
