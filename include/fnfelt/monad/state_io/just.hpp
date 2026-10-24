// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/just.hpp
 *
 * Lifting a plain value into the StateIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/just.hpp>
#include <fnfelt/monad/state_io/lift.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{

/**
 * Lifts a value into the StateIO monad with the given traits (default detail::JustStateIOTraits).
 *
 * Equivalent to lifting `io::just(value)`: the returned StateIO preserves its state and
 * yields an IO producing a pair of stored value and unchanged state. The value is decayed and
 * perfect-forwarded on input: lvalues are copied, rvalues moved.
 *
 * The value is validated through the traits (`validate_just`, delegating to
 * `detail::JustIOTraits`): it must be a complete, non-reference, non-IO type that is copy
 * constructible, since it is stored in the embedded IO's action and copied out on each lvalue
 * build. An invalid value is rejected by a friendly static_assert and yields `detail::JustError`.
 * Custom traits must provide `validate_just`, `validate_lift` and `validate_action`, since the
 * successful value is passed through `lift`; inheriting detail::JustStateIOTraits supplies these
 * defaults while preserving the injected traits and diagnostic name.
 *
 * @tparam TState The StateIO's state type.
 * @tparam TTraits Traits used to validate the value and for the result StateIO, defaulting to
 * detail::JustStateIOTraits.
 * @tparam TValue Deduced type of the value to lift.
 * @param value The value produced when the embedded IO runs.
 * @return StateIO producing the decayed value and unchanged state with TTraits, or
 * detail::JustError if the value is invalid (which is also rejected by a friendly static_assert).
 */
template <class TState, class TTraits = detail::JustStateIOTraits<>, class TValue>
[[nodiscard]] constexpr auto just(TValue && value)
{
    using value_type = std::decay_t<TValue>;
    static constexpr auto reason = TTraits::validate_just(^^value_type);
    static_assert(reason.empty(), detail::just_error_msg<^^value_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::lift<TState, TTraits>(io::just(FW(value)));
    }
    else
    {
        return detail::JustError{};
    }
}

/**
 * Lifts a value into the StateIO monad, naming it for diagnostics.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the value. Overloads on the first template arguments: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result StateIO takes detail::JustStateIOTraits<name_cstr>, which is also used to validate
 * the value.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My StateIO"_ss`).
 * @tparam TState The StateIO's state type.
 * @tparam TValue Deduced type of the value to lift.
 * @param value The value produced when the embedded IO runs.
 * @return StateIO producing the value and unchanged state with
 * detail::JustStateIOTraits<name_cstr>, or detail::JustError if the value is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TState, class TValue>
[[nodiscard]] constexpr auto just(TValue && value)
{
    return state_io::just<TState, detail::JustStateIOTraits<name_cstr>>(FW(value));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
