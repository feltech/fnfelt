// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/and_then.hpp
 *
 * Monadic composition for the StateIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/and_then.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{

/**
 * Chains a continuation to a source StateIO, producing a StateIO of the continuation's value
 * over the same state type.
 *
 * The composed StateIO runs the source, applies the continuation to its logical value, and
 * invokes the continuation's StateIO with the updated state. Values that are specialisations of
 * `std::pair` or `std::tuple` are spread into the continuation unless it accepts the value
 * directly.
 *
 * The continuation must be a complete, non-reference, move constructible class or function
 * pointer type, callable as an rvalue with the source's produced value, and must return a StateIO
 * over the same state type that produces a value. State is moved between stages.
 * An invalid pair (including a non-StateIO source) is rejected by a friendly static_assert and
 * yields `detail::AndThenError`.
 *
 * The source StateIO and continuation are taken by forwarding reference and perfect-forwarded
 * into the result StateIO's action, so an rvalue source moves (rvalue chains move with no extra
 * copies) and an lvalue source is copied (leaving the caller's StateIO untouched). The
 * continuation is decayed, so an lvalue continuation is copied and an rvalue moved into the
 * result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result StateIO, defaulting to
 * detail::AndThenStateIOTraits.
 * @tparam TSource Source StateIO type, deduced (a non-StateIO source is rejected by validation).
 * @tparam TContinuation Continuation taking the source's produced value and returning a StateIO.
 * @param source Source StateIO to invoke.
 * @param continuation Continuation applied to the source's produced value.
 * @return The composition StateIO, or detail::AndThenError if the pair is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <class TTraits, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation)
{
    using source_type = std::remove_cvref_t<TSource>;
    // Decay the continuation for by-value validation and storage: lvalues are copied, rvalues are
    // moved, and function lvalues become function pointers.
    using continuation_type = std::decay_t<TContinuation>;
    static constexpr auto reason = TTraits::validate_and_then(^^source_type, ^^continuation_type);
    static_assert(
        reason.empty(),
        detail::and_then_error_msg<^^source_type, ^^continuation_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        // The continuation's StateIO state type is the composition's state type (validated).
        using state_type = [:detail::state_io_state_meta(^^source_type):];
        using action_type = detail::AndThenAction<state_type, source_type, continuation_type>;
        // Qualified so the composition argument's namespaces cannot also find a foreign create of
        // the same shape and bind the call ambiguously.
        return state_io::create<state_type, TTraits>(action_type{FW(source), FW(continuation)});
    }
    else
    {
        return detail::AndThenError{};
    }
}

/**
 * Chains a continuation to a source StateIO, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the source. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result StateIO takes detail::AndThenStateIOTraits<name_cstr>, which is also used to
 * validate the source StateIO and continuation pair.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My StateIO"_ss`).
 * @tparam TSource Source StateIO type, deduced (a non-StateIO source is rejected by validation).
 * @tparam TContinuation Continuation taking the source's produced value and returning a StateIO.
 * @param source Source StateIO to invoke.
 * @param continuation Continuation applied to the source's produced value.
 * @return The composition StateIO with detail::AndThenStateIOTraits<name_cstr>, or
 * detail::AndThenError if the pair is invalid (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation)
{
    return state_io::and_then<detail::AndThenStateIOTraits<name_cstr>>(
        FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
