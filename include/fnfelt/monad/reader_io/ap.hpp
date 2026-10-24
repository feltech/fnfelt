// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/ap.hpp
 *
 * Applicative application for the ReaderIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/ap.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{

/**
 * Applies a function source ReaderIO to a value source ReaderIO (applicative).
 *
 * The composed ReaderIO binds the state, invokes the function source with its own copy of the
 * state and the value source with the (moved) bound state, then applies the function's produced
 * callable to the produced value via `io::ap`. Independent sources overlap under one runtime
 * scheduler; constant evaluation runs them inline.
 *
 * Both sources must be ReaderIOs typed over the same copy-constructible state. The callable/value
 * contract between them is delegated to the IO's ap validator on the reflected inner IOs. An
 * invalid pair is rejected by a friendly static_assert and yields `detail::ApError`.
 *
 * The two sources are taken by forwarding reference and perfect-forwarded into the result
 * ReaderIO's action, so rvalue sources move (rvalue chains move with no extra copies) and lvalue
 * sources are copied (leaving the callers' ReaderIOs untouched).
 *
 * @tparam TTraits Traits used to validate the pair and for the result ReaderIO, defaulting to
 * detail::ApReaderIOTraits.
 * @tparam TFnSource ReaderIO whose produced IO yields the callable, deduced (a non-ReaderIO is
 * rejected by validation).
 * @tparam TValueSource ReaderIO whose produced IO yields the callable's argument, deduced (a
 * non-ReaderIO is rejected by validation).
 * @param fn_source ReaderIO producing the callable to apply.
 * @param value_source ReaderIO producing the callable's argument.
 * @return The application ReaderIO, or detail::ApError if the pair is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <class TTraits = detail::ApReaderIOTraits<>, class TFnSource, class TValueSource>
[[nodiscard]] constexpr auto ap(TFnSource && fn_source, TValueSource && value_source)
{
    using fn_source_type = std::remove_cvref_t<TFnSource>;
    using value_source_type = std::remove_cvref_t<TValueSource>;
    static constexpr auto reason = TTraits::validate_ap(^^fn_source_type, ^^value_source_type);
    static_assert(
        reason.empty(),
        detail::ap_error_msg<^^fn_source_type, ^^value_source_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::reader_io_state_meta(^^fn_source_type):];
        using action_type = detail::ApAction<state_type, fn_source_type, value_source_type>;
        // Qualified so the composition argument's namespaces cannot also find a foreign create of
        // the same shape and bind the call ambiguously.
        return reader_io::create<state_type, TTraits>(action_type{FW(fn_source), FW(value_source)});
    }
    else
    {
        return detail::ApError{};
    }
}

/**
 * Applies a function source ReaderIO to a value source ReaderIO, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * arguments explicitly while deducing the sources. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result ReaderIO takes detail::ApReaderIOTraits<name_cstr>, which is also used to validate the
 * two sources.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ap"_ss`).
 * @tparam TFnSource ReaderIO whose produced IO yields the callable, deduced.
 * @tparam TValueSource ReaderIO whose produced IO yields the callable's argument, deduced.
 * @param fn_source ReaderIO producing the callable to apply.
 * @param value_source ReaderIO producing the callable's argument.
 * @return The application ReaderIO with detail::ApReaderIOTraits<name_cstr>, or detail::ApError if
 * the pair is invalid (which is also rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TFnSource, class TValueSource>
[[nodiscard]] constexpr auto ap(TFnSource && fn_source, TValueSource && value_source)
{
    return reader_io::ap<detail::ApReaderIOTraits<name_cstr>>(FW(fn_source), FW(value_source));
}
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
