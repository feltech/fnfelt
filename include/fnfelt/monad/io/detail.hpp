// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file detail.hpp
 *
 * Implementation details for the IO monad.
 *
 * Reflection helper predicates and the bind action used by IO::bind.
 */
#pragma once

#include <meta>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/fwd.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Check whether a reflection is of a (cv-ref-stripped) specialisation of IO, with any traits.
 *
 * @param type_meta Reflection of the type to check.
 * @return True if the type is an IO specialisation.
 */
consteval bool is_io(std::meta::info type_meta)
{
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^IO;
}

/**
 * Check whether every template argument of a specialisation is a type.
 *
 * Non-type template parameters (e.g. `std::array<T, N>`'s `N`) cannot be spread into a kleisli
 * invocation, and `template_arguments_of` on an alias must be dealiased before use.
 *
 * @param target_meta Reflection of a type specialisation.
 * @return True if all template arguments are types.
 */
consteval bool all_template_arguments_are_types(std::meta::info target_meta)
{
    for (std::meta::info const arg_meta : template_arguments_of(target_meta))
    {
        if (!is_type(arg_meta))
        {
            return false;
        }
    }
    return true;
}

/**
 * Reflection of the value an action produces when run.
 *
 * Incomplete actions (or non-callable types) have no meaningful result, but invocability queries
 * are a hard error for incomplete types, so `void` is returned as a placeholder in that case.
 *
 * @param action_meta Reflection of the action type.
 * @return Reflection of the action's invocation result, or `void` if the action is incomplete or
 * not callable with no arguments.
 */
consteval std::meta::info action_value_meta(std::meta::info action_meta)
{
    if (is_complete_type(action_meta) && is_invocable_type(action_meta, {}))
    {
        return invoke_result(action_meta, {});
    }
    return ^^void;
}

/**
 * Check whether a kleisli is directly invocable with the source IO's value.
 *
 * The kleisli is stored as a const member and invoked as a const lvalue, so const-lvalue receiver
 * semantics are checked (`add_lvalue_reference(add_const(...))`).
 *
 * @param kleisli_meta Reflection of the kleisli type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the kleisli accepts the value as a single argument.
 */
consteval bool is_directly_invocable(std::meta::info kleisli_meta, std::meta::info value_meta)
{
    return is_invocable_type(add_lvalue_reference(add_const(kleisli_meta)), {value_meta});
}

/**
 * Check whether a kleisli is invocable with the source IO's value spread as several arguments.
 *
 * Values that are specialisations with all-type template arguments (e.g. `std::pair<int, int>`,
 * `std::tuple<int, int>`) have their arguments spread into the kleisli invocation. The value
 * reflection is dealiased first because alias reflections report `has_template_arguments` false.
 *
 * @note Spreading follows the value's template arguments, so non-tuple specialisations whose
 * template arguments are all types (e.g. `std::vector<int>` spreads as `int,
 * std::allocator<int>`) are also treated as spread-callable, matching the legacy `applicable_with`
 * semantics; `std::apply` will only accept genuinely tuple-like values at run time.
 *
 * @param kleisli_meta Reflection of the kleisli type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the kleisli accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable(std::meta::info kleisli_meta, std::meta::info value_meta)
{
    value_meta = dealias(value_meta);
    return has_template_arguments(value_meta) && all_template_arguments_are_types(value_meta) &&
        is_invocable_type(
               add_lvalue_reference(add_const(kleisli_meta)), template_arguments_of(value_meta));
}

/**
 * Tombstone returned by `IO::bind` when the kleisli is invalid.
 *
 * The invalid bind is already rejected by a friendly static_assert; this type exists only to keep
 * the return type well-formed and must never be used.
 */
struct BindError
{
};

/**
 * Action that runs a source IO and feeds its value to a kleisli continuation.
 *
 * The continuation is invoked with the value directly when possible, otherwise the value is spread
 * into the continuation as if by `std::apply` (direct invocation wins over spreading). The
 * continuation is a const member, so it must be callable as a const lvalue.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TKleisli Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TKleisli>
struct BindAction
{
    /// Source IO to run.
    TSourceIO source;
    /// Continuation applied to the source's value.
    TKleisli kleisli;

    /**
     * Run the source IO, apply the continuation to its value, then run the resulting IO.
     *
     * @return The final value.
     */
    constexpr auto operator()() const
    {
        using source_value = typename TSourceIO::value;
        auto input = source();
        if constexpr (is_directly_invocable(^^TKleisli, ^^source_value))
        {
            auto next = kleisli(std::move(input));
            return next();
        }
        if constexpr (!is_directly_invocable(^^TKleisli, ^^source_value))
        {
            auto next = std::apply(kleisli, std::move(input));
            return next();
        }
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io
