// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file detail.hpp
 *
 * Generic implementation details for the IO monad.
 *
 * Reflection helper predicates shared by the IO monad's operations.
 */
#pragma once

#include <meta>

#include <array>
#include <string_view>

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
 * Check whether a reflection is of an empty class type.
 *
 * A stateless object holds no state: it has no non-static data members in itself or in any base.
 * Virtual functions and virtual bases also count as state for this check. This is how a stateless
 * callable (e.g. a non-capturing lambda) is recognised.
 *
 * @param type_meta Reflection of the type to check.
 * @return True if the type is a complete, non-polymorphic class type with no non-static data
 * members anywhere.
 */
consteval bool is_empty_object(std::meta::info type_meta)
{
    // Member queries do not see through cv-ref qualifiers, so strip them first.
    type_meta = dealias(remove_cvref(type_meta));
    // `is_complete_type` returns false (rather than throwing) for non-type reflections, so it
    // guards the `is_class_type` category query.
    if (!is_complete_type(type_meta) || !is_class_type(type_meta))
    {
        return false;
    }
    // `is_empty_type` reports no non-static data members, bases included. The obvious
    // `nonstatic_data_members_of` walk does NOT work for closures on gcc 16.2: it reports zero
    // members for a capturing lambda, so a stateful async function would slip through.
    return is_empty_type(type_meta);
}

/**
 * Check whether an action is asynchronous, i.e. its invocation result derives from
 * @c AsyncProxyTag.
 *
 * @param action_meta Reflection of the action type.
 * @return True if the action's invocation result derives from @c AsyncProxyTag.
 */
consteval bool action_is_async(std::meta::info action_meta)
{
    action_meta = dealias(remove_cvref(action_meta));
    // Invocability queries are a hard, non-catchable gcc error for incomplete types.
    if (!is_complete_type(action_meta) || !is_invocable_type(action_meta, {}))
    {
        return false;
    }
    std::meta::info const result_meta = remove_cvref(invoke_result(action_meta, {}));
    // `is_base_of_type` is a hard error for an incomplete derived type.
    if (!is_complete_type(result_meta) || !is_class_type(result_meta))
    {
        return false;
    }
    // Belt-and-braces for the remaining throwing paths.
    try
    {
        return is_base_of_type(^^AsyncProxyTag, result_meta);
    }
    catch (std::meta::exception const &)
    {
        return false;
    }
}

/**
 * Find the `value_type` alias an async proxy exposes.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return Reflection of the alias member, or `void` if no such alias is found.
 */
consteval std::meta::info async_proxy_value_alias(std::meta::info proxy_meta)
{
    // Querying members of an incomplete type is a hard error, so guard completeness first.
    if (!is_complete_type(proxy_meta) || !is_class_type(proxy_meta))
    {
        return ^^void;
    }
    // `members_of` does not list inherited members, so bases are walked too.
    for (std::meta::info const member :
         members_of(proxy_meta, std::meta::access_context::unprivileged()))
    {
        if (is_type_alias(member) && has_identifier(member) &&
            identifier_of(member) == "value_type")
        {
            return member;
        }
    }
    for (std::meta::info const base :
         bases_of(proxy_meta, std::meta::access_context::unprivileged()))
    {
        std::meta::info const found = async_proxy_value_alias(type_of(base));
        if (is_type_alias(found))
        {
            return found;
        }
    }
    return ^^void;
}

/**
 * Check whether an async proxy exposes a `value_type` alias.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return True if the proxy (or a base) has a `value_type` alias.
 */
consteval bool async_proxy_has_value(std::meta::info proxy_meta)
{
    // The `void` placeholder is not an alias member, so `is_type_alias` discriminates it.
    return is_type_alias(async_proxy_value_alias(proxy_meta));
}

/**
 * Reflection of the value an async proxy produces when run.
 *
 * @param proxy_meta Reflection of the proxy type.
 * @return Reflection of the `value_type` alias target, or `void` if no alias is found.
 */
consteval std::meta::info async_proxy_value_meta(std::meta::info proxy_meta)
{
    // `type_of` throws for alias reflections on gcc 16.2, so dealias before use.
    return dealias(async_proxy_value_alias(proxy_meta));
}

/**
 * Reflection of the value an action produces when run.
 *
 * An asynchronous action's value comes from its proxy's `value_type` alias.
 *
 * @param action_meta Reflection of the action type.
 * @return Reflection of the action's invocation result, or `void` if the action is incomplete or
 * not callable with no arguments.
 */
