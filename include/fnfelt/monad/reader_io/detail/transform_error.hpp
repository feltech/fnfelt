// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/transform_error.hpp
 *
 * Error mapping with a ReaderIO's environment retained.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <utility>

#include <fnfelt/monad/io/detail/transform_error.hpp>
#include <fnfelt/monad/io/transform_error.hpp>
#include <fnfelt/monad/reader_io/detail/transform.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/// Placeholder for invalid error mapping.
struct TransformErrorError
{
};

/**
 * Bind the source environment and map only its error payload.
 *
 * @tparam TState Environment type.
 * @tparam TSource Source ReaderIO type.
 * @tparam TTransformer Error mapper type.
 */
template <class TState, class TSource, class TTransformer>
struct TransformErrorAction
{
    /// Deferred source reader.
    TSource source;
    /// Pure error mapper.
    TTransformer transformer;

    /**
     * Delegate mapping without copying the environment.
     *
     * @param self Stored mapping action.
     * @param state Environment for the source.
     * @return Inner IO mapping only errors.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return FW(self).source.build(std::move(state)).io().transform_error(FW(self).transformer);
    }
};

/**
 * Validate error mapping using the inner IO's Result contract.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct TransformErrorReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Check source and mapper without querying impossible error handlers.
     *
     * @param source_meta Source ReaderIO reflection.
     * @param transformer_meta Error mapper reflection.
     * @return Empty on success, otherwise a validation reason.
     */
    static consteval std::string_view validate_transform_error(
        std::meta::info source_meta, std::meta::info transformer_meta)
    {
        if (!is_reader_io(source_meta))
        {
            return "source is not a ReaderIO";
        }
        return io::detail::TransformErrorIOTraits<>::validate_transform_error(
            reader_io_io_meta(source_meta), transformer_meta);
    }
};

/**
 * Format an error mapping failure with source and mapper context.
 *
 * @tparam source_meta Source ReaderIO reflection.
 * @tparam transformer_meta Error mapper reflection.
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Contextual diagnostic.
 */
template <std::meta::info source_meta, std::meta::info transformer_meta>
consteval std::string transform_error_error_msg(std::string_view name, std::string_view reason)
{
    return transform_error_msg<source_meta, transformer_meta>(name, reason, "transform_error");
}
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
