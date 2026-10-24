// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/sequence.hpp
 *
 * Validation and state binding for ReaderIO collections.
 */
#pragma once

#include <meta>

#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <immer/array.hpp>
#include <immer/array_transient.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/sequence.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/reader_io/detail/bind.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{
/// Placeholder for a rejected collection.
struct SequenceError
{
};

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
 * Collection action binding every tuple element to the same environment.
 *
 * @tparam TState Environment type.
 * @tparam TReaders Collection type.
 */
template <class TState, class TReaders>
struct TupleSequenceAction
{
    /// Source readers.
    TReaders readers;

    /**
     * Bind the environment and collect the inner IOs.
     *
     * @param self Collection action.
     * @param state Environment shared by value across elements.
     * @return IO collecting all results in input order.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return std::apply(
            [&](auto &&... source)
            { return io::sequence(std::make_tuple(bind_reader<TState>(FW(source), state)...)); },
            FW(self).readers);
    }
};

/**
 * Collection action consuming immutable array elements through copy-on-write storage.
 *
 * @tparam TState Environment type.
 * @tparam TReader Element ReaderIO type.
 * @tparam TMemoryPolicy Immutable storage policy.
 */
template <class TState, class TReader, class TMemoryPolicy>
struct ArraySequenceAction
{
    /// Source readers.
    immer::array<TReader, TMemoryPolicy> readers;

    /**
     * Bind the environment and collect the inner IOs.
     *
     * @param self Collection action.
     * @param state Environment copied into each element.
     * @return IO collecting an immutable array of results.
     */
    auto operator()(this auto && self, TState state)
    {
        auto elements = FW(self).readers.transient();
        auto sources = std::span{elements.data_mut(), elements.size()};
        using bound_io_type = decltype(bind_reader<TState>(std::declval<TReader>(), state));
        std::vector<bound_io_type> bound;
        bound.reserve(sources.size());
        for (auto & source : sources)
        {
            bound.push_back(bind_reader<TState>(std::move(source), state));
        }
        return io::sequence(
            immer::array<bound_io_type, TMemoryPolicy>{
                std::make_move_iterator(bound.begin()), std::make_move_iterator(bound.end())});
    }
};
}  // namespace detail

/**
 * Validation of collections of readers over one environment.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct SequenceReaderIOTraits : detail::CreateReaderIOTraits<name_cstr>
{
    /**
     * Check one collection element.
     *
     * @param reader_meta Element type.
     * @param state_meta Environment type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_element(
        std::meta::info reader_meta, std::meta::info state_meta)
    {
        if (is_reference_type(reader_meta))
        {
            return "element must not be a reference type";
        }
        if (!detail::is_reader_io(reader_meta))
        {
            return "element is not a ReaderIO";
        }
        if (!is_same_type(detail::reader_io_state_meta(reader_meta), state_meta))
        {
            return "element ReaderIO must have the provided state type";
        }
        if (is_void_type(detail::reader_io_value_meta(reader_meta)))
        {
            return "element ReaderIO produces no value";
        }
        return {};
    }

    /**
     * Validate a tuple or immutable array and its common environment.
     *
     * @param readers_meta Collection type.
     * @param state_meta Environment type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_sequence(
        std::meta::info readers_meta, std::meta::info state_meta)
    {
        if (!is_type(state_meta) || is_reference_type(state_meta) || !is_complete_type(state_meta))
        {
            return "state must be a complete non-reference type";
        }
        if (!is_move_constructible_type(state_meta))
        {
            return "state must be move constructible";
        }
        if (io::detail::is_sequence_array(readers_meta))
        {
            if (!is_copy_constructible_type(state_meta))
            {
                return "state must be copy constructible";
            }
            auto const element_meta = template_arguments_of(dealias(readers_meta))[0];
            if (auto const reason = validate_element(element_meta, state_meta); !reason.empty())
            {
                return reason;
            }
            if (!is_copy_constructible_type(element_meta))
            {
                return "array element ReaderIO must be copy constructible";
            }
            auto const bound_meta = detail::bound_reader_io_meta(state_meta, element_meta);
            auto const args = template_arguments_of(dealias(readers_meta));
            return io::detail::SequenceIOTraits<>::validate_sequence(
                substitute(^^immer::array, {bound_meta, args[1]}));
        }
        if (!io::detail::is_sequence_tuple(readers_meta))
        {
            return "provided collection is not a std::tuple or immer::array";
        }
        auto const elements = template_arguments_of(dealias(readers_meta));
        if (!elements.empty() && !is_copy_constructible_type(state_meta))
        {
            return "state must be copy constructible";
        }
        std::vector<std::meta::info> bound_elements;
        for (auto const element_meta : elements)
        {
            if (auto const reason = validate_element(element_meta, state_meta); !reason.empty())
            {
                return reason;
            }
            bound_elements.push_back(detail::bound_reader_io_meta(state_meta, element_meta));
        }
        return io::detail::SequenceIOTraits<>::validate_sequence(
            substitute(^^std::tuple, bound_elements));
    }
};
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
