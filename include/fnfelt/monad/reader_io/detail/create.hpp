// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/create.hpp
 *
 * Implementation details for validating a ReaderIO's action/state pair and its consumption.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
// The definitions backing `fnfelt::monad::io::detail::io_value_meta` (which calls `action_is_async`
// from) live in async.hpp; including only io's detail.hpp would hard-error with "action_is_async
// used before its definition".
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{

/// Placeholder for a rejected action/state pair.
struct CreateError
{
};

/**
 * Tombstone returned by `ReaderIO::build` when the action cannot be copied out of the invoking
 * expression.
 *
 * The invalid build is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct ReaderBuildError
{
};

/**
 * Tombstone returned by `ReaderRunProxy::io`/`sync_wait` when the proxy is not consumed as an
 * rvalue.
 *
 * The invalid consumption is already rejected by a friendly static_assert; this type exists only
 * to keep the return type well-formed and must never be used.
 */
struct ReaderRunError
{
};

/**
 * Reflection of the inner IO a ReaderIO's action produces for a state.
 *
 * The action is reflected bare, modelling the rvalue receiver run-time invocation gives a
 * callable. Anything not determinable - a non-ReaderIO source, an incomplete or non-invocable
 * action, a non-IO invocation result - is the `void` placeholder described on
 * `reader_io_action_meta`: this helper is used by pre-validation diagnostics, so it must never
 * hard-error.
 *
 * @param action_meta Reflection of the action type.
 * @param state_meta Reflection of the state type as passed to the invocation.
 * @return Reflection of the produced IO, or `void`.
 */
consteval std::meta::info action_io_meta(std::meta::info action_meta, std::meta::info state_meta)
{
    std::meta::info const bare_action_meta = dealias(remove_cvref(action_meta));
    // Invocability queries are a hard, non-catchable gcc error for incomplete action types, and
    // the action may be a declared-only type, so guard before reflecting the invocation.
    if (!is_complete_type(bare_action_meta) || !is_complete_type(remove_cvref(state_meta)) ||
        !is_invocable_type(bare_action_meta, {state_meta}))
    {
        return ^^void;
    }
    std::meta::info const result_meta = invoke_result(bare_action_meta, {state_meta});
    std::meta::info const stripped_result_meta = dealias(remove_cvref(result_meta));
    if (!io::detail::is_io(stripped_result_meta))
    {
        return ^^void;
    }
    return stripped_result_meta;
}

/**
 * Reflection of the value a ReaderIO's action's IO produces for a state.
 *
 * Unwraps through the produced IO's async proxies, so this works for sync and async IOs. The
 * inputs may be pre-validation, so `void` is the diagnostic placeholder described on
 * `action_io_meta`.
 *
 * @param action_meta Reflection of the action type.
 * @param state_meta Reflection of the state type as passed to the invocation.
 * @return Reflection of the produced value, or `void`.
 */
consteval std::meta::info action_value_meta(std::meta::info action_meta, std::meta::info state_meta)
{
    std::meta::info const io_meta = action_io_meta(action_meta, state_meta);
    if (is_void_type(io_meta))
    {
        return ^^void;
    }
    return io::detail::io_value_meta(io_meta);
}

/**
 * Reflection of the inner IO a ReaderIO produces when its action is invoked with the ReaderIO's
 * state type.
 *
 * The action is the ReaderIO's second template argument and the state its first; the state is
 * passed exactly as the proxy passes it (see the run proxy's `io`). Inputs may be pre-validation,
 * so `void` is the diagnostic placeholder described on `action_io_meta`.
 *
 * @param reader_meta Reflection of the (possibly ReaderIO) type.
 * @return Reflection of the produced IO, or `void`.
 */
consteval std::meta::info reader_io_io_meta(std::meta::info reader_meta)
{
    std::meta::info const action_meta = reader_io_action_meta(reader_meta);
    if (is_void_type(action_meta))
    {
        return ^^void;
    }
    std::meta::info const state_meta = reader_io_state_meta(reader_meta);
    return action_io_meta(action_meta, state_meta);
}

/**
 * Reflection of the value a ReaderIO's action's IO produces for the ReaderIO's state type.
 *
 * @param reader_meta Reflection of the (possibly ReaderIO) type.
 * @return Reflection of the produced value, or `void`, with the diagnostic placeholder semantics
 * described on `reader_io_io_meta`.
 */
consteval std::meta::info reader_io_value_meta(std::meta::info reader_meta)
{
    std::meta::info const io_meta = reader_io_io_meta(reader_meta);
    if (is_void_type(io_meta))
    {
        return ^^void;
    }
    return io::detail::io_value_meta(io_meta);
}

/**
 * Constructs the diagnostic message for a rejected action/state pair.
 *
 * The message includes the ReaderIO name, the specific validation failure reason, and the action's
 * and state's display strings, so the reason stays readable before any compiler truncation of the
 * message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam action_meta Reflection of the (decayed) action type.
 * @tparam state_meta Reflection of the state type.
 * @param name Display name of the resulting ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_action`.
 * @return Formatted error message string.
 */
template <std::meta::info action_meta, std::meta::info state_meta>
consteval std::string create_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " action is invalid: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(action_meta));
    msg += " with state ";
    fnfelt::detail::append_string_view(msg, display_string_of(state_meta));
    return msg;
}

