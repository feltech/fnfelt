// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/detail/type_names.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
/// Named class for short-name tests - identifier branch works for anonymous-namespace types too.
struct Named
{
};

/// Named scoped enum for short-name tests.
enum class Color
{
    red
};

/// Named class in a nested namespace for short-name tests.
namespace probe_ns
{
struct Nested
{
};
}  // namespace probe_ns

/// Variable reflection for the non-type guard test.
inline constexpr int probe_value = 0;
}  // namespace

TEST_CASE("detail::short_type_name renders a structurally safe short name")
{
    using fnfelt::detail::short_type_name;

    // Named class and enum types show their identifier; nested and anonymous-namespace types too.
    static_assert(short_type_name(^^Named) == "Named");
    static_assert(short_type_name(^^probe_ns::Nested) == "Nested");
    static_assert(short_type_name(^^Color) == "Color");

    // Template specialisations show only the outer template's name, so nested arguments don't leak.
    static_assert(short_type_name(^^std::vector<int>) == "vector");
    // clang-format off
    static_assert(short_type_name(^^std::map<int, std::vector<int> >) == "map");
    // clang-format on

    // Aliases win over the aliased type.
    static_assert(short_type_name(^^std::string) == "string");
    // A cv-ref-qualified alias resolves to its underlying type, dropping the alias name.
    static_assert(short_type_name(^^std::string const &) == "basic_string");

    // Anonymous class types are closures.
    static_assert(short_type_name(^^decltype([] { return 1; })) == "lambda");

    // Fundamentals show canonical spellings, not compiler display strings (e.g. gcc's
    // "long long int").
    static_assert(short_type_name(^^int) == "int");
    static_assert(short_type_name(^^void) == "void");
    // NOLINTNEXTLINE(runtime/int) - canonical spelling check needs the fundamental C keyword.
    static_assert(short_type_name(^^long long) == "long long");
    static_assert(short_type_name(^^std::nullptr_t) == "nullptr_t");
    static_assert(short_type_name(^^float) == "float");

    // Pointers, function pointers and arrays have no identifier and fall back to "value".
    static_assert(short_type_name(^^int *) == "value");
    static_assert(short_type_name(^^int (*)()) == "value");
    static_assert(short_type_name(^^int[3]) == "value");

    // Cv-ref qualifiers are dropped: the *name* of the type is requested, not its signature.
    static_assert(short_type_name(^^int &) == "int");
    static_assert(short_type_name(^^int const) == "int");
    // A cv-qualified named class still hits the identifier branch.
    static_assert(short_type_name(^^Named const) == "Named");
}

TEST_CASE("detail::maybe_short_type_name reports types without a short name")
{
    using fnfelt::detail::maybe_short_type_name;

    // Named class and enum types report their identifier wrapped in an optional.
    static_assert(maybe_short_type_name(^^Named) == std::optional<std::string_view>{"Named"});
    static_assert(
        maybe_short_type_name(^^probe_ns::Nested) == std::optional<std::string_view>{"Nested"});
    static_assert(maybe_short_type_name(^^Color) == std::optional<std::string_view>{"Color"});

    // Template specialisations and aliases still report their short names.
    static_assert(
        maybe_short_type_name(^^std::vector<int>) == std::optional<std::string_view>{"vector"});
    static_assert(
        maybe_short_type_name(^^std::string) == std::optional<std::string_view>{"string"});

    // Fundamentals report their canonical spellings.
    static_assert(maybe_short_type_name(^^int) == std::optional<std::string_view>{"int"});
    // The reflection is hoisted into a variable so the assertion fits one line and the C spelling
    // of the type needs a single suppression.
    constexpr std::meta::info long_long{^^long long};  // NOLINT(runtime/int)
    static_assert(maybe_short_type_name(long_long) == std::optional<std::string_view>{"long long"});

    // Cv-ref qualifiers are dropped, as for short_type_name.
    static_assert(maybe_short_type_name(^^int &) == std::optional<std::string_view>{"int"});
    static_assert(maybe_short_type_name(^^Named const) == std::optional<std::string_view>{"Named"});

    // Pointers, function pointers and arrays have no identifier: nullopt replaces the "value"
    // fallback.
    static_assert(!maybe_short_type_name(^^int *).has_value());
    static_assert(!maybe_short_type_name(^^int (*)()).has_value());
    static_assert(!maybe_short_type_name(^^int[3]).has_value());

    // Non-type reflections (here of a variable) hit the is_type guard, yielding nullopt instead of
    // a hard error.
    static_assert(!maybe_short_type_name(^^probe_value).has_value());
}

#endif  // DOCTEST_CONFIG_DISABLE
