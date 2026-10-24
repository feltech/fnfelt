// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/traverse.hpp
 *
 * Validation for tuple traversal through StateIO continuations.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <vector>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/detail/traverse.hpp>
#include <fnfelt/monad/state_io/detail/sequence.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{
/// Placeholder for a rejected traversal.
struct TraverseError
{
};

/**
 * Format a rejected traversal.
 *
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Diagnostic string.
 */
consteval std::string traverse_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg{"fnfelt: "};
    msg += name;
    msg += " traverse error: ";
    msg += reason;
    return msg;
}

/**
 * Validation of tuple values mapped to readers of one environment.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct TraverseStateIOTraits : SequenceStateIOTraits<name_cstr>
{
    /**
     * Validate a tuple and its StateIO-producing continuation.
     *
     * @param tuple_meta Tuple type.
     * @param continuation_meta Continuation type, invoked as an lvalue.
     * @param state_meta Common environment type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_traverse(
        std::meta::info tuple_meta, std::meta::info continuation_meta, std::meta::info state_meta)
    {
        if (auto const reason =
                SequenceStateIOTraits<name_cstr>::validate_sequence(^^std::tuple<>, state_meta);
            !reason.empty())
        {
            return reason;
        }
        if (!io::detail::is_sequence_tuple(tuple_meta))
        {
            return "provided values are not a std::tuple";
        }
        if (!io::detail::is_continuation_shape_valid(continuation_meta))
        {
            return "continuation must be a complete non-reference class or function pointer";
        }
        std::vector<std::meta::info> mapped;
        for (auto const element_meta : template_arguments_of(dealias(tuple_meta)))
        {
            auto const receiver_meta = add_lvalue_reference(continuation_meta);
            if (!fnfelt::detail::is_dispatch_invocable(receiver_meta, element_meta))
            {
                return "continuation does not accept the tuple element's value";
            }
            auto const result_meta =
                fnfelt::detail::dispatch_result_meta(receiver_meta, element_meta);
            if (auto const reason = SequenceStateIOTraits<name_cstr>::validate_element(
                    remove_cvref(result_meta), state_meta);
                !reason.empty())
            {
                return reason;
            }
            mapped.push_back(remove_cvref(result_meta));
        }
        return SequenceStateIOTraits<name_cstr>::validate_sequence(
            substitute(^^std::tuple, mapped), state_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::state_io