/**
 * Constructs the diagnostic message for a rejected run proxy consumption.
 *
 * The message includes the ReaderIO name and the specific validation failure reason.
 *
 * The message expression is instantiated even when validation passes, so it must still evaluate
 * for valid inputs; it must never be evaluated at runtime.
 *
 * @param name Display name of the ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_sync_wait`.
 * @return Formatted error message string.
 */
consteval std::string run_proxy_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " run proxy is invalid: ";
    fnfelt::detail::append_string_view(msg, reason);
    return msg;
}

/**
 * Traits validating a ReaderIO's action/state pair and the consumption of its run proxy.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct CreateReaderIOTraits
{
    /// Name of this ReaderIO instantiation, used in compile-time diagnostic messages.
    // A string_view over a static string is constant-initialised and cannot throw.
    // NOLINTNEXTLINE(*-statically-constructed-objects)
    static constexpr std::string_view name{name_cstr};

    /**
     * Validate construction of the stored action from a build receiver.
     *
     * @param action_meta Exact cv-ref-qualified action expression type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_build(std::meta::info action_meta)
    {
        return io::detail::CreateIOTraits<>::validate_build(action_meta);
    }

    /**
     * Check whether a type is a (cv-ref-stripped) specialisation of ReaderIO, with any state type,
     * action and traits.
     *
     * @param type_meta Reflection of the type.
     * @return True if the type is a ReaderIO specialisation.
     */
    static consteval bool is_reader_io(std::meta::info type_meta)
    {
        return detail::is_reader_io(type_meta);
    }

    /**
     * Validate the action/state pair given to a ReaderIO.
     *
     * The action must be a distinct callable (not a ReaderIO or an IO) with the stored-callable
     * shape shared with the IO monad; the state must be a complete, movable, non-reference type;
     * the action must be invocable as an rvalue with the state modelled by value; and it must
     * return a value-producing IO by value.
     *
     * @param action_meta Reflection of the action type.
     * @param state_meta Reflection of the state type as named.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_action(
        std::meta::info action_meta, std::meta::info state_meta)
    {
        action_meta = dealias(action_meta);
        // A ReaderIO is rejected before the shape checks, so a ReaderIO over any action is
        // rejected as a ReaderIO first.
        if (detail::is_reader_io(action_meta))
        {
            return "provided action is already a ReaderIO, pass its action instead";
        }
        if (io::detail::is_io(action_meta))
        {
            return "provided action is already an IO, use lift to embed it";
        }
        switch (fnfelt::detail::callable_shape_of(action_meta))
        {
            case fnfelt::detail::CallableShape::incomplete:
                return "provided action is not a complete type";
            case fnfelt::detail::CallableShape::reference:
                return "provided action must not be a reference type";
            case fnfelt::detail::CallableShape::not_class_or_function_pointer:
                return "provided action is not a class or function pointer type";
            case fnfelt::detail::CallableShape::not_movable:
                return "provided action must be move constructible";
            case fnfelt::detail::CallableShape::valid:
                break;
        }
        if (is_reference_type(state_meta))
        {
            return "provided state must not be a reference type";
        }
        if (!is_complete_type(state_meta))
        {
            return "provided state is not a complete type";
        }
        if (!is_move_constructible_type(state_meta))
        {
            return "provided state must be move constructible";
        }
        // The state is passed by value and moved into the action, so the invocation is modelled
        // with the bare state type.
        if (!is_invocable_type(action_meta, {state_meta}))
        {
            return "provided action is not callable with the provided state";
        }
        std::meta::info const raw_result = invoke_result(action_meta, {state_meta});
        if (is_reference_type(raw_result))
        {
            return "provided action returns a reference, return by value instead";
        }
        std::meta::info const result_meta = dealias(remove_cvref(raw_result));
        if (!io::detail::is_io(result_meta))
        {
            return "provided action does not return an IO";
        }
        if (is_void_type(io::detail::io_value_meta(result_meta)))
        {
            return "provided action returns an IO that produces no value";
        }
        return {};
    }

    /**
     * Validate the consumption of a ReaderIO run proxy.
     *
     * The proxy binds a state and consumes its members by rvalue; an lvalue or const-bound proxy
     * cannot support that contract (the inner IO's scheduler-argument validation is left to the
     * inner IO's own traits).
     *
     * @param proxy_meta Reflection of the run proxy's receiver type as the consuming expression
     * provides it.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_sync_wait(std::meta::info proxy_meta)
    {
        // `is_const_type` does not see through references (references themselves are never
        // cv-qualified), so strip the reference and keep the referent's cv-qualification.
        if (is_lvalue_reference_type(proxy_meta) || is_const_type(remove_reference(proxy_meta)))
        {
            return "the ReaderIO run proxy must be consumed as an rvalue (e.g. "
                   "std::move(proxy).sync_wait())";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io
