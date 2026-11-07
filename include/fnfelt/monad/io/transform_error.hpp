// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/transform_error.hpp
 *
 * Pure error mapping for the IO monad.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/transform_error.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Map a Result's error while preserving its success type and stopped state.
 *
 * Only errors invoke the mapper, directly or spread for std::pair and std::tuple errors. A
 * mapper-returned Result remains opaque error data. The source must produce a Result. Infallible
 * sources pass through without inspecting mapper invocation signatures or return types. The mapper
 * must return a movable error object, excluding IO and Infallible. Sources and mappers are stored
 * by value, copying lvalues and moving rvalues. An asynchronous source stays asynchronous.
 *
 * @tparam TTraits Validation and result traits.
 * @tparam TSource Source IO expression type.
 * @tparam TTransformer Error mapper expression type.
 * @param source Deferred Result source.
 * @param transformer Pure error mapper.
 * @return Deferred IO with the mapped error domain.
 */
template <class TTraits, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform_error(TSource && source, TTransformer && transformer)
{
    using source_type = std::remove_cvref_t<TSource>;
    using transformer_type = std::decay_t<TTransformer>;
    static constexpr auto reason =
        TTraits::validate_transform_error(^^source_type, ^^transformer_type);
    static_assert(reason.empty(), detail::transform_error_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return io::create<TTraits>(detail::TransformErrorAction<source_type, transformer_type>{
            FW(source), FW(transformer)});
    }
    else
    {
        return detail::TransformErrorError{};
    }
}

/**
 * Map a Result's error, naming the returned IO.
 *
 * @tparam name_cstr Diagnostic name with static storage.
 * @tparam TSource Source IO expression type.
 * @tparam TTransformer Error mapper expression type.
 * @param source Deferred Result source.
 * @param transformer Pure error mapper.
 * @return Named deferred IO with the mapped error domain.
 */
template <char const * name_cstr, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform_error(TSource && source, TTransformer && transformer)
{
    return io::transform_error<detail::TransformErrorIOTraits<name_cstr>>(
        FW(source), FW(transformer));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
