// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/transform.hpp
 *
 * Functor mapping for the IO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/transform.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Transforms a source IO's value with a transformer, re-wrapping the plain result in an IO.
 *
 * The returned IO runs the source IO, applies the transformer to its value, and produces the
 * transformer's plain value. An asynchronous source makes the returned IO asynchronous, so the
 * whole composition runs under a single top-level scheduler. Values that are specialisations of
 * `std::pair` or `std::tuple` are spread into the transformer unless it accepts the value
 * directly. Unlike `and_then`, the transformer's result is not run as an IO.
 *
 * The transformer must be a complete, non-reference, move constructible class or function pointer
 * type, callable as an rvalue with the source's value (directly or spread), and must return a
 * valid plain value (not void, not a reference, not an IO, move constructible). An invalid pair
 * (including a non-IO source) is rejected by a friendly static_assert and yields
 * `detail::TransformError`.
 *
 * The source IO and transformer are taken by forwarding reference and perfect-forwarded into the
 * result IO's action, so an rvalue source moves (rvalue chains move with no extra copies) and an
 * lvalue source is copied (leaving the caller's IO untouched). The transformer is decayed, so an
 * lvalue transformer is copied and an rvalue moved into the result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to
 * detail::TransformIOTraits.
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @param source Source IO to run (a non-IO source is rejected by validation).
 * @param transformer Transformer applied to the source's value.
 * @return IO running the source IO and producing the transformer's plain value, or
 * detail::TransformError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer)
{
    using source_io_type = std::remove_cvref_t<TSource>;
    // Decay the transformer for by-value validation and storage: lvalues are copied, rvalues are
    // moved, and function lvalues become function pointers.
    using transformer_type = std::decay_t<TTransformer>;
    static constexpr auto reason =
        TTraits::validate_transform(^^source_io_type, ^^transformer_type);
    static_assert(
        reason.empty(),
        detail::transform_error_msg<^^source_io_type, ^^transformer_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::TransformAction<source_io_type, transformer_type>;
        // Qualified so a transformer/source type mentioning other monads (e.g. a co-included
        // ReaderIO) cannot make the enclosing namespace's factories reachable by ADL and bind
        // ambiguously.
        return io::create<TTraits>(action_type{FW(source), FW(transformer)});
    }
    else
    {
        return detail::TransformError{};
    }
}

/**
 * Transforms a source IO's value, naming the result.
 *
 * This overload allows the returned IO type to be named, for use in diagnostics.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My transform"_ss`).
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @param source Source IO to run (a non-IO source is rejected by validation).
 * @param transformer Transformer applied to the source's value.
 * @return IO running the source IO and producing the transformer's plain value with
 * detail::TransformIOTraits<name_cstr>, or detail::TransformError if the pair is invalid (which is
 * also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer)
{
    // cpplint mistakes the recursive call for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    return io::transform<detail::TransformIOTraits<name_cstr>>(FW(source), FW(transformer));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
