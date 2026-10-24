// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/just.hpp
 *
 * Lifting a plain value into the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/just.hpp>
#include <fnfelt/monad/reader_io/lift.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{

/**
 * Lifts a value into the ReaderIO monad with the given traits (default detail::JustReaderIOTraits).
 *
 * Equivalent to lifting `io::just(value)`: the returned ReaderIO ignores its environment and
 * yields an IO that, when run, produces the stored value. The value is decayed and
 * perfect-forwarded on input: lvalues are copied, rvalues moved.
 *
 * The value is validated through the traits (`validate_just`, delegating to
 * `detail::JustIOTraits`): it must be a complete, non-reference, non-IO type that is copy
 * constructible, since it is stored in the embedded IO's action and copied out on each lvalue
 * build. An invalid value is rejected by a friendly static_assert and yields `detail::JustError`.
 * Custom traits must provide `validate_just`, `validate_lift` and `validate_action`, since the
 * successful value is passed through `lift`; inheriting detail::JustReaderIOTraits supplies these
 * defaults while preserving the injected traits and diagnostic name.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TTraits Traits used to validate the value and for the result ReaderIO, defaulting to
 * detail::JustReaderIOTraits.
 * @tparam TValue Deduced type of the value to lift.
 * @param value The value produced when the embedded IO runs.
 * @return ReaderIO yielding an IO of the decayed value with TTraits, or detail::JustError if the
 * value is invalid (which is also rejected by a friendly static_assert).
 */
template <class TState, class TTraits = detail::JustReaderIOTraits<>, class TValue>
[[nodiscard]] constexpr auto just(TValue && value)
{
    using value_type = std::decay_t<TValue>;
    static constexpr auto reason = TTraits::validate_just(^^value_type);
    static_assert(reason.empty(), detail::just_error_msg<^^value_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return reader_io::lift<TState, TTraits>(io::just(FW(value)));
    }
    else
    {
        return detail::JustError{};
    }
}

/**
 * Lifts a value into the ReaderIO monad, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the value. Overloads on the first template arguments: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result ReaderIO takes detail::JustReaderIOTraits<name_cstr>, which is also used to validate
 * the value.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ReaderIO"_ss`).
 * @tparam TState The ReaderIO's state type.
 * @tparam TValue Deduced type of the value to lift.
 * @param value The value produced when the embedded IO runs.
 * @return ReaderIO yielding an IO of the decayed value with detail::JustReaderIOTraits<name_cstr>,
 * or detail::JustError if the value is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TState, class TValue>
[[nodiscard]] constexpr auto just(TValue && value)
{
    return reader_io::just<TState, detail::JustReaderIOTraits<name_cstr>>(FW(value));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
