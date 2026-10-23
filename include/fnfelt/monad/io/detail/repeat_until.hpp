// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/repeat_until.hpp
 *
 * Implementation details for the IO monad's repeat_until loop operation.
 */
#pragma once

#include <meta>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
// Forward declaration for the detail helpers below, which reflect on the proxy. The proxy itself
// is defined after them.
template <class TSourceIO, class TState, class TStepper>
struct RepeatUntilProxy;

namespace detail
{

/**
 * Tombstone returned by `io::repeat_until` when the source IO, state or stepper is invalid.
 *
 * The invalid repeat_until is already rejected by a friendly static_assert; this type exists only
 * to keep the return type well-formed and must never be used.
 */
struct RepeatUntilError
{
};

/**
 * Reflection of the value a repeat_until loop produces when run.
 *
 * The stepper is invoked once per iteration with the state (moved, matching the run-time
 * `std::invoke(stepper, std::move(state), std::move(value))`) and the source's freshly produced
 * value; its result must be a `std::expected<NewValue, State>` specialisation, whose value type is
 * the loop's value.
 *
 * @tparam TSourceIO Source IO to re-run each iteration.
 * @tparam TState State threaded between iterations through the unexpected value.
 * @tparam TStepper Stepper taking the state and value, returning an expected.
 * @return Reflection of the expected's value type, or `void` if not determinable. The `void` is
 * the diagnostic placeholder described on `action_value_meta`.
 */
template <class TSourceIO, class TState, class TStepper>
consteval std::meta::info repeat_until_value_type_meta()
{
    // Every reflection query below is guarded: this meta is also instantiated with invalid
    // (pre-validation) inputs while diagnostics are composed, and invocability queries are hard,
    // non-catchable gcc errors for incomplete or non-type reflections.
    constexpr std::meta::info source_value_meta = io_value_meta(^^TSourceIO);
    constexpr std::meta::info state_meta = remove_cvref(^^TState);
    if constexpr (
        is_type(state_meta) && is_complete_type(state_meta) && is_type(^^TStepper) &&
        is_complete_type(^^TStepper) && is_type(source_value_meta) &&
        !is_void_type(source_value_meta) && is_complete_type(source_value_meta))
    {
        // The stepper is invoked as an lvalue receiver (see RepeatUntilAction and the proxy).
        constexpr std::meta::info receiver_meta = add_lvalue_reference(remove_cvref(^^TStepper));
        if constexpr (is_invocable_type(receiver_meta, {state_meta, source_value_meta}))
        {
            constexpr std::meta::info result_meta =
                dealias(invoke_result(receiver_meta, {state_meta, source_value_meta}));
            if constexpr (
                has_template_arguments(result_meta) &&
                template_of(result_meta) ==
                    ^^std::expected && template_arguments_of(result_meta).size() == 2)
            {
                return template_arguments_of(result_meta)[0];
            }
        }
    }
    return ^^void;
}

/**
 * Whether a repeat_until composition must run asynchronously.
 *
 * The composition is asynchronous when the source IO's action is asynchronous; the loop itself
 * adds no scheduler requirements. The result type and the action's run-time dispatch both derive
 * from this predicate, keeping them in sync.
 *
 * @tparam TSourceIO Source IO to re-run each iteration.
 * @return True if the composition runs asynchronously.
 */
template <class TSourceIO>
consteval bool repeat_until_is_async()
{
    return action_is_async(io_action_meta(^^TSourceIO));
}

/**
 * Reflection of the value a RepeatUntilAction's declared return type names.
 *
 * Synchronous compositions produce a plain value; asynchronous ones produce the loop's async
 * proxy.
 *
 * @tparam TSourceIO Source IO to re-run each iteration.
 * @tparam TState State threaded between iterations through the unexpected value.
 * @tparam TStepper Stepper taking the state and value, returning an expected.
 * @return Reflection of the action's declared return type.
 */
template <class TSourceIO, class TState, class TStepper>
consteval std::meta::info repeat_until_result_type_meta()
{
    if constexpr (repeat_until_is_async<TSourceIO>())
    {
        return ^^RepeatUntilProxy<TSourceIO, TState, TStepper>;
    }
    return repeat_until_value_type_meta<TSourceIO, TState, TStepper>();
}

/**
 * Action that re-runs a source IO in a loop until a stepper reports success.
 *
 * Each iteration copies the source action out of the stored source (the action must be copy
 * constructible - re-running an lvalue source never poisons it), invokes the stepper with the
 * current state and the freshly produced value, and threads the state through the stepper's
 * `std::unexpected` result. The first `std::expected` result's value becomes the loop's value.
 *
 * Synchronous compositions run the loop inline and produce the expected's value; asynchronous
 * compositions instead materialise the loop's async proxy so that the whole tree runs under a
 * single top-level scheduler.
 *
 * The stepper is stored by value and invoked as an lvalue receiver with exactly two arguments
 * (the state and the value), matching validation and result inference; the invocation receiver
 * is never an rvalue, though the stepper itself is moved into the async proxy when the source
 * is asynchronous.
 *
 * @tparam TSourceIO Source IO to re-run each iteration.
 * @tparam TState State threaded between iterations through the unexpected value.
 * @tparam TStepper Stepper taking the state and value, returning an expected.
 */
template <class TSourceIO, class TState, class TStepper>
struct RepeatUntilAction
{
    /// Value the action produces when run (async proxy or plain value).
    using result_type = [:repeat_until_result_type_meta<TSourceIO, TState, TStepper>():];
    /// Source IO to re-run on every iteration.
    TSourceIO source;
    /// State handed to the stepper's first invocation.
    TState initial_state;
    /// Stepper taking the state and value, returning an expected.
    TStepper stepper;

