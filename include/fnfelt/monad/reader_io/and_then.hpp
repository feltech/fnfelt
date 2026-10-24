// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/and_then.hpp
 *
 * Monadic composition for the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/and_then.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{

/**
 * Chains a continuation to a source ReaderIO, producing a ReaderIO of the continuation's value
 * over the same state type.
 *
 * The composed ReaderIO binds the state, invokes the source with its own copy of the state,
 * applies the continuation to the source IO's produced value, then invokes the continuation's
 * ReaderIO with a second copy of the same state. Values that are specialisations of `std::pair`
 * or `std::tuple` are spread into the continuation unless it accepts the value directly.
 *
 * The continuation must be a complete, non-reference, move constructible class or function
 * pointer type, callable as an rvalue with the source's produced value, and must return a ReaderIO
 * over the same state type that produces a value. The source's state must be copy constructible.
 * An invalid pair (including a non-ReaderIO source) is rejected by a friendly static_assert and
 * yields `detail::AndThenError`.
 *
 * The source ReaderIO and continuation are taken by forwarding reference and perfect-forwarded
 * into the result ReaderIO's action, so an rvalue source moves (rvalue chains move with no extra
 * copies) and an lvalue source is copied (leaving the caller's ReaderIO untouched). The
 * continuation is decayed, so an lvalue continuation is copied and an rvalue moved into the
 * result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result ReaderIO, defaulting to
 * detail::AndThenReaderIOTraits.
 * @tparam TSource Source ReaderIO type, deduced (a non-ReaderIO source is rejected by validation).
 * @tparam TContinuation Continuation taking the source's produced value and returning a ReaderIO.
 * @param source Source ReaderIO to invoke.
 * @param continuation Continuation applied to the source's produced value.
 * @return The composition ReaderIO, or detail::AndThenError if the pair is invalid (which is also
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
        // The continuation's ReaderIO state type is the composition's state type (validated).
        using state_type = [:detail::reader_io_state_meta(^^source_type):];
        using action_type = detail::AndThenAction<state_type, source_type, continuation_type>;
        // Qualified so the composition argument's namespaces cannot also find a foreign create of
        // the same shape and bind the call ambiguously.
        return reader_io::create<state_type, TTraits>(action_type{FW(source), FW(continuation)});
    }
    else
    {
        return detail::AndThenError{};
    }
}

/**
 * Chains a continuation to a source ReaderIO, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the source. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result ReaderIO takes detail::AndThenReaderIOTraits<name_cstr>, which is also used to
 * validate the source ReaderIO and continuation pair.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ReaderIO"_ss`).
 * @tparam TSource Source ReaderIO type, deduced (a non-ReaderIO source is rejected by validation).
 * @tparam TContinuation Continuation taking the source's produced value and returning a ReaderIO.
 * @param source Source ReaderIO to invoke.
 * @param continuation Continuation applied to the source's produced value.
 * @return The composition ReaderIO with detail::AndThenReaderIOTraits<name_cstr>, or
 * detail::AndThenError if the pair is invalid (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation)
{
    return reader_io::and_then<detail::AndThenReaderIOTraits<name_cstr>>(
        FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
