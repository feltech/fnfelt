// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/ap.hpp
 *
 * Implementation-detail helpers for the `io::ap` operations.
 */
#pragma once

#include <meta>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Tombstone returned by `io::ap` when an argument IO is invalid.
 *
 * The invalid application is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct ApError
{
};

/**
 * Reflection of the value an ApAction produces when run.
 *
 * The function IO's callable is applied to the value IO's value directly when possible, otherwise
 * the value is spread into the callable (direct invocation wins over spreading), mirroring the
 * action's dispatch.
 *
 * Used as the action's declared return type so that validating the action never instantiates its
 * body, which would otherwise require copying a move-only nested action for the const receiver.
 *
 * @tparam TFnIO IO whose value is a callable.
 * @tparam TValueIO IO whose value is the callable's argument.
 * @return Reflection of the value the action produces.
 */
template <class TFnIO, class TValueIO>
consteval std::meta::info ap_result_meta()
{
    constexpr std::meta::info fn_value_meta = io_value_meta(^^TFnIO);
    constexpr std::meta::info value_meta = io_value_meta(^^TValueIO);
    if constexpr (is_invocable_type(fn_value_meta, {value_meta}))
    {
        return invoke_result(fn_value_meta, {value_meta});
    }
    if constexpr (is_spread_invocable(fn_value_meta, value_meta))
    {
        return invoke_result(fn_value_meta, template_arguments_of(dealias(value_meta)));
    }
    return ^^void;
}

/**
 * Action that runs a function IO and a value IO, then applies the function to the value.
 *
 * The function IO is run first to obtain a callable, then the value IO is run to obtain a value,
 * and the callable is applied to the value. The callable is invoked with the value directly when
 * possible, otherwise the value is spread into the callable as if by `std::apply` (direct
 * invocation wins over spreading), mirroring `AndThenAction`.
 *
 * The action's value category is forwarded to both IOs, so running an rvalue moves captures. The
 * callable is invoked on a moved local (prvalue receiver semantics).
 *
 * @tparam TFnIO IO whose value is a callable.
 * @tparam TValueIO IO whose value is the callable's argument.
 */
template <class TFnIO, class TValueIO>
struct ApAction
{
    /// Value the action produces when run.
    using result_type = [:ap_result_meta<TFnIO, TValueIO>():];
    /// IO whose value is a callable.
    TFnIO fn_io;
    /// IO whose value is the callable's argument.
    TValueIO value_io;

