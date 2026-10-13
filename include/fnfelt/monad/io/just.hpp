// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file just.hpp
 *
 * Lifting a plain value into the IO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/just.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Lifts a value into the IO monad with the given traits (default detail::JustIOTraits).
 *
 * The returned IO, when run, yields the stored value with the value category of the run: the
 * value is moved out of an rvalue IO, consuming it, and copied out of an lvalue or const IO,
 * which remains runnable.
 *
 * The value is decayed and perfect-forwarded on input: lvalues are copied, rvalues moved, and
 * function lvalues become pointers. It is validated through the traits (`validate_just`,
 * defaulting to `detail::JustIOTraits::validate_just`): it must be a complete, non-reference,
 * non-IO type that is copy constructible, since it is stored in the action and copied out on each
 * lvalue run. An invalid value is rejected by a friendly static_assert and yields
 * `detail::JustError`.
 *
 * @tparam TTraits Traits used to validate the value and for the result IO, defaulting to
 * detail::JustIOTraits.
 * @tparam TValue Deduced type of the value to lift.
 * @param value The value produced when the IO is run.
 * @return IO over the decayed value with TTraits, or detail::JustError if the value is invalid
 * (which is also rejected by a friendly static_assert).
 */
template <class TTraits = detail::JustIOTraits<>, class TValue>
[[nodiscard]] constexpr auto just(TValue && value)
{
    using value_type = std::decay_t<TValue>;
    static constexpr auto reason = TTraits::validate_just(^^value_type);
    static_assert(reason.empty(), detail::just_error_msg<^^value_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::JustAction<value_type>;
        // Qualified so a value type mentioning other monads (e.g. a co-included ReaderIO) cannot
        // make the enclosing namespace's factories reachable by ADL and bind ambiguously.
        return io::create<TTraits>(action_type{FW(value)});
    }
    else
    {
        return detail::JustError{};
    }
}

/**
 * Lifts a value into the IO monad, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the value. Overloads on the first template argument: a name
 * (`char const *` non-type parameter) or a traits type.
 *
 * The result IO takes detail::JustIOTraits<name_cstr>, which is also used to validate the value.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My IO"_ss`).
 * @tparam TValue Deduced type of the value to lift.
 * @param value The value produced when the IO is run.
 * @return IO over the decayed value with detail::JustIOTraits<name_cstr>, or detail::JustError if
 * the value is invalid (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TValue>
[[nodiscard]] constexpr auto just(TValue && value)
{
    return io::just<detail::JustIOTraits<name_cstr>>(FW(value));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
