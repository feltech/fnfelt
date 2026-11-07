// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/transform_error.hpp
 *
 * Pure error mapping for ReaderIO.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/transform_error.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
/**
 * Map a Result error while retaining success, stop and the environment.
 *
 * The mapper accepts the error directly or spread and returns an owned error payload. A returned
 * Result remains opaque error data. Infallible sources do not inspect the mapper's signature.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source ReaderIO expression type.
 * @tparam TTransformer Error mapper expression type.
 * @param source Deferred Result source.
 * @param transformer Pure error mapper.
 * @return ReaderIO with the mapped error domain.
 */
template <class TTraits, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform_error(TSource && source, TTransformer && transformer)
{
    using source_type = std::remove_cvref_t<TSource>;
    using transformer_type = std::decay_t<TTransformer>;
    static constexpr auto reason =
        TTraits::validate_transform_error(^^source_type, ^^transformer_type);
    static_assert(
        reason.empty(),
        detail::transform_error_error_msg<^^source_type, ^^transformer_type>(
            TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::reader_io_state_meta(^^source_type):];
        return reader_io::create<state_type, TTraits>(
            detail::TransformErrorAction<state_type, source_type, transformer_type>{
                FW(source), FW(transformer)});
    }
    else
    {
        return detail::TransformErrorError{};
    }
}

/**
 * Map an error, naming the resulting ReaderIO.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TSource Source ReaderIO expression type.
 * @tparam TTransformer Error mapper expression type.
 * @param source Deferred Result source.
 * @param transformer Pure error mapper.
 * @return Named ReaderIO with the mapped error domain.
 */
template <char const * name_cstr, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform_error(TSource && source, TTransformer && transformer)
{
    return reader_io::transform_error<detail::TransformErrorReaderIOTraits<name_cstr>>(
        FW(source), FW(transformer));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
