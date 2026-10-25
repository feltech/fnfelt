// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/get_state.hpp
 *
 * Environment inspection actions and validation.
 */
#pragma once
#include <meta>

#include <string_view>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/// Placeholder for rejected environment inspection.
struct GetStateError
{
};

/**
 * Produce an owned environment value.
 *
 * @tparam TState Environment type.
 */
template <class TState>
struct StateValueAction
{
    /// Owned environment.
    TState state;
    /**
     * Copy or move the stored environment into the result.
     *
     * @param self Stored action.
     * @return Environment value.
     */
    constexpr auto operator()(this auto && self)
    {
        return FW(self).state;
    }
};

/**
 * Lift the environment into its deferred value computation.
 *
 * @tparam TState Environment type.
 */
template <class TState>
struct GetStateAction
{
    /**
     * Retain the environment as the IO's value.
     *
     * @param state Owned environment.
     * @return IO producing the environment.
     */
    constexpr auto operator()(TState state) const
    {
        return io::create(StateValueAction<TState>{std::move(state)});
    }
};

/**
 * Validate environment inspection.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr = default_reader_io_name>
struct GetStateReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Check whether the environment can be owned and returned.
     *
     * @param state_meta Environment type.
     * @return Empty on success, otherwise a reason.
     */
    static consteval std::string_view validate_get_state(std::meta::info state_meta)
    {
        if (!is_type(state_meta) || is_reference_type(state_meta) || !is_complete_type(state_meta))
        {
            return "state must be a complete non-reference type";
        }
        if (!is_move_constructible_type(state_meta))
        {
            return "get_state requires move constructible state";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
