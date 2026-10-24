// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/lift.hpp
 *
 * Lifting an IO into the StateIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>

#include <fnfelt/monad/io/detail.hpp>
// The definitions backing `fnfelt::monad::io::detail::io_value_meta` (called via
// `detail::LiftStateIOTraits::validate_lift`) live in async.hpp; including only io's detail.hpp
// would hard-error with "action_is_async used before its definition".
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/lift.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{

/**
 * Lifts an IO into the StateIO monad with the given traits (default detail::LiftStateIOTraits).
 *
 * Embeds an IO or a ReaderIO while preserving the state. A ReaderIO receives a copy of the state;
 * its state type must match and be copy constructible. An IO needs no state copy. Both synchronous
 * and asynchronous sources execute in the enclosing IO scheduler.
 *
 * The source is decayed and perfect-forwarded on input: lvalues are copied, rvalues moved. It is
 * validated through the traits (`validate_lift`): it must be an IO or ReaderIO producing a
 * value. An invalid source is rejected by a friendly static_assert and yields
 * `detail::LiftError`. A move-only source IO must be built through an rvalue StateIO, enforced
 * by `StateIO::build`'s reachability check.
 *
 * Plain values have their own path: `state_io::just` lifts a plain value (equivalently, the lift
 * of `io::just(value)`).
 *
 * @tparam TState The StateIO's state type.
 * @tparam TTraits Traits used to validate the source and for the result StateIO, defaulting to
 * detail::LiftStateIOTraits.
 * @tparam TSource Deduced source type.
 * @param source The IO to embed.
 * @return StateIO producing the source value and unchanged state, or detail::LiftError if the
 * source is invalid (which is also rejected by a friendly static_assert).
 */
template <class TState, class TTraits = detail::LiftStateIOTraits<>, class TSource>
[[nodiscard]] constexpr auto lift(TSource && source)
{
    using source_type = std::decay_t<TSource>;
    static constexpr auto reason = TTraits::validate_lift(^^source_type, ^^TState);
    static_assert(reason.empty(), detail::lift_error_msg<^^source_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        // Qualified so the IO-typed embedded source's ADL cannot also pick up
        // `fnfelt::monad::io::create` and make the call ambiguous with StateIO's own create.
        return state_io::create<TState, TTraits>(
            detail::LiftAction<TState, source_type>{FW(source)});
    }
    else
    {
        return detail::LiftError{};
    }
}

/**
 * Lifts an IO into the StateIO monad, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the source. Overloads on the first template arguments: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result StateIO takes detail::LiftStateIOTraits<name_cstr>, which is also used to validate
 * the source.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My StateIO"_ss`).
 * @tparam TState The StateIO's state type.
 * @tparam TSource Deduced source type.
 * @param source The IO to embed.
 * @return StateIO producing the source value and state with detail::LiftStateIOTraits<name_cstr>,
 * or detail::LiftError if the source is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TState, class TSource>
[[nodiscard]] constexpr auto lift(TSource && source)
{
    return state_io::lift<TState, detail::LiftStateIOTraits<name_cstr>>(FW(source));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
