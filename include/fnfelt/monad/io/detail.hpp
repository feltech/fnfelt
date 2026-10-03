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
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/detail/fwd.hpp>
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
 * Append a view to a string one character at a time.
 *
 * `operator+=` accepts `std::string_view` and is well-formed and constant-evaluable in general;
 * this helper exists for the gcc 16.2 boundary described below. Ordering-sensitive diagnostics
 * append through here so that boundary cannot be hit.
 *
 * @param target String to append to.
 * @param view Characters to append.
 */
consteval void append_string_view(std::string & target, std::string_view view)
{
    // Under `-fsanitize=undefined`, gcc 16.2 refuses to constant-evaluate
    // `std::string::operator+=(std::string_view)` when the view is backed by a non-type template
    // parameter string (as used by diagnostic names) and the target grows past its small-string
    // buffer: `_M_mutate`'s null-data-pointer test (`if (__s && __len2)`) stops being a constant
    // expression. An intervening append of a non-NTP-backed view masks it, so the failure is
    // append-order dependent. Copying the characters sidesteps it.
    for (char const character : view)
    {
        target.push_back(character);
    }
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
 * Check whether a callable is directly invocable with a value when the callable is invoked as an
 * rvalue.
 *
 * The stored callable is moved out and invoked as an rvalue on the public execution path, so this
 * models the receiver's value category (bare-type prvalue semantics) rather than a const lvalue.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type.
 * @return True if the function accepts the value as a single argument.
 */
consteval bool is_directly_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    return is_invocable_type(receiver_meta, {value_meta});
}

/**
 * Check whether a callable is invocable with a spread value (std::apply) when the callable is
 * invoked as an rvalue.
 *
 * The stored callable is moved out and invoked as an rvalue on the public execution path, so this
 * models the receiver's value category (bare-type prvalue semantics) rather than a const lvalue.
 *
 * Only `std::pair` and `std::tuple` specialisations are spread. `std::apply` is the run-time
 * mechanism, so eligibility must match what it can actually unpack; these are also the tuple-like
 * types whose elements the reflection models. Other tuple-like types (e.g. `std::array`) are not
 * supported yet.
 *
 * @param receiver_meta Reflection of the callable type.
 * @param value_meta Reflection of the value type produced by the source IO.
 * @return True if the continuation accepts the value's template arguments as its parameter pack.
 */
consteval bool is_spread_invocable(std::meta::info receiver_meta, std::meta::info value_meta)
{
    // Alias reflections report `has_template_arguments` false, so dealias first.
    value_meta = dealias(value_meta);
    if (!has_template_arguments(value_meta))
    {
        return false;
    }
    // `template_of` comparison is the same idiom as `is_io`; restrict spreading to the pair/tuple
    // protocol that `std::apply` actually unpacks.
    std::meta::info const value_template = template_of(value_meta);
    if (value_template != ^^std::pair && value_template != ^^std::tuple)
    {
        return false;
    }
    return is_invocable_type(receiver_meta, template_arguments_of(value_meta));
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
        return [:io_meta:] ::traits::name;
    }

    return "<unknown>";
}
}  // namespace detail
}  // namespace fnfelt::monad::io
