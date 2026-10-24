// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/ap.hpp
 *
 * Applicative application in the StateIO monad transformer.
 */
#pragma once
#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/ap.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Run the function computation, then the value computation with its updated state.
 *
 * Both synchronous and asynchronous computations execute sequentially. The value action is
 * materialised only after successful function execution. Apply the logical function to the logical
 * value, preferring direct invocation to pair/tuple spreading, and preserve the final state.
 *
 * @param function_source StateIO producing a function.
 * @param value_source StateIO producing its argument.
 * @return StateIO producing the application result and final state.
 */
template <class TTraits = detail::ApStateIOTraits<>, class TFunctionSource, class TValueSource>
[[nodiscard]] constexpr auto ap(TFunctionSource && function_source, TValueSource && value_source)
{
    using function_type = std::remove_cvref_t<TFunctionSource>;
    using value_type = std::remove_cvref_t<TValueSource>;
    static constexpr auto reason = TTraits::validate_ap(^^function_type, ^^value_type);
    static_assert(
        reason.empty(),
        detail::create_error_msg<^^function_type, ^^value_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using state_type = [:detail::state_io_state_meta(^^function_type):];
        return state_io::create<state_type, TTraits>(
            detail::ApAction<state_type, function_type, value_type>{
                FW(function_source), FW(value_source)});
    }
    else
    {
        return detail::ApError{};
    }
}

/**
 * Sequentially apply a function computation and name the result.
 *
 * @param function_source StateIO producing a function.
 * @param value_source StateIO producing its argument.
 * @return Named application StateIO.
 */
template <char const * name_cstr, class TFunctionSource, class TValueSource>
[[nodiscard]] constexpr auto ap(TFunctionSource && function_source, TValueSource && value_source)
{
    return state_io::ap<detail::ApStateIOTraits<name_cstr>>(FW(function_source), FW(value_source));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