    /**
     * Run the source IO once per iteration until the stepper reports success.
     *
     * `self` is a named parameter, so `self.source` is an lvalue member access even when the
     * action is invoked as an rvalue: `build` on that lvalue copies the source action, which keeps
     * it intact for the following iterations (moving it would leave a moved-from action running
     * from the second iteration on).
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The loop's async proxy, or the final value on the synchronous path.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (repeat_until_is_async<TSourceIO>())
        {
            return result_type{std::tuple<TSourceIO, TState, TStepper>{
                FW(self).source, FW(self).initial_state, FW(self).stepper}};
        }
        else
        {
            TState state = std::move(self.initial_state);
            while (true)
            {
                auto value = self.source.build().sync_wait();
                auto result = std::invoke(self.stepper, std::move(state), std::move(value));
                if (result.has_value())
                {
                    // `operator*`, not `value()`: `value`'s throw path mandates a copy
                    // constructible error type, which would bar move-only states (the loop only
                    // reads the value in the success branch, so the unchecked access is safe).
                    return *std::move(result);
                }
                state = std::move(result).error();
            }
        }
        // Not reachable: validation guarantees one of the dispatches above is taken.
        std::unreachable();
    }
};

/**
 * Name of the value a stepper's `std::expected` result produces.
 *
 * Guards exactly like `repeat_until_value_type_meta`, since the diagnostic is instantiated even
 * when validation fails: the state, stepper and source value are only reflected when they are
 * complete types, and the result only when it is a `std::expected` specialisation. Any other input
 * yields "<unknown>".
 *
 * A short type name (`fnfelt::detail::maybe_short_type_name`) is produced rather than a display
 * string, so a long or nested value type never buries the reason mid-message. A value type with no
 * short name (e.g. a function pointer value) falls back to "<unknown>".
 *
 * @tparam source_meta Reflection of the source IO type.
 * @tparam state_meta Reflection of the state type.
 * @tparam stepper_meta Reflection of the stepper type.
 * @return A short name of the expected's value type if it has one, otherwise "<unknown>".
 */
template <std::meta::info source_meta, std::meta::info state_meta, std::meta::info stepper_meta>
constexpr std::string_view maybe_repeat_until_result_name()
{
    if constexpr (
        is_type(state_meta) && is_complete_type(state_meta) && is_type(stepper_meta) &&
        is_complete_type(stepper_meta))
    {
        constexpr std::meta::info state_arg_meta = remove_cvref(state_meta);
        // The stepper is invoked as an lvalue receiver (see RepeatUntilAction and the proxy).
        constexpr std::meta::info receiver_meta = add_lvalue_reference(stepper_meta);
        constexpr std::meta::info source_value_meta = io_value_meta(source_meta);
        if constexpr (
            is_type(source_value_meta) && !is_void_type(source_value_meta) &&
            is_complete_type(source_value_meta))
        {
            if constexpr (is_invocable_type(receiver_meta, {state_arg_meta, source_value_meta}))
            {
                constexpr std::meta::info result_meta =
                    dealias(invoke_result(receiver_meta, {state_arg_meta, source_value_meta}));
                if constexpr (
                    has_template_arguments(result_meta) &&
                    template_of(result_meta) ==
                        ^^std::expected && template_arguments_of(result_meta).size() == 2)
                {
                    constexpr std::optional<std::string_view> value_name =
                        fnfelt::detail::maybe_short_type_name(
                            dealias(template_arguments_of(result_meta)[0]));
                    if constexpr (value_name.has_value())
                    {
                        return *value_name;
                    }
                }
            }
        }
    }
    return "<unknown>";
}

/**
 * Name of the value the source IO produces, as used in the loop signature.
 *
 * Guards like `maybe_repeat_until_result_name`, since the diagnostic is instantiated even when
 * validation fails: the source is only reflected when it is an IO producing a value.
 *
 * @tparam source_meta Reflection of the source IO type.
 * @return Short name of the source IO's value, or "<unknown>" when the source is not an IO,
 * produces no value, or the value has no short name.
 */
template <std::meta::info source_meta>
constexpr std::string_view maybe_repeat_until_source_value_name()
{
    if constexpr (is_io(source_meta))
    {
        constexpr std::meta::info source_value_meta = io_value_meta(source_meta);
        if constexpr (is_type(source_value_meta) && !is_void_type(source_value_meta))
        {
            constexpr std::optional<std::string_view> value_name =
                fnfelt::detail::maybe_short_type_name(source_value_meta);
            if constexpr (value_name.has_value())
            {
                return *value_name;
            }
        }
    }
    return "<unknown>";
}

/**
 * Constructs the diagnostic message for a rejected repeat_until combination.
 *
 * The message has the shape
 * `fnfelt: IO repeat_until error: <io_name>{<source io name>, <stepper short name>(<state short
 * name>, <source value short name>) => <result short name>}: <reason>: <offending display
 * string>`, naming the IO instantiation, the attempted stepper and its loop parameter signature:
 * the source IO's traits name, the stepper's short name, the state's and the source IO's produced
 * value's short names, and the value the stepper's `std::expected` result produces. Each name
 * inside the braces is a short name, rendered as "<unknown>" when it is not determinable or the
 * type has no short name. The specific validation failure reason follows, and then the offending
 * argument's display string at the end: the source when it is not an IO or produces no value, the
 * state when it is not a complete type, otherwise the stepper (e.g. when a custom
 * `validate_repeat_until` rejected an otherwise well-formed combination).
 *
 * This function must never fail to evaluate or throw, since invalid combinations reach it before
 * validation has run: the guards exist for pre-validation inputs (non-IO, incomplete or void
 * reflections), with the name helpers falling through to safe placeholder names, while the
 * function's own three reflections are always type reflections (the public op reflects its local
 * decay aliases), so the queries it makes directly on them need no non-type guard. The
 * reflections are dealiased before naming so caller-side aliases (e.g. the public op's decay
 * aliases) do not leak into the message.
 *
 * @tparam source_meta Reflection of the (decayed) source IO type.
 * @tparam state_meta Reflection of the (decayed) state type.
 * @tparam stepper_meta Reflection of the (decayed) stepper type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_repeat_until`.
 * @return Formatted error message string.
 */
template <std::meta::info source_meta, std::meta::info state_meta, std::meta::info stepper_meta>
consteval std::string repeat_until_error_msg(std::string_view io_name, std::string_view reason)
{
    // The public op reflects its own local decay aliases (`source_io_type`, `state_type`,
    // `stepper_type`), so resolve them: the diagnostic must name the types the caller actually
    // supplied, not the implementation's alias names. This is also why the expected's value type
    // is dealiased in `maybe_repeat_until_result_name`.
    constexpr std::meta::info source_type_meta =
        is_type(source_meta) ? dealias(source_meta) : source_meta;
    constexpr std::meta::info state_type_meta =
        is_type(state_meta) ? dealias(state_meta) : state_meta;
    constexpr std::meta::info stepper_type_meta =
        is_type(stepper_meta) ? dealias(stepper_meta) : stepper_meta;
    constexpr std::meta::info offending_meta = []
    {
        if (!is_io(source_type_meta) || is_void_type(io_value_meta(source_type_meta)))
        {
            return source_type_meta;
        }
        if (!is_type(state_type_meta) || !is_complete_type(state_type_meta))
        {
            return state_type_meta;
        }
        return stepper_type_meta;
    }();
    // The stepper's is_type guard inside `maybe_short_type_name` covers non-type reflections, so
    // the name queries are safe on unvalidated inputs; the incomplete-but-named state (e.g.
    // `Incomplete`) shows its identifier, which is the intended rendering.
    constexpr std::optional<std::string_view> stepper_name =
        fnfelt::detail::maybe_short_type_name(stepper_type_meta);
    constexpr std::optional<std::string_view> state_name =
        fnfelt::detail::maybe_short_type_name(state_type_meta);
    constexpr std::string_view source_value_name =
        maybe_repeat_until_source_value_name<source_type_meta>();
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO repeat_until error: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += "{";
    fnfelt::detail::append_string_view(msg, maybe_io_name<source_type_meta>());
    msg += ", ";
    fnfelt::detail::append_string_view(msg, stepper_name.value_or("<unknown>"));
    msg += "(";
    fnfelt::detail::append_string_view(msg, state_name.value_or("<unknown>"));
    msg += ", ";
    fnfelt::detail::append_string_view(msg, source_value_name);
    msg += ") => ";
    fnfelt::detail::append_string_view(
        msg,
        maybe_repeat_until_result_name<source_type_meta, state_type_meta, stepper_type_meta>());
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(offending_meta));

