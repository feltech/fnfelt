// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/store.hpp
 *
 * Logical-value-preserving state replacement.
 */
#pragma once
#include <meta>

#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for rejected state replacement.
struct StoreError
{
};

/**
 * Select direct or spread mutator arguments, with owned state last.
 *
 * @param mutator_meta Mutator type.
 * @param value_meta Const logical-value reference type.
 * @param state_meta Owned state type.
 * @return Selected argument types; an empty vector means no matching signature.
 */
consteval std::vector<std::meta::info> store_argument_metas(
    std::meta::info mutator_meta, std::meta::info value_meta, std::meta::info state_meta)
{
    if (is_invocable_type(mutator_meta, {value_meta, state_meta}))
    {
        return {value_meta, state_meta};
    }
    auto const bare_value = dealias(remove_cvref(value_meta));
    if (!has_template_arguments(bare_value) || is_volatile_type(remove_reference(value_meta)))
    {
        return {};
    }
    auto const value_template = template_of(bare_value);
    if (value_template != ^^std::pair && value_template != ^^std::tuple)
    {
        return {};
    }
    auto arguments = fnfelt::detail::spread_argument_metas(value_meta);
    arguments.push_back(state_meta);
    if (is_invocable_type(mutator_meta, arguments))
    {
        return arguments;
    }
    return {};
}

/**
 * Invoke a mutator directly or with spread elements and owned state last.
 *
 * @param mutator State replacement function.
 * @param value Preserved logical value.
 * @param state Owned state.
 * @return Replacement state.
 */
template <class TMutator, class TValue, class TState>
constexpr decltype(auto) invoke_store(TMutator && mutator, TValue && value, TState && state)
{
    if constexpr (std::is_invocable_v<TMutator, TValue &&, TState &&>)
    {
        return std::invoke(FW(mutator), FW(value), FW(state));
    }
    else
    {
        return std::apply(
            [&](auto &&... elements)
            { return std::invoke(FW(mutator), FW(elements)..., FW(state)); },
            FW(value));
    }
}

/**
 * Replace state while preserving the logical value.
 *
 * @tparam TMutator State replacement function.
 */
template <class TMutator>
struct StoreValue
{
    /// State replacement function.
    TMutator mutator;
    /**
     * Inspect the logical value and consume the updated state.
     *
     * @param self Mutator holder.
     * @param result Source logical value and updated state.
     * @return Original logical value and replacement state.
     */
    constexpr auto operator()(this auto && self, auto && result)
    {
        auto state = invoke_store(FW(self).mutator, std::as_const(result.first), FW(result).second);
        return std::pair<decltype(result.first), decltype(result.second)>{
            FW(result).first, std::move(state)};
    }
};

/**
 * Bind state and map the source pair through a state replacement function.
 *
 * @tparam TSource Source computation.
 * @tparam TMutator State replacement function.
 */
template <class TSource, class TMutator>
struct StoreAction
{
    /// Source computation.
    TSource source;
    /// State replacement function.
    TMutator mutator;
    /**
     * Build a lazy value-preserving state replacement.
     *
     * @param self Stored action.
     * @param state Initial state.
     * @return IO producing preserved value and replacement state.
     */
    constexpr auto operator()(this auto && self, typename TSource::state_type state)
    {
        return bind_state(FW(self).source, std::move(state))
            .transform(StoreValue{FW(self).mutator});
    }
};

/**
 * Validate value-preserving state replacement.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr>
struct StoreStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Check the mutator's exact logical-value and state signature.
     *
     * @param source_meta Source StateIO reflection.
     * @param mutator_meta State replacement function reflection.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_store(
        std::meta::info source_meta, std::meta::info mutator_meta)
    {
        if (!is_state_io(source_meta))
        {
            return "source must be a StateIO";
        }
        if (fnfelt::detail::callable_shape_of(mutator_meta) != fnfelt::detail::CallableShape::valid)
        {
            return "mutator must be a complete movable class or function pointer";
        }
        auto const value_meta = state_io_value_meta(source_meta);
        auto const state_meta = state_io_state_meta(source_meta);
        if (is_void_type(value_meta))
        {
            return "source StateIO produces no value";
        }
        auto const const_value = substitute(^^std::add_const_t, {value_meta});
        auto const arguments =
            store_argument_metas(mutator_meta, add_lvalue_reference(const_value), state_meta);
        if (arguments.empty())
        {
            return "mutator must accept the const logical value or its spread elements, followed "
                   "by owned state";
        }
        auto const result_meta = invoke_result(mutator_meta, arguments);
        if (is_reference_type(result_meta) || !is_same_type(result_meta, state_meta))
        {
            return "mutator must return the same state type by value";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
