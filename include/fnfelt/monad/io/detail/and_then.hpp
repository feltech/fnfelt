// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/and_then.hpp
 *
 * Implementation-detail helpers for the `io::and_then` operations.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
// Forward declaration for the detail helpers below, which reflect on the proxy. The proxy itself
// is defined alongside the free functions.
template <class TSourceIO, class TContinuation>
struct AndThenProxy;

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
 * Reflection of the IO returned by a source IO's continuation.
 *
 * The continuation is applied with the source IO's value directly when possible, otherwise the
 * value is spread into it (direct invocation wins over spreading), mirroring the run-time dispatch.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the continuation's IO, or `void` if not determinable.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_continuation_io_meta()
{
    constexpr std::meta::info value_meta = io_value_meta(^^TSourceIO);
    if constexpr (is_directly_invocable(^^TContinuation, value_meta))
    {
        return invoke_result(^^TContinuation, {value_meta});
    }
    if constexpr (is_spread_invocable(^^TContinuation, value_meta))
    {
        return invoke_result(^^TContinuation, template_arguments_of(dealias(value_meta)));
    }
    return ^^void;
}

/**
 * Reflection of the value an and_then action produces when run.
 *
 * The invocation result is the continuation's IO, whose own value (async-transparent) is the final
 * value.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the value the action produces.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_result_meta()
{
    return io_value_meta(and_then_continuation_io_meta<TSourceIO, TContinuation>());
}

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
 * value category is forwarded to the source, and the stored continuation is invoked as an rvalue,
 * matching validation and result inference.
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
            auto input = FW(self).source.build().sync_wait();
            if constexpr (is_directly_invocable(^^TContinuation, ^^source_value_type))
            {
                auto next = FW(self).continuation(std::move(input));
                return std::move(next).build().sync_wait();
            }
            if constexpr (is_spread_invocable(^^TContinuation, ^^source_value_type))
            {
                auto next = std::apply(FW(self).continuation, std::move(input));
                return std::move(next).build().sync_wait();
            }
        }
        // Not reachable: validation guarantees one of the dispatches above is taken.
        std::unreachable();
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
    append_string_view(msg, io_name);
    msg += "{(";
    append_string_view(msg, maybe_io_name<source_io_meta>());
    msg += "()) => ";
    append_string_view(msg, maybe_continuation_result_io_name<source_io_meta, continuation_meta>());
    msg += "}: ";
    append_string_view(msg, reason);
    msg += ": ";
    std::meta::info const target_meta =
        !detail::is_io(source_io_meta) ? source_io_meta : continuation_meta;
    append_string_view(msg, display_string_of(target_meta));

    return msg;
}
}  // namespace detail

/**
 * Traits validating the source IO and continuation function provided to `io::and_then`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct AndThenIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the source IO and continuation function provided to @ref and_then.
     *
     * Checks, in order: the source is an IO producing a value (not void); the continuation function
     * is a complete, non-reference, move constructible class or function pointer type; it accepts
     * the source IO's value either directly or spread from its template arguments; and its result
     * is an IO.
     *
     * This is the overridable default implementation used by `io::and_then`: custom traits may
     * replace it to change which source IO and continuation pairs are accepted.
     *
     * @param io_meta Reflection of the source IO type.
     * @param continuation_meta Reflection of the continuation function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_and_then(
        std::meta::info io_meta, std::meta::info continuation_meta)
    {
        if (!detail::is_io(io_meta))
        {
            return "source IO is not an IO";
        }
        auto const value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(value_meta))
        {
            return "source IO produces no value";
        }
        if (!is_complete_type(continuation_meta))
        {
            return "continuation function is not a complete type";
        }
        if (is_reference_type(continuation_meta))
        {
            return "continuation function must not be a reference type";
        }
        continuation_meta = remove_cvref(continuation_meta);
        if (!is_class_type(continuation_meta) &&
            !(is_pointer_type(continuation_meta) &&
              is_function_type(remove_pointer(continuation_meta))))
        {
            return "continuation function is not a class or function pointer type";
        }
        if (!is_move_constructible_type(continuation_meta))
        {
            return "continuation function must be move constructible";
        }
        if (detail::is_directly_invocable(continuation_meta, value_meta))
        {
            std::meta::info result_meta = invoke_result(continuation_meta, {value_meta});
            if (!detail::is_io(result_meta))
            {
                return "continuation function must return an IO";
            }
            return {};
        }
        if (detail::is_spread_invocable(continuation_meta, value_meta))
        {
            std::meta::info result_meta =
                invoke_result(continuation_meta, template_arguments_of(dealias(value_meta)));
            if (!detail::is_io(result_meta))
            {
                return "continuation function must return an IO";
            }
            return {};
        }
        return "continuation function does not accept the source IO's value";
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