    return msg;
}

}  // namespace detail

/**
 * Async proxy re-running a source IO in a loop until a stepper reports success.
 *
 * The driver re-runs the source IO on every iteration (passing the frame-local source lvalue to
 * @ref fnfelt::monad::io::detail::just_child, so the source action is copied per iteration and
 * stays intact), applies the stepper to the state and the value, and returns the `std::expected`
 * result's value at success.
 *
 * @tparam TSourceIO Source IO to re-run each iteration.
 * @tparam TState State threaded between iterations through the unexpected value.
 * @tparam TStepper Stepper taking the state and value, returning an expected.
 */
template <class TSourceIO, class TState, class TStepper>
struct RepeatUntilProxy : detail::TransportProxyTag
{
    /// Value the loop produces when the stepper reports success.
    using value_type = [:detail::repeat_until_value_type_meta<TSourceIO, TState, TStepper>():];
    /// Source IO, initial state and stepper spread into @ref fn.
    std::tuple<TSourceIO, TState, TStepper> arg;

    /// Run the loop: re-run the source IO per iteration, apply the stepper, stop at success.
    static constexpr auto fn = [](auto, TSourceIO source, TState initial_state, TStepper stepper)
        -> lf::task<detail::task_result_t<value_type>>
    {
        TState state = std::move(initial_state);
        while (true)
        {
            // The source is passed as an lvalue, so just_child copies it per iteration; the
            // frame-local source stays intact for the next one.
            auto value = detail::unpack_result(co_await detail::just_child(source));
            auto result = std::invoke(stepper, std::move(state), std::move(value));
            if (result.has_value())
            {
                // `operator*`, not `value()`: `value`'s throw path mandates a copy constructible
                // error type, which would bar move-only states (the loop only reads the value in
                // the success branch, so the unchecked access is safe).
                co_return detail::pack_result<value_type>(*std::move(result));
            }
            state = std::move(result).error();
        }
    };

