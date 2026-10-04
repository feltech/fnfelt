// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file type_names.hpp
 *
 * Helpers for constructing human-readable type names for diagnostics.
 */
#pragma once

#include <meta>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace fnfelt::detail
{
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
 * Short name of a type for use in diagnostics, when the type has one.
 *
 * Same naming rules as `short_type_name`, but reports the absence of a name instead of the
 * "value" fallback.
 *
 * Never inspects string contents: names come from identifiers (single tokens by the grammar) or
 * fixed literals, so the result can never contain brackets or nested type structure. Template
 * specialisations show only their outer template's name; anonymous class types (i.e. closures)
 * show "lambda"; fundamentals show canonical spellings; anything else without an identifier (e.g.
 * pointers, function pointers, arrays) has no name. Aliases win over the aliased type (e.g.
 * std::string shows "string"), but a cv-ref-qualified alias resolves to its underlying type when
 * the qualifier is dropped (e.g. const std::string& shows "basic_string").
 *
 * @param type_meta Reflection of the type to name.
 * @return A short, structurally safe name for the type, or `std::nullopt` when it has none.
 */
consteval std::optional<std::string_view> maybe_short_type_name(std::meta::info type_meta)
{
    // Load-bearing guard: the queries below throw a hard, non-catchable std::meta::exception for
    // non-type reflections.
    if (!is_type(type_meta))
    {
        return std::nullopt;
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
    std::string_view const fundamental_name = fundamental_type_name(type_meta);
    // Relies on `fundamental_type_name` using "value" only as its not-a-fundamental sentinel, so
    // the sentinel can never collide with a real fundamental name.
    if (fundamental_name == "value")
    {
        return std::nullopt;
    }
    return fundamental_name;
}

/**
 * Short name of a type for use in diagnostic messages.
 *
 * Delegates to `maybe_short_type_name` (which applies the naming rules and their structural-safety
 * guarantees, and reports a missing name as `std::nullopt`), rendering that absence as "value".
 *
 * @param type_meta Reflection of the type to name.
 * @return A short, structurally safe name for the type, "value" when it has no short name.
 */
consteval std::string_view short_type_name(std::meta::info type_meta)
{
    return maybe_short_type_name(type_meta).value_or("value");
}
}  // namespace fnfelt::detail
