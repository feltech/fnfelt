// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/ap.hpp
 *
 * Implementation details for the IO monad's applicative application.
 */
#pragma once

#include <meta>

#include <exception>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <libfork/core/control_flow.hpp>
#include <libfork/core/eventually.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
// Forward declaration for the detail helpers below, which reflect on the proxy. The proxy itself
// is defined after them.
template <class TFnIO, class TValueIO>
struct ApProxy;

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
 * Reflection of the value an ap action produces when run.
 *
 * The function IO's callable is applied to the value IO's value directly when possible, otherwise
 * the value is spread into the callable (direct invocation wins over spreading), mirroring the
 * action's dispatch. The value is unwrapped through async proxies, so this works for both sync and
 * async IOs.
 *
 * @tparam TFnIO IO whose value is a callable.
 * @tparam TValueIO IO whose value is the callable's argument.
 * @return Reflection of the value the action produces.
 */
template <class TFnIO, class TValueIO>
consteval std::meta::info ap_value_type_meta()
{
    return fnfelt::detail::dispatch_result_meta(io_value_meta(^^TFnIO), io_value_meta(^^TValueIO));
}

/**
 * Action that runs a function IO and a value IO, then applies the function to the value.
 *
 * The independent IOs share a runtime scheduler, then the callable is applied to the value.
 * Constant evaluation runs the children inline. The callable is invoked with the value directly
 * when possible, otherwise the value is spread into the callable as if by `std::apply` (direct
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
    /// Proxy retaining the independent children.
    using result_type = ApProxy<TFnIO, TValueIO>;
    /// IO whose value is a callable.
    TFnIO fn_io;
    /// IO whose value is the callable's argument.
    TValueIO value_io;

    /**
     * Retain both children for execution.
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The composed proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        return result_type{std::tuple<TFnIO, TValueIO>{FW(self).fn_io, FW(self).value_io}};
    }
};

/**
 * Name of the value produced by an ap application's result.
 *
 * An IO result shows the IO's name instead of its kind-based short name; any other result shows its
 * kind-based short name (see `fnfelt::detail::short_type_name`).
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
    return fnfelt::detail::short_type_name(result_meta);
}

/**
 * Name of the value produced by applying an ap function IO to a value IO.
 *
 * The application is dispatched like `ApAction`: the direct-vs-spread dispatch of
 * `is_dispatch_invocable`. Its result is named by `ap_result_name` (an IO result is unusual for
 * ap); any part not determinable from the reflections yields "<unknown>".
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
            if constexpr (fnfelt::detail::is_dispatch_invocable(fn_value_meta, val_value_meta))
            {
                return ap_result_name<fnfelt::detail::dispatch_result_meta(
                    fn_value_meta, val_value_meta)>();
            }
        }
    }

    return "<unknown>";
}

/**
 * Constructs the diagnostic message for a rejected application.
 *
 * Message for a rejected application: the ap name, the application signature
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
    fnfelt::detail::append_string_view(msg, ap_io_name);
    msg += "{(";
    fnfelt::detail::append_string_view(msg, maybe_io_name<fn_io_meta>());
    msg += "()";
    msg += "(";
    fnfelt::detail::append_string_view(msg, maybe_io_name<value_io_meta>());
    msg += "()";
    msg += ") => ";
    fnfelt::detail::append_string_view(msg, maybe_ap_result_name<fn_io_meta, value_io_meta>());
    msg += ")}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(target_meta));
    return msg;
}

}  // namespace detail

/**
 * Async proxy composing a function IO and a value IO (async/mixed `ap`).
 *
 * The driver runs both IOs, overlapping them on a pool worker under a single top-level scheduler,
 * and applies the function IO's callable to the value IO's value, with the same direct-vs-spread
 * dispatch as constant evaluation. An exception from either side cannot surface while a child is
 * still outstanding.
 *
 * @tparam TFnIO IO whose value is a callable.
 * @tparam TValueIO IO whose value is the callable's argument.
 */
template <class TFnIO, class TValueIO>
struct ApProxy : detail::TransportProxyTag
{
    /// Value the composed IO produces.
    using value_type = [:detail::ap_value_type_meta<TFnIO, TValueIO>():];
    /// Function IO and value IO spread into @ref fn.
    std::tuple<TFnIO, TValueIO> arg;
    /// Async/mixed application driver.
    static constexpr auto fn =
        [](auto, TFnIO fn_io, TValueIO value_io) -> lf::task<detail::task_result_t<value_type>>
    {
        lf::eventually<detail::task_result_t<typename TFnIO::value_type>> fn_slot;
        lf::eventually<detail::task_result_t<typename TValueIO::value_type>> value_slot;
        std::exception_ptr launch_error;
        try
        {
            co_await detail::fork_child(fn_slot, std::move(fn_io));
            // Calling the value side may throw while materialising its proxy, after the function
            // side was forked. Retain both result slots until the unconditional join below.
            co_await detail::call_child(value_slot, std::move(value_io));
        }
        catch (...)
        {
            launch_error = std::current_exception();
        }
        co_await lf::join;
        if (launch_error)
        {
            std::rethrow_exception(launch_error);
        }
        // The direct-vs-spread dispatch is `invoke_dispatch`'s, on the moved locals.
        co_return detail::pack_result<value_type>(fnfelt::detail::invoke_dispatch(
            detail::unpack_result(std::move(*fn_slot)),
            detail::unpack_result(std::move(*value_slot))));
    };

    /// Evaluate independent children inline only during constant evaluation.
    static constexpr auto run_inline = [](TFnIO fn_io, TValueIO value_io) -> value_type
    {
        typename TFnIO::value_type fn = std::move(fn_io).build().sync_wait();
        typename TValueIO::value_type value = std::move(value_io).build().sync_wait();
        return fnfelt::detail::invoke_dispatch(std::move(fn), std::move(value));
    };

    /**
     * Construct from the function IO and value IO.
     *
     * @param arg_in Function IO and value IO to store.
     */
    constexpr explicit ApProxy(std::tuple<TFnIO, TValueIO> arg_in) : arg{std::move(arg_in)} {}
};

namespace detail
{
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
     * Validate the IO-wrapped function and value.
     *
     * Checks that both reflections are IO specialisations, and that the IO-wrapped function's
     * callable accepts the IO-wrapped value's value either directly or spread from the value's
     * template arguments (direct invocation wins, `is_dispatch_invocable`), and that the
     * application returns a value (not void).
     *
     * The bare-type (prvalue) receiver of `is_dispatch_invocable` matches the run-time
     * invocation on moved locals by `invoke_dispatch` (`std::invoke(std::move(fn),
     * std::move(value))` / `std::apply(std::move(fn), ...)`), the same moved-receiver contract.
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
        if (!fnfelt::detail::is_dispatch_invocable(fn_value_meta, val_value_meta))
        {
            return "IO-wrapped function's callable does not accept the IO-wrapped value's value";
        }
        if (is_void_type(fnfelt::detail::dispatch_result_meta(fn_value_meta, val_value_meta)))
        {
            return "IO-wrapped function's callable does not return a value";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
