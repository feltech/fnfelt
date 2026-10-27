// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/select.hpp
 *
 * Conditional effect execution for the IO monad.
 */
#pragma once

#include <meta>

#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
template <class TChoice, class TFallback>
struct SelectProxy;

namespace detail
{
/// Placeholder for a rejected selective composition.
struct SelectError
{
};

/**
 * Validate the logical choice and produced function.
 *
 * @param choice_meta Choice value reflection.
 * @param function_meta Produced function reflection.
 * @return Empty on success, otherwise a diagnostic reason.
 */
consteval std::string_view validate_select_values(
    std::meta::info choice_meta, std::meta::info function_meta)
{
    if (!is_type(choice_meta) || is_reference_type(choice_meta))
    {
        return "choice must produce a std::expected by value";
    }
    choice_meta = dealias(remove_cvref(choice_meta));
    if (!has_template_arguments(choice_meta) || template_of(choice_meta) != ^^std::expected)
    {
        return "choice must produce a std::expected by value";
    }
    auto const args = template_arguments_of(choice_meta);
    auto const value_meta = args[0];
    auto const error_meta = args[1];
    if (is_void_type(value_meta))
    {
        return "choice expected must contain a non-void value";
    }
    if (is_io(value_meta))
    {
        return "choice expected must contain a plain value";
    }
    if (!is_complete_type(value_meta) || !is_move_constructible_type(value_meta))
    {
        return "choice value must be a complete move constructible type";
    }
    if (!is_complete_type(function_meta) ||
        !fnfelt::detail::is_dispatch_invocable(function_meta, error_meta))
    {
        return "fallback function does not accept the choice's error";
    }
    auto const result_meta = fnfelt::detail::dispatch_result_meta(function_meta, error_meta);
    if (!is_same_type(result_meta, value_meta))
    {
        return "fallback function must return the choice's value type by value";
    }
    return {};
}

/**
 * Whether either declared effect requires asynchronous execution.
 *
 * @tparam TChoice Choice IO type.
 * @tparam TFallback Fallback IO type.
 * @return True if either effect is asynchronous.
 */
template <class TChoice, class TFallback>
consteval bool select_is_async()
{
    return action_is_async(io_action_meta(^^TChoice)) ||
        action_is_async(io_action_meta(^^TFallback));
}

/**
 * Reflect the selective action's plain result or asynchronous proxy.
 *
 * @tparam TChoice Choice IO type.
 * @tparam TFallback Fallback IO type.
 * @return Action result reflection.
 */
template <class TChoice, class TFallback>
consteval std::meta::info select_result_meta()
{
    if constexpr (select_is_async<TChoice, TFallback>())
    {
        return ^^SelectProxy<TChoice, TFallback>;
    }
    return template_arguments_of(dealias(remove_cvref(io_value_meta(^^TChoice))))[0];
}

/**
 * Run a choice and execute the fallback only when it needs processing.
 *
 * @tparam TChoice Choice IO type.
 * @tparam TFallback Fallback IO type.
 */
template <class TChoice, class TFallback>
struct SelectAction
{
    /// Plain value or asynchronous proxy.
    using result_type = [:select_result_meta<TChoice, TFallback>():];
    /// Effect producing the completed value or fallback input.
    TChoice choice;
    /// Effect producing a function for the fallback input.
    TFallback fallback;

    /**
     * Execute the choice, then only the required fallback.
     *
     * @param self Stored effects.
     * @return Completed value or asynchronous proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (select_is_async<TChoice, TFallback>())
        {
            return result_type{{FW(self).choice, FW(self).fallback}};
        }
        else
        {
            auto choice = FW(self).choice.build().sync_wait();
            if (choice.has_value())
            {
                return *std::move(choice);
            }
            typename TFallback::value_type fn = FW(self).fallback.build().sync_wait();
            return fnfelt::detail::invoke_dispatch(std::move(fn), std::move(choice).error());
        }
    }
};

/**
 * Describe a rejected selective composition.
 *
 * @tparam choice_meta Choice source reflection.
 * @tparam fallback_meta Fallback source reflection.
 * @param name Result name.
 * @param reason Validation failure.
 * @return Diagnostic text.
 */
template <std::meta::info choice_meta, std::meta::info fallback_meta>
consteval std::string select_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg = "fnfelt: IO select error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += '{';
    fnfelt::detail::append_string_view(msg, maybe_io_name<choice_meta>());
    msg += "(), ";
    fnfelt::detail::append_string_view(msg, maybe_io_name<fallback_meta>());
    msg += "()}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(choice_meta));
    msg += ", ";
    fnfelt::detail::append_string_view(msg, display_string_of(fallback_meta));
    return msg;
}
}  // namespace detail

/**
 * Sequential asynchronous selective execution under the enclosing scheduler.
 *
 * @tparam TChoice Choice IO type.
 * @tparam TFallback Fallback IO type.
 */
template <class TChoice, class TFallback>
struct SelectProxy : detail::TransportProxyTag
{
    /// Completed result type.
    using value_type = typename TChoice::value_type::value_type;
    /// Owned effects.
    std::tuple<TChoice, TFallback> arg;
    /// Run the fallback child only after an unexpected choice.
    static constexpr auto fn = [](auto,
                                  TChoice choice_source,
                                  TFallback fallback) -> lf::task<detail::task_result_t<value_type>>
    {
        auto choice = detail::unpack_result(co_await detail::just_child(std::move(choice_source)));
        if (choice.has_value())
        {
            co_return detail::pack_result<value_type>(*std::move(choice));
        }
        typename TFallback::value_type fn =
            detail::unpack_result(co_await detail::just_child(std::move(fallback)));
        co_return detail::pack_result<value_type>(
            fnfelt::detail::invoke_dispatch(std::move(fn), std::move(choice).error()));
    };

    /// Execute only the required effects during constant evaluation.
    static constexpr auto run_inline = [](TChoice choice_source, TFallback fallback) -> value_type
    {
        auto choice = std::move(choice_source).build().sync_wait();
        if (choice.has_value())
        {
            return *std::move(choice);
        }
        typename TFallback::value_type fn = std::move(fallback).build().sync_wait();
        return fnfelt::detail::invoke_dispatch(std::move(fn), std::move(choice).error());
    };

    /**
     * Retain the declared effects until execution.
     *
     * @param stored Owned effects.
     */
    constexpr explicit SelectProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

namespace detail
{
/**
 * Validation and naming for selective IO execution.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct SelectIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Check choice and fallback source contracts.
     *
     * @param choice_meta Choice IO reflection.
     * @param fallback_meta Fallback IO reflection.
     * @return Empty on success, otherwise a diagnostic reason.
     */
    static consteval std::string_view validate_select(
        std::meta::info choice_meta, std::meta::info fallback_meta)
    {
        if (!is_io(choice_meta))
        {
            return "choice source is not an IO";
        }
        if (!is_io(fallback_meta))
        {
            return "fallback source is not an IO";
        }
        return validate_select_values(io_value_meta(choice_meta), io_value_meta(fallback_meta));
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
