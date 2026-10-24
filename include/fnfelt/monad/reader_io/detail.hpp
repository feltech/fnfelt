// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail.hpp
 *
 * Generic implementation details for the ReaderIO monad.
 *
 * Reflection helper predicates shared by the ReaderIO monad's operations.
 */
#pragma once

#include <meta>

#include <fnfelt/monad/reader_io/fwd.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{
/**
 * Check whether a reflection is of a (cv-ref-stripped) specialisation of ReaderIO, with any state
 * type, action and traits.
 *
 * @param type_meta Reflection of the type to check.
 * @return True if the type is a ReaderIO specialisation.
 */
consteval bool is_reader_io(std::meta::info type_meta)
{
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^ReaderIO;
}

/**
 * Reflection of the state type a ReaderIO is typed over.
 *
 * @param reader_io_meta Reflection of the (possibly ReaderIO) type.
 * @return Reflection of the state type, or `void` if the type is not a ReaderIO. The `void` is
 * the diagnostic placeholder: it keeps an invalid instantiation well-formed while the companion
 * validators remain the single, friendly error.
 */
consteval std::meta::info reader_io_state_meta(std::meta::info reader_io_meta)
{
    // `template_arguments_of` throws for non-template types, and a consteval throw is a hard
    // compile error, so `void` is returned as a placeholder for non-ReaderIO types.
    if (!is_reader_io(reader_io_meta))
    {
        return ^^void;
    }
    // Extract the scalar inside the consteval function: vector<info> results allocate and cannot
    // escape a constant-evaluated context.
    return template_arguments_of(dealias(remove_cvref(reader_io_meta)))[0];
}

/**
 * Reflection of the action type wrapped by a ReaderIO.
 *
 * The action is the ReaderIO's second template argument (the first is the state type).
 *
 * @param reader_io_meta Reflection of the (possibly ReaderIO) type.
 * @return Reflection of the wrapped action, or `void` if the type is not a ReaderIO, with the
 * same diagnostic placeholder semantics as `reader_io_state_meta`.
 */
consteval std::meta::info reader_io_action_meta(std::meta::info reader_io_meta)
{
    if (!is_reader_io(reader_io_meta))
    {
        return ^^void;
    }
    return template_arguments_of(dealias(remove_cvref(reader_io_meta)))[1];
}

/**
 * Name of ReaderIO.
 *
 * @tparam reader_io_meta ReaderIO reflection to check.
 * @return "<unknown>" if the reflection is not a ReaderIO, otherwise the ReaderIO's name.
 */
template <std::meta::info reader_io_meta>
constexpr std::string_view maybe_reader_io_name()
{
    // Guarded like `is_reader_io`'s members: the member splice requires the specialization to be
    // complete, so diagnostics over pre-validation or declared-only reflections stay well-formed.
    if constexpr (
        is_reader_io(reader_io_meta) && is_complete_type(dealias(remove_cvref(reader_io_meta))))
    {
        return [:reader_io_meta:] ::traits::name;
    }

    return "<unknown>";
}
}  // namespace detail
}  // namespace fnfelt::monad::reader_io
