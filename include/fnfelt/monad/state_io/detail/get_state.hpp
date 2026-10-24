// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/get_state.hpp
 *
 * State inspection actions and validation.
 */
#pragma once
#include <meta>

#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for rejected state inspection.
struct GetStateError
{
};

/**
 * Produce an independent logical snapshot and forward the owned state.
 *
 * @tparam TState Owned state type.
 */
template <class TState>
struct GetStateIOAction
{
    /// Current state.
    TState state;

    /**
     * Copy the logical snapshot before forwarding the original state.
     *
     * @param self Owned state action.
     * @return Snapshot and original state.
     */
    constexpr auto operator()(this auto && self)
    {
        auto value = self.state;
        return std::pair{std::move(value), FW(self).state};
    }
};

/// Preserve state qualification during class template argument deduction.
template <class TState>
GetStateIOAction(TState &&) -> GetStateIOAction<std::remove_reference_t<TState>>;

/**
 * Copy the current state into the logical value.
 *
 * @tparam TState Copyable state type.
 */
template <class TState>
struct GetStateAction
{
    /**
     * Preserve the owned state and produce an independent copy.
     *
     * @param state Current state.
     * @return IO producing a state copy and the original state.
     */
    constexpr auto operator()(TState state) const
    {
        return io::create(GetStateIOAction{std::move(state)});
    }
};

/**
 * Validate state inspection.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr>
struct GetStateStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Check whether the state can be copied into a logical value.
     *
     * @param state_meta State type reflection.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_get_state(std::meta::info state_meta)
    {
        if (!is_type(state_meta) || is_reference_type(state_meta) || !is_complete_type(state_meta))
        {
            return "state must be a complete non-reference type";
        }
        if (!is_copy_constructible_type(state_meta) || !is_move_constructible_type(state_meta))
        {
            return "get_state requires copy constructible and move constructible state";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
