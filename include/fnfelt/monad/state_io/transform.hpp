// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/transform.hpp
 *
 * Functor mapping for the StateIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/transform.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{

/**
 * Transforms a source StateIO's produced value with a transformer, re-wrapping the plain result.
 *
 * The composed StateIO binds the state, invokes the source with it (no state copy: the state is
 * forwarded unchanged and never reaches the transformer), and produces an IO that maps the source
 * IO's value through the transformer. Values that are specialisations of `std::pair` or
 * `std::tuple` are spread into the transformer unless it accepts the value directly. Unlike
 * `and_then`, the transformer's result is not run as an IO.
 *
 * The transformer must be a complete, non-reference, move constructible class or function pointer
 * type accepting the logical value directly or spread and returning a complete, move constructible
 * plain value by value. An invalid pair (including a non-StateIO source) is
 * rejected by a friendly static_assert and yields `detail::TransformError`.
 *
 * The source StateIO and transformer are taken by forwarding reference and perfect-forwarded
 * into the result StateIO's action, so an rvalue source moves (rvalue chains move with no extra
 * copies) and an lvalue source is copied (leaving the caller's StateIO untouched). The
 * transformer is decayed, so an lvalue transformer is copied and an rvalue moved into the result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result StateIO, defaulting to
 * detail::TransformStateIOTraits.
 * @tparam TSource Source StateIO type, deduced (a non-StateIO source is rejected by validation).
 * @tparam TTransformer Transformer taking the source's produced value and returning a plain value.
 * @param source Source StateIO to transform.
 * @param transformer Transformer applied to the produced value.
 * @return The composition StateIO, or detail::TransformError if the pair is invalid (which is
 * also rejected by a friendly static_assert).
 */
template <class TTraits, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer)
{
    using source_type = std::remove_cvref_t<TSource>;
    // Decay the transformer for by-value validation and storage: lvalues are copied, rvalues are
    // moved, and function lvalues become function pointers.
    using transformer_type = std::decay_t<TTransformer>;
    static constexpr auto reason = TTraits::validate_transform(^^source_type, ^^transformer_type);
    static_assert(
        reason.empty(),
        detail::transform_error_msg<^^source_type, ^^transformer_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::state_io_state_meta(^^source_type):];
        using action_type = detail::TransformAction<state_type, source_type, transformer_type>;
        // Qualified so the composition argument's namespaces cannot also find a foreign create of
        // the same shape and bind the call ambiguously.
        return state_io::create<state_type, TTraits>(action_type{FW(source), FW(transformer)});
    }
    else
    {
        return detail::TransformError{};
    }
}

/**
 * Transforms a source StateIO's produced value, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the source. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result StateIO takes detail::TransformStateIOTraits<name_cstr>, which is also used to
 * validate the source StateIO and transformer pair.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My StateIO"_ss`).
 * @tparam TSource Source StateIO type, deduced (a non-StateIO source is rejected by validation).
 * @tparam TTransformer Transformer taking the source's produced value and returning a plain value.
 * @param source Source StateIO to transform.
 * @param transformer Transformer applied to the produced value.
 * @return The composition StateIO with detail::TransformStateIOTraits<name_cstr>, or
 * detail::TransformError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer)
{
    // cpplint mistakes the recursive call for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    return state_io::transform<detail::TransformStateIOTraits<name_cstr>>(
        FW(source), FW(transformer));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
