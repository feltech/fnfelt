// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file and_then.hpp
 *
 * Free `and_then` operation for the IO monad (and its implementation details).
 *
 * This header includes IO.hpp, so the IO class is complete, and defines the free `and_then`
 * functions.
 */
#pragma once

#include <meta>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/and_then.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Async proxy composing a source IO with a continuation (async/mixed `and_then`).
 *
 * The driver runs the source IO, applies the continuation to its value with the same direct-vs-
 * spread dispatch as the synchronous action, then runs the continuation's IO.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TContinuation>
struct AndThenProxy : AsyncProxyTag
{
    /// Value the composed IO produces.
    using value_type = [:detail::and_then_result_meta<TSourceIO, TContinuation>():];
    /// Source IO and continuation spread into @ref fn.
    std::tuple<TSourceIO, TContinuation> arg;
    /// Reflection of the source's value.
    // Dispatch is computed here, at class scope, because gcc 16.2 rejects reflection queries in a
    // lambda's `if constexpr` condition.
    static constexpr std::meta::info source_value_meta = detail::io_value_meta(^^TSourceIO);
    /// Whether the continuation is applied directly to the source's value.
    static constexpr bool is_direct_dispatch =
        detail::is_directly_invocable(^^TContinuation, source_value_meta);
    /// Whether the continuation is applied by spreading the value's template arguments.
    static constexpr bool is_spread_dispatch =
        !is_direct_dispatch && detail::is_spread_invocable(^^TContinuation, source_value_meta);
    /// Async/mixed composition driver.
    static constexpr auto fn =
        [](auto, TSourceIO source, TContinuation continuation) -> lf::task<value_type>
    {
        auto input = co_await detail::just_child(std::move(source));
        // Direct invocation wins over spreading, mirroring the synchronous action. The stored
        // continuation is moved into the call so the receiver matches validation and inference.
        if constexpr (is_direct_dispatch)
        {
            auto next = std::invoke(std::move(continuation), std::move(input));
            co_return co_await detail::just_child(std::move(next));
        }
        if constexpr (is_spread_dispatch)
        {
            auto next = std::apply(std::move(continuation), std::move(input));
            co_return co_await detail::just_child(std::move(next));
        }
        // Not reachable: validation guarantees one of the dispatches above is taken.
        std::unreachable();
    };

    /**
     * Construct from the source IO and continuation.
     *
     * @param arg_in Source IO and continuation to store.
     */
    constexpr explicit AndThenProxy(std::tuple<TSourceIO, TContinuation> arg_in)
        : arg{std::move(arg_in)}
    {
    }
};

/**
 * Chains a continuation to a source IO, producing an IO of the continuation's value.
 *
 * The returned IO runs the source IO, applies the continuation to its value, then runs the
 * continuation's IO. Values that are specialisations of `std::pair` or `std::tuple` are spread
 * into the continuation unless it accepts the value directly.
 *
 * The continuation must be a complete, non-reference, move constructible class or function pointer
 * type, callable as an rvalue with the source's value (directly or spread), and must return an
 * IO. An invalid pair (including a non-IO source) is rejected by a friendly static_assert and
 * yields `detail::AndThenError`.
 *
 * The source IO and continuation are taken by forwarding reference and perfect-forwarded into the
 * result IO's action, so an rvalue source moves (rvalue chains move with no extra copies) and an
 * lvalue source is copied (leaving the caller's IO untouched). The continuation is decayed, so an
 * lvalue continuation is copied and an rvalue moved into the result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to
 * AndThenIOTraits.
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @param source Source IO to run (a non-IO source is rejected by validation).
 * @param continuation Continuation applied to the source's value.
 * @return IO running the source IO followed by the continuation's IO, or detail::AndThenError if
 * the pair is invalid (which is also rejected by a friendly static_assert).
 */
template <class TTraits, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation)
{
    using source_io_type = std::remove_cvref_t<TSource>;
    // Decay the continuation for by-value validation and storage: lvalues are copied, rvalues are
    // moved, and function lvalues become function pointers.
    using continuation_type = std::decay_t<TContinuation>;
    static constexpr auto reason =
        TTraits::validate_and_then(^^source_io_type, ^^continuation_type);
    static_assert(
        reason.empty(),
        detail::and_then_error_msg<^^source_io_type, ^^continuation_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::AndThenAction<source_io_type, continuation_type>;
        return create<TTraits>(action_type{FW(source), FW(continuation)});
    }
    if constexpr (!reason.empty())
    {
        return detail::AndThenError{};
    }
}

/**
 * Chains a continuation to a source IO, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the source IO. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result IO takes AndThenIOTraits<name_cstr>, which is also used to validate the source IO and
 * continuation pair, overriding the source IO's traits.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My and_then"_ss`).
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @param source Source IO to run (a non-IO source is rejected by validation).
 * @param continuation Continuation applied to the source's value.
 * @return IO running the source IO followed by the continuation's IO with
 * AndThenIOTraits<name_cstr>, or detail::AndThenError if the pair is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation)
{
    return and_then<AndThenIOTraits<name_cstr>>(FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
