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
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/traits.hpp>

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
 * and the callable is applied to the value with `std::invoke`. No tuple spreading occurs (unlike
 * BindAction): the value is always passed as a single argument.
 *
 * Both IOs are const members and are invoked as const lvalues. The callable and value are moved
 * into local variables before invocation, so the callable is invoked on a moved local (prvalue
 * receiver semantics).
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
     * @return The result of applying the function IO's value to the value IO's value.
     */
    constexpr auto operator()() const
    {
        auto fn = fn_io();
        auto value = value_io();
        return std::invoke(std::move(fn), std::move(value));
    }
};

/**
 * Name of the value produced by applying an ap function IO to a value IO.
 *
 * The application's result is a plain value, so its display string is used; an IO result (unusual
 * for ap) shows the IO's name instead. Any part not determinable from the reflections yields
 * "<unknown>".
 *
 * @tparam fn_io_meta Reflection of the function IO.
 * @tparam value_io_meta Reflection of the value IO.
 * @return Display string of the application's result if determinable, otherwise "<unknown>".
 */
template <std::meta::info fn_io_meta, std::meta::info value_io_meta>
constexpr std::string_view maybe_ap_result_name()
{
    if constexpr (is_io(fn_io_meta) && is_io(value_io_meta))
    {
        constexpr std::meta::info fn_value_meta = io_value_meta(fn_io_meta);
        constexpr std::meta::info val_value_meta = io_value_meta(value_io_meta);
        if constexpr (
            is_complete_type(fn_value_meta) && is_invocable_type(fn_value_meta, {val_value_meta}))
        {
            constexpr std::meta::info result_meta = invoke_result(fn_value_meta, {val_value_meta});
            if constexpr (is_io(result_meta))
            {
                return maybe_io_name<result_meta>();
            }
            static constexpr std::size_t max_type_name_length = 10;
            constexpr auto display_string = display_string_of(result_meta);
            if constexpr (display_string.size() < max_type_name_length)
            {
                return display_string;
            }
            return "value";
        }
    }

    return "<unknown>";
}

/**
 * Constructs a detailed error message for an invalid `io::ap` operation.
 *
 * Message for an invalid `io::ap`: the ap name, the application signature
 * `(FnIO(ValueIO()) => Result)` with `<unknown>` for undeterminable parts, the validation
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
 * obtain a value, then applies the callable to the value with `std::invoke`. Values are not
 * spread from tuples (unlike `bind`): the value is always passed as a single argument.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to IOTraits.
 * @tparam TFnAction Function IO's action, producing a callable.
 * @tparam TFnTraits Function IO's traits, used only for the function IO's own construction.
 * @tparam TValAction Value IO's action, producing the callable's argument.
 * @tparam TValTraits Value IO's traits, used only for the value IO's own construction.
 * @param fn_io IO producing the callable to apply.
 * @param value_io IO producing the value to apply the callable to.
 * @return IO running the function IO then the value IO and applying the callable to the value,
 * or detail::ApError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <
    class TTraits = IOTraits<>,
    class TFnAction,
    class TFnTraits,
    class TValAction,
    class TValTraits>
[[nodiscard]] constexpr auto ap(IO<TFnAction, TFnTraits> fn_io, IO<TValAction, TValTraits> value_io)
{
    using fn_io_type = IO<TFnAction, TFnTraits>;
    using value_io_type = IO<TValAction, TValTraits>;
    static constexpr auto reason = TTraits::validate_ap(^^fn_io_type, ^^value_io_type);
    static_assert(
        reason.empty(), detail::ap_error_msg<^^fn_io_type, ^^value_io_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::ApAction<fn_io_type, value_io_type>;
        return IO<action_type, TTraits>{action_type{std::move(fn_io), std::move(value_io)}};
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
 * @tparam TFnAction Function IO's action, producing a callable.
 * @tparam TFnTraits Function IO's traits, used only for the function IO's own construction.
 * @tparam TValAction Value IO's action, producing the callable's argument.
 * @tparam TValTraits Value IO's traits, used only for the value IO's own construction.
 * @param fn_io IO producing the callable to apply.
 * @param value_io IO producing the value to apply the callable to.
 * @return IO running the function IO then the value IO and applying the callable to the value
 * with IOTraits<name_cstr>, or detail::ApError if the pair is invalid (which is also
 * rejected by a friendly static_assert).
 */
template <
    char const * name_cstr,
    class TFnAction,
    class TFnTraits,
    class TValAction,
    class TValTraits>
[[nodiscard]] constexpr auto ap(IO<TFnAction, TFnTraits> fn_io, IO<TValAction, TValTraits> value_io)
{
    return ap<IOTraits<name_cstr>>(std::move(fn_io), std::move(value_io));
}

/**
 * Applies a function IO to a value IO when either argument is not an IO, producing a compile-time
 * error.
 *
 * This overload is selected when at least one argument is not an IO type. It exists to provide a
 * friendly static_assert diagnostic naming the offending argument: the static_assert always fails,
 * so compilation never succeeds.
 *
 * Constrained to non-IO arguments so that the IO overloads stay unambiguous when a traits argument
 * is passed explicitly (e.g. `ap<SomeTraits>(io, io)`).
 *
 * @tparam TTraits Traits used to validate the ap and generate the error message.
 * @tparam TFnNotAnIO Type of the invalid (non-IO) function argument.
 * @tparam TValNotAnIO Type of the invalid (non-IO) value argument.
 * @param fn_io Function IO (or non-IO) argument.
 * @param value_io Value IO (or non-IO) argument.
 * @return detail::ApError, like the other overloads, so the return type stays well-formed (e.g.
 * for deduction by `auto`); the static_assert always fails when an argument is not an IO.
 */
template <class TTraits = IOTraits<>, class TFnNotAnIO, class TValNotAnIO>
requires(!detail::is_io(^^TFnNotAnIO) || !detail::is_io(^^TValNotAnIO))
    [[nodiscard]] constexpr auto ap(
        [[maybe_unused]] TFnNotAnIO fn_io, [[maybe_unused]] TValNotAnIO value_io)
{
    static constexpr auto reason = TTraits::validate_ap(^^TFnNotAnIO, ^^TValNotAnIO);
    static_assert(
        reason.empty(), detail::ap_error_msg<^^TFnNotAnIO, ^^TValNotAnIO>(TTraits::name, reason));
    static_assert(!reason.empty(), "io::ap error overload chosen without an error flagged");
    return detail::ApError{};
}
}  // namespace fnfelt::monad::io