    /// Repeat dependent effects inline during constant evaluation.
    static constexpr auto run_inline =
        [](TSourceIO source, TState state, TStepper stepper) -> value_type
    {
        while (true)
        {
            auto value = source.build().sync_wait();
            auto result = std::invoke(stepper, std::move(state), std::move(value));
            if (result.has_value())
            {
                return *std::move(result);
            }
            state = std::move(result).error();
        }
    };

    /**
     * Construct from the source IO, initial state and stepper.
     *
     * @param arg_in Source IO, initial state and stepper to store.
     */
    constexpr explicit RepeatUntilProxy(std::tuple<TSourceIO, TState, TStepper> arg_in)
        : arg{std::move(arg_in)}
    {
    }
};

namespace detail
{
/**
 * Traits validating the source IO, state and stepper function provided to `io::repeat_until`.
 *
 * The source IO must have a copy constructible (re-runnable) action, and the stepper function must
 * accept the state and value and return a `std::expected` whose unexpected type is the state type.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct RepeatUntilIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the source IO, threading state and stepper function.
     *
     * Checks, in order: the source is an IO producing a complete value (not void); the source's
     * action is copy constructible (the action is re-run, by copy, on every iteration); the state
     * is a complete, movable type; the stepper is a complete, non-reference, move constructible
     * class or function pointer type; it accepts the state (moved) and the source IO's value as
     * exactly two arguments with an lvalue receiver; and its result is a `std::expected`
     * specialisation whose unexpected type is the state type and whose value is a non-void,
     * non-reference, non-IO, complete, movable type.
     *
     * @param io_meta Reflection of the source IO type.
     * @param state_meta Reflection of the state type.
     * @param stepper_meta Reflection of the stepper function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_repeat_until(
        std::meta::info io_meta, std::meta::info state_meta, std::meta::info stepper_meta)
    {
        if (!detail::is_io(io_meta))
        {
            return "source IO is not an IO";
        }
        auto const source_value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(source_value_meta))
        {
            return "source IO produces no value";
        }
        // The value is an argument of the stepper's invocability queries below, which are hard,
        // non-catchable gcc errors for incomplete types.
        if (!is_complete_type(source_value_meta))
        {
            return "source IO's value is not a complete type";
        }
        // The source action is re-run on every iteration (a copy is built each time), so it must
        // stay re-runnable by copy.
        std::meta::info const source_action_meta = detail::io_action_meta(io_meta);
        if (!is_complete_type(source_action_meta) ||
            !is_copy_constructible_type(source_action_meta))
        {
            return "source IO's action must be copy constructible";
        }
        if (!is_complete_type(state_meta))
        {
            return "state is not a complete type";
        }
        state_meta = remove_cvref(state_meta);
        // The state is moved out of `initial_state` and reassigned from every `std::unexpected`
        // result, so it must be both move constructible and move assignable.
        if (!is_move_constructible_type(state_meta) || !is_move_assignable_type(state_meta))
        {
            return "state must be movable";
        }
        if (!is_complete_type(stepper_meta))
        {
            return "stepper function is not a complete type";
        }
        if (is_reference_type(stepper_meta))
        {
            return "stepper function must not be a reference type";
        }
        stepper_meta = remove_cvref(stepper_meta);
        if (!is_class_type(stepper_meta) &&
            !(is_pointer_type(stepper_meta) && is_function_type(remove_pointer(stepper_meta))))
        {
            return "stepper function is not a class or function pointer type";
        }
        // The stepper is held by value in the composition and invoked as an lvalue receiver, once
        // per iteration, so it must be move constructible (into the composition) but never copied.
        if (!is_move_constructible_type(stepper_meta))
        {
            return "stepper function must be move constructible";
        }
        std::meta::info const receiver_meta = add_lvalue_reference(stepper_meta);
        if (!is_invocable_type(receiver_meta, {state_meta, source_value_meta}))
        {
            return "stepper function does not accept the state and value";
        }
        std::meta::info const result_meta =
            dealias(invoke_result(receiver_meta, {state_meta, source_value_meta}));
        if (!has_template_arguments(result_meta) || template_of(result_meta) != ^^std::expected ||
            template_arguments_of(result_meta).size() != 2)
        {
            return "stepper function must return a std::expected";
        }
        std::meta::info const result_state_meta = template_arguments_of(result_meta)[1];
        if (!is_same_type(result_state_meta, state_meta))
        {
            return "stepper function's unexpected type is not the state type";
        }
        std::meta::info const result_value_meta = template_arguments_of(result_meta)[0];
        if (is_void_type(result_value_meta))
        {
            return "stepper function must return an expected that produces a value";
        }
        if (is_reference_type(result_value_meta))
        {
            return "stepper function's expected value must not be a reference type";
        }
        if (detail::is_io(result_value_meta))
        {
            return "stepper function's expected value should not be an IO";
        }
        if (!is_complete_type(result_value_meta))
        {
            return "stepper function's expected value is not a complete type";
        }
        // The value is returned through the loop's result (an `lf::task` on the async path), which
        // requires move constructibility and move assignability.
        if (!is_move_constructible_type(result_value_meta) ||
            !is_move_assignable_type(result_value_meta))
        {
            return "stepper function's expected value must be movable";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
