// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file ap.hpp
 *
 * Free `ap` (applicative) operation for the IO monad (and its implementation details).
 *
 * This header includes IO.hpp, so the IO class is complete, and defines the free `ap` functions.
 */
#pragma once

#include <meta>

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

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
 * Tombstone returned by `io::ap` when an argument IO is invalid.
 *
 * The invalid application is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct ApError
{
};

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
    constexpr auto operator()(this auto && self)
    {
        constexpr std::meta::info fn_value_meta = io_value_meta(^^TFnIO);
        constexpr std::meta::info val_value_meta = io_value_meta(^^TValueIO);
        // The callable and value are moved into local variables before invocation.
        auto fn = FW(self).fn_io();
        auto value = FW(self).value_io();
        if constexpr (is_invocable_type(fn_value_meta, {val_value_meta}))
        {
            return std::invoke(std::move(fn), std::move(value));
        }
        if constexpr (is_spread_invocable_as(fn_value_meta, val_value_meta))
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
            if constexpr (is_spread_invocable_as(fn_value_meta, val_value_meta))
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
    msg += ap_io_name;
    msg += "{(";
    msg += maybe_io_name<fn_io_meta>();
    msg += "()";
    msg += "(";
    msg += maybe_io_name<value_io_meta>();
    msg += "()";
    msg += ") => ";
    msg += maybe_ap_result_name<fn_io_meta, value_io_meta>();
    msg += ")}: ";
    msg += reason;
    msg += ": ";
    msg += display_string_of(target_meta);
    return msg;
}
}  // namespace detail

/**
 * Applies a function IO to a value IO, producing an IO of the application's result (applicative).
 *
 * The returned IO runs the function IO first to obtain a callable, then runs the value IO to
 * obtain a value, then applies the callable to the value. Like `and_then`, the callable is invoked
 * with the value directly when possible, otherwise the value is spread into the callable as if by
 * `std::apply` (direct invocation wins over spreading).
 *
 * Both IOs are taken by forwarding reference and perfect-forwarded into the result IO's action, so
 * rvalue IOs move (rvalue chains move with no extra copies) and lvalue IOs are copied. Each IO is
 * forwarded independently, so a mix of an rvalue and an lvalue moves the former and copies the
 * latter.
 *
 * An invalid pair (including a non-IO argument) is rejected by a friendly static_assert and yields
 * `detail::ApError`.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to IOTraits.
 * @tparam TFnIO Function IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing a callable.
 * @tparam TValIO Value IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing the callable's argument.
 * @param fn_io IO producing the callable to apply (a non-IO is rejected by validation).
 * @param value_io IO producing the value to apply the callable to (a non-IO is rejected by
 * validation).
 * @return IO running the function IO then the value IO and applying the callable to the value,
 * or detail::ApError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits = IOTraits<>, class TFnIO, class TValIO>
[[nodiscard]] constexpr auto ap(TFnIO && fn_io, TValIO && value_io)
{
    using fn_io_type = std::remove_cvref_t<TFnIO>;
    using value_io_type = std::remove_cvref_t<TValIO>;
    static constexpr auto reason = TTraits::validate_ap(^^fn_io_type, ^^value_io_type);
    static_assert(
        reason.empty(), detail::ap_error_msg<^^fn_io_type, ^^value_io_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::ApAction<fn_io_type, value_io_type>;
        return IO<action_type, TTraits>{action_type{FW(fn_io), FW(value_io)}};
    }
    if constexpr (!reason.empty())
    {
        return detail::ApError{};
    }
}

/**
 * Applies a function IO to a value IO, naming the result (applicative).
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the IOs. Overloads on the first template argument: a name
 * (`char const *` non-type parameter) or a traits type.
 *
 * See the defaulted-traits overload for the application's runtime and validation semantics,
 * which are identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My ap"_ss`).
 * @tparam TFnIO Function IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing a callable.
 * @tparam TValIO Value IO type, deduced (any value category; a non-IO argument is rejected by
 * validation), producing the callable's argument.
 * @param fn_io IO producing the callable to apply (a non-IO is rejected by validation).
 * @param value_io IO producing the value to apply the callable to (a non-IO is rejected by
 * validation).
 * @return IO running the function IO then the value IO and applying the callable to the value
 * with IOTraits<name_cstr>, or detail::ApError if the pair is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <char const * name_cstr, class TFnIO, class TValIO>
[[nodiscard]] constexpr auto ap(TFnIO && fn_io, TValIO && value_io)
{
    return ap<IOTraits<name_cstr>>(FW(fn_io), FW(value_io));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
