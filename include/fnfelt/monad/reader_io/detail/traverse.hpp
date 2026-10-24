// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/traverse.hpp
 *
 * Validation for collection traversal through ReaderIO continuations.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <vector>

#include <immer/array.hpp>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/detail/traverse.hpp>
#include <fnfelt/monad/reader_io/detail/sequence.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

namespace fnfelt::monad::reader_io
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
 * Validation of collection values mapped to readers of one environment.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct TraverseReaderIOTraits : SequenceReaderIOTraits<name_cstr>
{
    /**
     * Validate a collection and its ReaderIO-producing continuation.
     *
     * @param tuple_meta Tuple or immutable array type.
     * @param continuation_meta Continuation type, invoked as an lvalue.
     * @param state_meta Common environment type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_traverse(
        std::meta::info tuple_meta, std::meta::info continuation_meta, std::meta::info state_meta)
    {
        if (auto const reason =
                SequenceReaderIOTraits<name_cstr>::validate_sequence(^^std::tuple<>, state_meta);
            !reason.empty())
        {
            return reason;
        }
        if (!io::detail::is_sequence_tuple(tuple_meta) &&
            !io::detail::is_sequence_array(tuple_meta))
        {
            return "provided collection is not a std::tuple or immer::array";
        }
        if (!io::detail::is_continuation_shape_valid(continuation_meta))
        {
            return "continuation must be a complete non-reference class or function pointer";
        }
        std::vector<std::meta::info> mapped;
        auto elements = template_arguments_of(dealias(remove_cvref(tuple_meta)));
        bool const is_array = io::detail::is_sequence_array(tuple_meta);
        auto const policy_meta = is_array ? elements[1] : ^^void;
        if (is_array)
        {
            if (auto const reason = io::detail::validate_traverse_array(tuple_meta);
                !reason.empty())
            {
                return reason;
            }
            elements.resize(1);
        }
        for (auto const element_meta : elements)
        {
            auto const receiver_meta = add_lvalue_reference(continuation_meta);
            if (!fnfelt::detail::is_dispatch_invocable(receiver_meta, element_meta))
            {
                return "continuation does not accept the tuple element's value";
            }
            auto const result_meta =
                fnfelt::detail::dispatch_result_meta(receiver_meta, element_meta);
            if (auto const reason = SequenceReaderIOTraits<name_cstr>::validate_element(
                    remove_cvref(result_meta), state_meta);
                !reason.empty())
            {
                return reason;
            }
            mapped.push_back(remove_cvref(result_meta));
        }
        if (is_array)
        {
            auto const mapped_meta = substitute(^^immer::array, {mapped[0], policy_meta});
            if (auto const reason = io::detail::validate_traverse_array(mapped_meta);
                !reason.empty())
            {
                return reason;
            }
            return SequenceReaderIOTraits<name_cstr>::validate_sequence(mapped_meta, state_meta);
        }
        return SequenceReaderIOTraits<name_cstr>::validate_sequence(
            substitute(^^std::tuple, mapped), state_meta);
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io