    /**
     * Run the function IO, run the value IO, then apply the callable to the value.
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The result of applying the function IO's value to the value IO's value.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        constexpr std::meta::info fn_value_meta = io_value_meta(^^TFnIO);
        constexpr std::meta::info val_value_meta = io_value_meta(^^TValueIO);
        // The callable and value are moved into local variables before invocation.
        auto fn = FW(self).fn_io.build().sync_wait();
        auto value = FW(self).value_io.build().sync_wait();
        if constexpr (is_invocable_type(fn_value_meta, {val_value_meta}))
        {
            return std::invoke(std::move(fn), std::move(value));
        }
        if constexpr (is_spread_invocable(fn_value_meta, val_value_meta))
        {
            return std::apply(std::move(fn), std::move(value));
        }
    }
};

/**
 * Name of the value produced by an ap application's result.
 *
 * An IO result shows the IO's name instead of its kind-based short name; any other result shows its
 * kind-based short name (see `detail::short_type_name`).
 *
 * @tparam result_meta Reflection of the application's result.
 * @return Short name of the application's result.
 */
template <std::meta::info result_meta>
constexpr std::string_view ap_result_name()
{
    if constexpr (is_io(result_meta))
    {
        return maybe_io_name<result_meta>();
    }
    return short_type_name(result_meta);
}

/**
 * Name of the value produced by applying an ap function IO to a value IO.
 *
 * The application is dispatched like `ApAction`: direct invocation when possible, otherwise
 * spreading the value's template arguments. Its result is named by `ap_result_name` (an IO result
 * is unusual for ap); any part not determinable from the reflections yields "<unknown>".
 *
 * @tparam fn_io_meta Reflection of the function IO.
 * @tparam value_io_meta Reflection of the value IO.
 * @return Short name of the application's result if determinable, otherwise "<unknown>".
 */
template <std::meta::info fn_io_meta, std::meta::info value_io_meta>
constexpr std::string_view maybe_ap_result_name()
{
    if constexpr (is_io(fn_io_meta) && is_io(value_io_meta))
    {
        constexpr std::meta::info fn_value_meta = io_value_meta(fn_io_meta);
        constexpr std::meta::info val_value_meta = io_value_meta(value_io_meta);
        if constexpr (is_complete_type(fn_value_meta))
        {
            if constexpr (is_invocable_type(fn_value_meta, {val_value_meta}))
            {
                return ap_result_name<invoke_result(fn_value_meta, {val_value_meta})>();
            }
            if constexpr (is_spread_invocable(fn_value_meta, val_value_meta))
            {
                return ap_result_name<invoke_result(
                    fn_value_meta, template_arguments_of(dealias(val_value_meta)))>();
            }
        }
    }

    return "<unknown>";
}

/**
 * Constructs a detailed error message for an invalid `io::ap` operation.
 *
 * Message for an invalid `io::ap`: the ap name, the application signature
 * `(FnIO()(ValueIO()) => Result)` (the function IO's run applied to the value IO's run) with
 * `<unknown>` for undeterminable parts, the validation
 * reason, and the offending type's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam fn_io_meta Reflection info for the function IO type.
 * @tparam value_io_meta Reflection info for the value IO type.
 * @param ap_io_name Name of the ap operation (from traits).
 * @param reason Validation failure reason from `TTraits::validate_ap`.
 * @return Formatted error message string.
 */
template <std::meta::info fn_io_meta, std::meta::info value_io_meta>
consteval std::string ap_error_msg(std::string_view ap_io_name, std::string_view reason)
{
    std::meta::info const target_meta = (!is_io(fn_io_meta) || !is_io(value_io_meta))
        ? (is_io(fn_io_meta) ? value_io_meta : fn_io_meta)
        : fn_io_meta;

    std::string msg;
    msg += "fnfelt: ";
    msg += "IO ap error: ";
    append_string_view(msg, ap_io_name);
    msg += "{(";
    append_string_view(msg, maybe_io_name<fn_io_meta>());
    msg += "()";
    msg += "(";
    append_string_view(msg, maybe_io_name<value_io_meta>());
    msg += "()";
    msg += ") => ";
    append_string_view(msg, maybe_ap_result_name<fn_io_meta, value_io_meta>());
    msg += ")}: ";
    append_string_view(msg, reason);
    msg += ": ";
    append_string_view(msg, display_string_of(target_meta));
    return msg;
}
}  // namespace detail

/**
 * Traits validating the IO-wrapped function and value provided to `io::ap`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct ApIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the IO-wrapped function and value provided to @ref ap.
     *
     * Checks that both reflections are IO specialisations, and that the IO-wrapped function's
     * callable accepts the IO-wrapped value's value either directly or spread from the value's
     * template arguments (direct invocation wins), and that the application returns a value (not
     * void).
     *
     * `is_invocable_type` bare-type (prvalue) semantics match the runtime invocation
     * `std::invoke(std::move(fn), std::move(value))` / `std::apply(std::move(fn), ...)` on moved
     * locals, the same moved-receiver contract as `detail::is_directly_invocable`.
     *
     * This is the overridable default implementation used by `io::ap`: custom traits may replace
     * it to change which IO-wrapped function/value pairs are accepted.
     *
     * @param fn_io_meta Reflection of the function IO type.
     * @param value_io_meta Reflection of the value IO type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_ap(
        std::meta::info fn_io_meta, std::meta::info value_io_meta)
    {
        if (!detail::is_io(fn_io_meta))
        {
            return "IO-wrapped function is not an IO";
        }
        if (!detail::is_io(value_io_meta))
        {
            return "IO-wrapped value is not an IO";
        }
        std::meta::info const fn_value_meta = detail::io_value_meta(fn_io_meta);
        std::meta::info const val_value_meta = detail::io_value_meta(value_io_meta);
        if (is_invocable_type(fn_value_meta, {val_value_meta}))
        {
            if (is_void_type(invoke_result(fn_value_meta, {val_value_meta})))
            {
                return "IO-wrapped function's callable does not return a value";
            }
            return {};
        }
        if (detail::is_spread_invocable(fn_value_meta, val_value_meta))
        {
            if (is_void_type(
                    invoke_result(fn_value_meta, template_arguments_of(dealias(val_value_meta)))))
            {
                return "IO-wrapped function's callable does not return a value";
            }
            return {};
        }
        return "IO-wrapped function's callable does not accept the IO-wrapped value's value";
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
