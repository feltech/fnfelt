// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/lift.hpp
 *
 * Implementation details for lifting an IO into the StateIO monad.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
// The definitions backing `fnfelt::monad::io::detail::io_value_meta` (which calls `action_is_async`
// from) live in async.hpp; including only io's detail.hpp would hard-error with "action_is_async
// used before its definition".
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/reader_io/ReaderIO.hpp>
#include <fnfelt/monad/reader_io/detail/bind.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/state_io/detail.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>
#include <fnfelt/monad/state_io/fwd.hpp>
#include <fnfelt/result.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{

/**
 * Tombstone returned by `state_io::lift` when the source is invalid.
 *
 * The invalid lift is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct LiftError
{
};

/**
 * Preserve an entire asynchronous IO result alongside owned state.
 *
 * @tparam TState Owned state type.
 * @tparam TIO Embedded IO type.
 */
template <class TState, class TIO>
struct AttachStateProxy : io::detail::TransportProxyTag
{
    /// Whole IO value, including its Result layer, and retained state.
    using value_type = std::pair<typename TIO::value_type, TState>;
    /// Embedded source and retained state.
    std::tuple<TIO, TState> arg;

    /**
     * Await the source before attaching state to any of its outcomes.
     *
     * @par Callable parameters
     * The first argument is supplied by libfork. `source` is the owned IO and `state` is retained.
     */
    static constexpr auto fn =
        [](auto, TIO source, TState state) -> lf::task<io::detail::task_result_t<value_type>>
    {
        auto value = io::detail::unpack_result(co_await io::detail::just_child(std::move(source)));
        co_return io::detail::pack_result<value_type>(
            value_type{std::move(value), std::move(state)});
    };

    /**
     * Attach state after constant-evaluated execution, preserving the complete source value.
     *
     * @par Callable parameters
     * `source`: Embedded IO. `state`: State retained during execution.
     */
    static constexpr auto run_inline = [](TIO source, TState state) -> value_type
    { return {std::move(source).build().sync_wait(), std::move(state)}; };

    /**
     * Retain the source and state for deferred execution.
     *
     * @param stored Owned source and state.
     */
    constexpr explicit AttachStateProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Attach state without interpreting a source Result as IO control flow.
 *
 * @tparam TState Owned state type.
 * @tparam TIO Embedded IO type.
 */
template <class TState, class TIO>
struct AttachStateAction
{
    /// Whether the embedded source requires asynchronous transport.
    static constexpr bool is_async = io::detail::action_is_async(io::detail::io_action_meta(^^TIO));
    /// Whole source value and state, or their asynchronous driver.
    using result_type = std::conditional_t<
        is_async,
        AttachStateProxy<TState, TIO>,
        std::pair<typename TIO::value_type, TState>>;
    /// Embedded IO.
    TIO source;
    /// State retained even on error or stop.
    TState state;

    /**
     * Run inline or transfer ownership to the asynchronous driver.
     *
     * @param self Stored source and state.
     * @return Whole source value and retained state, or its driver.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (is_async)
        {
            return result_type{{FW(self).source, FW(self).state}};
        }
        else
        {
            return {FW(self).source.build().sync_wait(), FW(self).state};
        }
    }
};

/// Preserve exact stored state qualification while owning the embedded IO.
template <class TIO, class TState>
AttachStateAction(TIO &&, TState &&)
    -> AttachStateAction<std::remove_reference_t<TState>, std::remove_cvref_t<TIO>>;

/**
 * Embed an owned IO or ReaderIO while preserving the original state.
 *
 * @tparam TState The StateIO's state type.
 * @tparam TIO The embedded IO type.
 */
template <class TState, class TIO>
struct LiftAction
{
    /// The embedded IO.
    TIO io;

    /**
     * Pair the embedded source's value with the unchanged state.
     *
     * The explicit object parameter is not const, so an rvalue proxy (the only consumable one)
     * moves the embedded IO out; a copyable embedded IO is copied per lvalue build by
     * `StateIO::build`'s reachability check.
     *
     * @param self The action to run (explicit object parameter).
     * @param state State preserved while the source runs.
     * @return IO producing the source value and unchanged state.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        if constexpr (reader_io::detail::is_reader_io(^^TIO))
        {
            auto source = reader_io::detail::bind_reader<TState>(FW(self).io, state);
            return io::create(AttachStateAction{std::move(source), std::move(state)});
        }
        else
        {
            return io::create(AttachStateAction{FW(self).io, std::move(state)});
        }
    }
};

/**
 * Constructs the diagnostic message for a rejected lift source.
 *
 * The message includes the StateIO name, the source's signature, the specific validation failure
 * reason, and the offending source's display string, so the reason stays readable before any
 * compiler truncation of the message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_meta Reflection of the (decayed) source type.
 * @param name Display name of the resulting StateIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_lift`.
 * @return Formatted error message string.
 */
template <std::meta::info source_meta>
consteval std::string lift_error_msg(std::string_view name, std::string_view reason)
{
    // The source may be a non-IO when the not-an-IO check fails, so it is named by an IO name
    // when it is an IO and by its short name otherwise.
    std::string_view const source_name = io::detail::is_io(source_meta)
        ? io::detail::maybe_io_name<source_meta>()
        : fnfelt::detail::short_type_name(source_meta);
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " lift error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += '{';
    fnfelt::detail::append_string_view(msg, source_name);
    msg += "()}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(source_meta));
    return msg;
}

/**
 * Traits validating the source IO provided to `state_io::lift`.
 *
 * @tparam name_cstr Null-terminated name of the StateIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct LiftStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Validate the source IO provided to `state_io::lift`.
     *
     * The source must be an IO (not already a StateIO: a StateIO composes by passing its action
     * rather than being lifted) that produces a value, so the embedded IO can feed continuation
     * composition later.
     *
     * @param source_meta Reflection of the (decayed) source type.
     * @param state_meta Reflection of the StateIO state type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_lift(
        std::meta::info source_meta, std::meta::info state_meta)
    {
        if (reader_io::detail::is_reader_io(source_meta))
        {
            if (!is_same_type(reader_io::detail::reader_io_state_meta(source_meta), state_meta))
            {
                return "source ReaderIO must have the same state type";
            }
            if (!is_copy_constructible_type(state_meta))
            {
                return "lifting a ReaderIO requires copy constructible state";
            }
            auto const value_meta = reader_io::detail::reader_io_value_meta(source_meta);
            if (auto const reason = fnfelt::detail::ResultTypeInfo::from(value_meta).validate();
                !reason.empty())
            {
                return reason;
            }
            if (is_void_type(value_meta))
            {
                return "provided source ReaderIO produces no value";
            }
            return {};
        }
        if (detail::is_state_io(source_meta))
        {
            return "provided source is already a StateIO, pass it directly";
        }
        if (!io::detail::is_io(source_meta))
        {
            return "provided source is not an IO";
        }
        auto const value_meta = io::detail::io_value_meta(source_meta);
        if (auto const reason = fnfelt::detail::ResultTypeInfo::from(value_meta).validate();
            !reason.empty())
        {
            return reason;
        }
        if (is_void_type(value_meta))
        {
            return "provided source IO produces no value";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