consteval std::meta::info action_value_meta(std::meta::info action_meta)
{
    // Invocability queries are a hard error for incomplete types, so guard completeness first.
    if (is_complete_type(action_meta) && is_invocable_type(action_meta, {}))
    {
        if (action_is_async(action_meta))
        {
            return async_proxy_value_meta(remove_cvref(invoke_result(action_meta, {})));
        }
        return invoke_result(action_meta, {});
    }
    return ^^void;
}

/**
 * Reflection of the action type wrapped by an IO.
 *
 * @param io_meta Reflection of the (possibly IO) type.
 * @return Reflection of the wrapped action, or `void` if the type is not an IO.
 */
consteval std::meta::info io_action_meta(std::meta::info io_meta)
{
    // `template_arguments_of` throws for non-template types, and a consteval throw is a hard
    // compile error, so `void` is returned as a placeholder for non-IO types.
    if (!is_io(io_meta))
    {
        return ^^void;
    }
    // Extract the scalar inside the consteval function: vector<info> results allocate and cannot
    // escape a constant-evaluated context.
    return template_arguments_of(dealias(remove_cvref(io_meta)))[0];
}

/**
 * Reflection of the value an IO's action produces when run.
 *
 * @param io_meta Reflection of the (possibly IO) type.
 * @return Reflection of the wrapped action's invocation result, or `void` if the type is not an IO
 * or its action produces no value.
 */
consteval std::meta::info io_value_meta(std::meta::info io_meta)
{
    return action_value_meta(io_action_meta(io_meta));
}

/**
 * Check whether a callable is directly invocable with a value when the callable is invoked as a
 * const lvalue.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type.
 * @return True if the function accepts the value as a single argument.
 */
consteval bool is_directly_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    return is_invocable_type(add_lvalue_reference(add_const(receiver_meta)), {value_meta});
}

/**
 * Check whether a callable, invoked as the given receiver type, is invocable with a value's
 * template arguments spread as its parameter pack.
 *
 * Values that are specialisations with all-type template arguments (e.g. `std::pair<int, int>`,
 * `std::tuple<int, int>`) have their arguments spread into the invocation.
 *
 * @note Spreading follows the value's template arguments, so non-tuple specialisations whose
 * template arguments are all types (e.g. `std::vector<int>` spreads as `int,
 * std::allocator<int>`) are also treated as spread-callable; `std::apply` will only accept
 * genuinely tuple-like values at run time.
 *
 * @param receiver_meta Reflection of the receiver the callable is invoked as (e.g. a const lvalue
 * for a stored member, or the bare type for a moved local).
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the callable accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable_as(std::meta::info receiver_meta, std::meta::info value_meta)
{
    // Alias reflections report `has_template_arguments` false, so dealias first.
    value_meta = dealias(value_meta);
    return has_template_arguments(value_meta) && all_template_arguments_are_types(value_meta) &&
        is_invocable_type(receiver_meta, template_arguments_of(value_meta));
}

/**
 * Check whether a callable is invocable with a spread value (std::apply) when the callable is
 * invoked as a const lvalue.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the continuation accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    return is_spread_invocable_as(add_lvalue_reference(add_const(receiver_meta)), value_meta);
}

/**
 * Reflection of the IO returned by a source IO's continuation.
 *
 * The continuation is applied with the source IO's value directly when possible, otherwise the
 * value is spread into it (direct invocation wins over spreading), mirroring the run-time dispatch.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the continuation's IO, or `void` if not determinable.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_continuation_io_meta()
{
    constexpr std::meta::info value_meta = io_value_meta(^^TSourceIO);
    if constexpr (is_directly_invocable(^^TContinuation, value_meta))
    {
        return invoke_result(^^TContinuation, {value_meta});
    }
    if constexpr (is_spread_invocable(^^TContinuation, value_meta))
    {
        return invoke_result(^^TContinuation, template_arguments_of(dealias(value_meta)));
    }
    return ^^void;
}

/**
 * Reflection of the value an and_then action produces when run.
 *
 * The invocation result is the continuation's IO, whose own value (async-transparent) is the final
 * value.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TContinuation Continuation taking the source's value and returning an IO.
 * @return Reflection of the value the action produces.
 */
