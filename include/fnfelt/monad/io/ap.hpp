// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file ap.hpp
 *
 * Free `ap` (applicative) operation for the IO monad (and its implementation details).
 *
 * This header includes IO.hpp, so the IO class is complete, and defines the free `ap` functions.
 */
#pragma once

#include <meta>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/ap.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Applies a function IO to a value IO, producing an IO of the application's result (applicative).
 *
 * The returned IO runs the function IO first to obtain a callable, then runs the value IO to
 * obtain a value, then applies the callable to the value. Like `and_then`, the callable is invoked
 * with the value directly when possible, otherwise a `std::pair` or `std::tuple` value is spread
 * into the callable as if by `std::apply` (direct invocation wins over spreading).
 *
 * Both IOs are taken by forwarding reference and perfect-forwarded into the result IO's action, so
 * rvalue IOs move (rvalue chains move with no extra copies) and lvalue IOs are copied. Each IO is
 * forwarded independently, so a mix of an rvalue and an lvalue moves the former and copies the
 * latter.
 *
 * An invalid pair (including a non-IO argument) is rejected by a friendly static_assert and yields
 * `detail::ApError`.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to ApIOTraits.
 * @tparam TFnIO Function IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing a callable.
 * @tparam TValIO Value IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing the callable's argument.
 * @param fn_io IO producing the callable to apply (a non-IO is rejected by validation).
 * @param value_io IO producing the value to apply the callable to (a non-IO is rejected by
 * validation).
 * @return IO running the function IO then the value IO and applying the callable to the value,
 * or detail::ApError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits = ApIOTraits<>, class TFnIO, class TValIO>
[[nodiscard]] constexpr auto ap(TFnIO && fn_io, TValIO && value_io)
{
    using fn_io_type = std::remove_cvref_t<TFnIO>;
    using value_io_type = std::remove_cvref_t<TValIO>;
    static constexpr auto reason = TTraits::validate_ap(^^fn_io_type, ^^value_io_type);
    static_assert(
        reason.empty(), detail::ap_error_msg<^^fn_io_type, ^^value_io_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::ApAction<fn_io_type, value_io_type>;
        return create<TTraits>(action_type{FW(fn_io), FW(value_io)});
    }
    if constexpr (!reason.empty())
    {
        return detail::ApError{};
    }
}

/**
 * Applies a function IO to a value IO, naming the result (applicative).
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the IOs. Overloads on the first template argument: a name
 * (`char const *` non-type parameter) or a traits type.
 *
 * See the defaulted-traits overload for the application's runtime and validation semantics,
 * which are identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ap"_ss`).
 * @tparam TFnIO Function IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing a callable.
 * @tparam TValIO Value IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing the callable's argument.
 * @param fn_io IO producing the callable to apply (a non-IO is rejected by validation).
 * @param value_io IO producing the value to apply the callable to (a non-IO is rejected by
 * validation).
 * @return IO running the function IO then the value IO and applying the callable to the value
 * with ApIOTraits<name_cstr>, or detail::ApError if the pair is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TFnIO, class TValIO>
[[nodiscard]] constexpr auto ap(TFnIO && fn_io, TValIO && value_io)
{
    return ap<ApIOTraits<name_cstr>>(FW(fn_io), FW(value_io));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
