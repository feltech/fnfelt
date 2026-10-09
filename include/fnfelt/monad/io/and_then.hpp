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

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/errors.hpp>
#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/traits.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Tombstone returned by `io::and_then` when the continuation is invalid.
 *
 * The invalid and_then is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct AndThenError
{
};

/**
 * Whether an and_then composition must run asynchronously.
 *
 * The composition is asynchronous when either the source action or the continuation's returned IO's
 * action is asynchronous. The result type and the action's run-time dispatch both derive from this
 * predicate, keeping them in sync.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return True if the composition runs asynchronously.
 */
template <class TSourceIO, class TContinuation>
consteval bool and_then_is_async()
{
    constexpr std::meta::info source_action_meta = io_action_meta(^^TSourceIO);
    if constexpr (action_is_async(source_action_meta))
    {
        return true;
    }
    constexpr std::meta::info continuation_io_meta =
        and_then_continuation_io_meta<TSourceIO, TContinuation>();
    if constexpr (is_io(continuation_io_meta))
    {
        return action_is_async(io_action_meta(continuation_io_meta));
    }
    return false;
}

/**
 * Reflection of the value an AndThenAction's declared return type names.
 *
 * Synchronous compositions produce a plain value; asynchronous ones produce the composed async
 * proxy.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the action's declared return type.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_result_type_meta()
{
    if constexpr (and_then_is_async<TSourceIO, TContinuation>())
    {
        return ^^AndThenProxy<TSourceIO, TContinuation>;
    }
    return and_then_result_meta<TSourceIO, TContinuation>();
}

/**
 * Action that runs a source IO and feeds its value to a continuation.
 *
 * Synchronous compositions run the source and continuation inline and produce a plain value;
 * asynchronous or mixed compositions instead materialise the composed async proxy so that the whole
 * tree runs under a single top-level scheduler.
 *
 * The continuation is invoked with the value directly when possible, otherwise the value is spread
 * into the continuation as if by `std::apply` (direct invocation wins over spreading). The action's
 * value category is forwarded to the source and continuation, so running an rvalue moves captures.
 * The continuation must still be callable as a const lvalue.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TContinuation>
struct AndThenAction
{
    /// Value the action produces when run (async proxy or plain value).
    using result_type = [:and_then_result_type_meta<TSourceIO, TContinuation>():];
    /// Source IO to run.
    TSourceIO source;
    /// Continuation applied to the source's value.
    TContinuation continuation;

    /**
     * Run the source IO, apply the continuation to its value, then run the resulting IO.
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The composed async proxy, or the final value on the synchronous path.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (and_then_is_async<TSourceIO, TContinuation>())
        {
            return result_type{
                std::tuple<TSourceIO, TContinuation>{FW(self).source, FW(self).continuation}};
        }
        if constexpr (!and_then_is_async<TSourceIO, TContinuation>())
        {
            using source_value_type = TSourceIO::value_type;
            auto input = FW(self).source().sync_wait();
            if constexpr (is_directly_invocable(^^TContinuation, ^^source_value_type))
            {
                auto next = FW(self).continuation(std::move(input));
                return std::move(next)().sync_wait();
            }
            if constexpr (is_spread_invocable(^^TContinuation, ^^source_value_type))
            {
                auto next = std::apply(FW(self).continuation, std::move(input));
                return std::move(next)().sync_wait();
            }
        }
    }
};

/**
 * Name of IO returned from the continuation.
 *
 * @tparam source_io_meta Source IO reflection.
 * @tparam continuation_meta Continuation reflection.
 * @return Name of IO returned from the continuation if possible, otherwise "<unknown>".
 */
template <std::meta::info source_io_meta, std::meta::info continuation_meta>
constexpr std::string_view maybe_continuation_result_io_name()
{
    if constexpr (is_io(source_io_meta))
    {
        constexpr std::meta::info continuation_arg_meta = io_value_meta(source_io_meta);

        if constexpr (is_invocable_type(continuation_meta, {continuation_arg_meta}))
        {
            constexpr std::meta::info continuation_result_io =
                invoke_result(continuation_meta, {continuation_arg_meta});
            return maybe_io_name<continuation_result_io>();
        }
    }

    return "<unknown>";
}

/**
 * Constructs a detailed error message for an invalid `io::and_then` operation.
 *
 * Uses reflection to extract type names and diagnostics about the source IO and continuation when
 * and_then validation fails. The message includes the IO name, the source IO type, the
 * continuation's result type (if determinable), the specific validation failure reason, and the
 * offending argument's display string (the source type if the source is not an IO, otherwise the
 * continuation type).
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_io_meta Reflection info for the source IO type.
 * @tparam continuation_meta Reflection info for the continuation type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_and_then`.
 * @return Formatted error message string.
 */
template <std::meta::info source_io_meta, std::meta::info continuation_meta>
consteval std::string and_then_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO and_then error: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += "{(";
    fnfelt::detail::append_string_view(msg, maybe_io_name<source_io_meta>());
    msg += "()) => ";
    fnfelt::detail::append_string_view(
        msg, maybe_continuation_result_io_name<source_io_meta, continuation_meta>());
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    std::meta::info const target_meta =
        !detail::is_io(source_io_meta) ? source_io_meta : continuation_meta;
    fnfelt::detail::append_string_view(msg, display_string_of(target_meta));

    return msg;
}
}  // namespace detail

/**
 * Chains a continuation to a source IO, producing an IO of the continuation's value.
 *
 * The returned IO runs the source IO, applies the continuation to its value, then runs the
 * continuation's IO. Values that are specialisations with all-type template arguments (e.g.
 * `std::pair`, `std::tuple`) are spread into the continuation unless it accepts the value
 * directly.
 *
 * The continuation must be a complete, non-reference, move constructible class or function pointer
 * type, callable as a const lvalue with the source's value (directly or spread), and must return an
 * IO. An invalid pair (including a non-IO source) is rejected by a friendly static_assert and
 * yields `detail::AndThenError`.
 *
 * The source IO and continuation are taken by forwarding reference and perfect-forwarded into the
 * result IO's action, so an rvalue source moves (rvalue chains move with no extra copies) and an
 * lvalue source is copied (leaving the caller's IO untouched). The continuation is decayed, so an
 * lvalue continuation is copied and an rvalue moved into the result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to IOTraits.
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
        return IO<action_type, TTraits>{action_type{FW(source), FW(continuation)}};
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
 * The result IO takes IOTraits<name_cstr>, which is also used to validate the source IO and
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
 * @return IO running the source IO followed by the continuation's IO with IOTraits<name_cstr>,
 * or detail::AndThenError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation)
{
    return and_then<IOTraits<name_cstr>>(FW(source), FW(continuation));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
