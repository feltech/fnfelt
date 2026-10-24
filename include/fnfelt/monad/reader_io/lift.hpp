// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/lift.hpp
 *
 * Lifting an IO into the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>

#include <fnfelt/monad/io/detail.hpp>
// The definitions backing `fnfelt::monad::io::detail::io_value_meta` (called via
// `detail::LiftReaderIOTraits::validate_lift`) live in async.hpp; including only io's detail.hpp
// would hard-error with "action_is_async used before its definition".
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/lift.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{

/**
 * Lifts an IO into the ReaderIO monad with the given traits (default detail::LiftReaderIOTraits).
 *
 * The returned ReaderIO ignores its environment: invoking it yields the source IO (copied for an
 * lvalue build, moved for an rvalue build), which the caller then runs with the IO machinery. An
 * IO, once constructed, computes its value for any state, so a lift embeds a state-independent
 * computation into a stateful context.
 *
 * The source is decayed and perfect-forwarded on input: lvalues are copied, rvalues moved. It is
 * validated through the traits (`validate_lift`): it must be an IO (not a ReaderIO) producing a
 * value. An invalid source is rejected by a friendly static_assert and yields
 * `detail::LiftError`. A move-only source IO must be built through an rvalue ReaderIO, enforced
 * by `ReaderIO::build`'s reachability check.
 *
 * Plain values have their own path: `reader_io::just` lifts a plain value (equivalently, the lift
 * of `io::just(value)`).
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TTraits Traits used to validate the source and for the result ReaderIO, defaulting to
 * detail::LiftReaderIOTraits.
 * @tparam TSource Deduced source type.
 * @param source The IO to embed.
 * @return ReaderIO yielding the source IO for any state, or detail::LiftError if the source is
 * invalid (which is also rejected by a friendly static_assert).
 */
template <class TState, class TTraits = detail::LiftReaderIOTraits<>, class TSource>
[[nodiscard]] constexpr auto lift(TSource && source)
{
    using source_type = std::decay_t<TSource>;
    static constexpr auto reason = TTraits::validate_lift(^^source_type);
    static_assert(reason.empty(), detail::lift_error_msg<^^source_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        // Qualified so the IO-typed embedded source's ADL cannot also pick up
        // `fnfelt::monad::io::create` and make the call ambiguous with the reader's own create.
        return reader_io::create<TState, TTraits>(
            detail::LiftAction<TState, source_type>{FW(source)});
    }
    else
    {
        return detail::LiftError{};
    }
}

/**
 * Lifts an IO into the ReaderIO monad, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the source. Overloads on the first template arguments: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result ReaderIO takes detail::LiftReaderIOTraits<name_cstr>, which is also used to validate
 * the source.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ReaderIO"_ss`).
 * @tparam TState The ReaderIO's state type.
 * @tparam TSource Deduced source type.
 * @param source The IO to embed.
 * @return ReaderIO yielding the source IO for any state with detail::LiftReaderIOTraits<name_cstr>,
 * or detail::LiftError if the source is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TState, class TSource>
[[nodiscard]] constexpr auto lift(TSource && source)
{
    return reader_io::lift<TState, detail::LiftReaderIOTraits<name_cstr>>(FW(source));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
