// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../state_io.hpp"
/**
 * @file monad/state_io/detail/ap.hpp
 *
 * Sequential applicative application with state threading.
 */
#pragma once
#include <meta>

#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/state_io/detail/bind.hpp>
#include <fnfelt/monad/state_io/detail/transform.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io::detail
{
/// Placeholder for rejected applicative application.
struct ApError
{
};

/**
 * Invoke a produced function on a logical value.
 *
 * @tparam TFunction Function type.
 */
template <class TFunction>
struct ApplyFunction
{
    /// Produced function.
    TFunction fn;
    /**
     * Apply the function, spreading only when direct invocation is unavailable.
     *
     * @param self Function holder.
     * @param value Logical value.
     * @return Function result.
     */
    constexpr auto operator()(this auto && self, auto && value)
    {
        return fnfelt::detail::invoke_dispatch(FW(self).fn, FW(value));
    }
};

/**
 * Run the value computation after obtaining its function and state.
 *
 * @tparam TState State type.
 * @tparam TValueSource Value computation.
 */
template <class TState, class TValueSource>
struct ApplyValue
{
    /// Value computation, materialised only after function success.
    TValueSource source;
    /**
     * Bind updated state and apply the produced function.
     *
     * @param self Value computation holder.
     * @param result Function and updated state.
     * @return IO producing the application result and final state.
     */
    constexpr auto operator()(this auto && self, auto && result)
    {
        using function_type = std::remove_reference_t<decltype(result.first)>;
        return bind_state<TState>(FW(self).source, FW(result).second)
            .transform(MapValue<ApplyFunction<function_type>>{{FW(result).first}});
    }
};

/**
 * Sequentially run a function and value computation.
 *
 * @tparam TState State type.
 * @tparam TFunctionSource Function computation.
 * @tparam TValueSource Value computation.
 */
template <class TState, class TFunctionSource, class TValueSource>
struct ApAction
{
    /// Function computation.
    TFunctionSource function_source;
    /// Value computation.
    TValueSource value_source;
    /**
     * Start sequential application with owned state.
     *
     * @param self Action to consume.
     * @param state Initial state.
     * @return IO producing applied value and final state.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return bind_state<TState>(FW(self).function_source, std::move(state))
            .and_then(ApplyValue<TState, TValueSource>{FW(self).value_source});
    }
};

/**
 * Validate sequential applicative application.
 *
 * @tparam name_cstr Diagnostic name.
 */
template <char const * name_cstr>
struct ApStateIOTraits : CreateStateIOTraits<name_cstr>
{
    /**
     * Check source state agreement and logical-value invocability.
     *
     * @param function_meta Function StateIO reflection.
     * @param value_meta Value StateIO reflection.
     * @return Empty on success, otherwise a diagnostic reason.
     */
    static consteval std::string_view validate_ap(
        std::meta::info function_meta, std::meta::info value_meta)
    {
        if (!is_state_io(function_meta) || !is_state_io(value_meta))
        {
            return "function and value sources must be StateIOs";
        }
        if (!is_same_type(state_io_state_meta(function_meta), state_io_state_meta(value_meta)))
        {
            return "function and value sources must have the same state type";
        }
        auto const fn_meta = state_io_value_meta(function_meta);
        auto const arg_meta = state_io_value_meta(value_meta);
        if (is_void_type(fn_meta) || is_void_type(arg_meta) ||
            !fnfelt::detail::is_dispatch_invocable(fn_meta, arg_meta))
        {
            return "produced function does not accept the value source's logical value";
        }
        auto const result_meta = fnfelt::detail::dispatch_result_meta(fn_meta, arg_meta);
        if (is_void_type(result_meta) || is_reference_type(result_meta) ||
            io::detail::is_io(result_meta) || is_state_io(result_meta))
        {
            return "produced function must return a plain value by value";
        }
        if (!is_complete_type(result_meta) || !is_move_constructible_type(result_meta))
        {
            return "produced function result must be a complete move constructible type";
        }
        return {};
    }
};
}  // namespace fnfelt::monad::state_io::detail

#include <fnfelt/macros_pop.hpp>