template <class TSourceIO, class TContinuation>
consteval std::meta::info and_then_result_meta()
{
    return io_value_meta(and_then_continuation_io_meta<TSourceIO, TContinuation>());
}

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
consteval std::meta::info ap_result_meta()
{
    constexpr std::meta::info fn_value_meta = io_value_meta(^^TFnIO);
    constexpr std::meta::info value_meta = io_value_meta(^^TValueIO);
    if constexpr (is_invocable_type(fn_value_meta, {value_meta}))
    {
        return invoke_result(fn_value_meta, {value_meta});
    }
    if constexpr (is_spread_invocable_as(fn_value_meta, value_meta))
    {
        return invoke_result(fn_value_meta, template_arguments_of(dealias(value_meta)));
    }
    return ^^void;
}

/**
 * Canonical name of a fundamental type.
 *
 * Fundamental types have no identifier and their display strings are compiler-specific.
 *
 * @param type_meta Reflection of the (cv-ref-stripped) type.
 * @return Canonical name (e.g. "long long"), or "value" if not a fundamental type.
 */
consteval std::string_view fundamental_type_name(std::meta::info type_meta)
{
    struct Entry
    {
        std::meta::info meta;
        std::string_view name;
    };
    // Fundamental types have no identifier and their display strings are compiler-specific, so they
    // are matched against this closed table with fixed spellings.
    // Canonical spellings require the fundamental C keywords, which cpplint would otherwise flag.
    // NOLINTBEGIN(runtime/int)
    constexpr std::array candidates{
        Entry{^^void, "void"},
        Entry{^^bool, "bool"},
        Entry{^^char, "char"},
        Entry{^^signed char, "signed char"},
        Entry{^^unsigned char, "unsigned char"},
        Entry{^^wchar_t, "wchar_t"},
        Entry{^^char8_t, "char8_t"},
        Entry{^^char16_t, "char16_t"},
        Entry{^^char32_t, "char32_t"},
        Entry{^^short, "short"},
        Entry{^^unsigned short, "unsigned short"},
        Entry{^^int, "int"},
        Entry{^^unsigned int, "unsigned int"},
        Entry{^^long, "long"},
        Entry{^^unsigned long, "unsigned long"},
        Entry{^^long long, "long long"},
        Entry{^^unsigned long long, "unsigned long long"},
        Entry{^^float, "float"},
        Entry{^^double, "double"},
        Entry{^^long double, "long double"},
        Entry{^^decltype(nullptr), "nullptr_t"},
    };
    // NOLINTEND(runtime/int)
    for (Entry const & candidate : candidates)
    {
        if (is_same_type(candidate.meta, type_meta))
        {
            return candidate.name;
        }
    }
    return "value";
}

/**
 * Short name of a type for use in diagnostic messages.
 *
 * Never inspects string contents: names come from identifiers (single tokens by the grammar) or
 * fixed literals, so the result can never contain brackets or nested type structure. Template
 * specialisations show only their outer template's name; anonymous class types (i.e. closures)
 * show "lambda"; fundamentals show canonical spellings; everything else shows "value". Aliases
 * win over the aliased type (e.g. std::string shows "string"), but a cv-ref-qualified alias
 * resolves to its underlying type when the qualifier is dropped (e.g. const std::string& shows
 * "basic_string").
 *
 * @param type_meta Reflection of the type to name.
 * @return A short, structurally safe name for the type.
 */
consteval std::string_view short_type_name(std::meta::info type_meta)
{
    if (!is_type(type_meta))
    {
        return "value";
    }
    if (is_type_alias(type_meta) && has_identifier(type_meta))
    {
        return identifier_of(type_meta);
    }
    type_meta = remove_cvref(type_meta);
    type_meta = dealias(type_meta);
    if (has_identifier(type_meta))
    {
        return identifier_of(type_meta);
    }
    if (has_template_arguments(type_meta) && has_identifier(template_of(type_meta)))
    {
        return identifier_of(template_of(type_meta));
    }
    if (is_class_type(type_meta))
    {
        return "lambda";
    }
    return fundamental_type_name(type_meta);
}

/**
 * Name of IO.
 *
 * @tparam io_meta IO reflection to check.
 * @return "<unknown>" if the IO reflection is not an IO, otherwise the IO's name.
 */
template <std::meta::info io_meta>
constexpr std::string_view maybe_io_name()
{
    if constexpr (is_io(io_meta))
    {
        return [:io_meta:] ::name;
    }

    return "<unknown>";
}
}  // namespace detail
}  // namespace fnfelt::monad::io
