// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/detail.hpp>

#include <doctest/doctest.h>

#include <array>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fnfelt/monad/io/IO.hpp>

namespace
{
/// Incomplete type - has no meaningful invocation result.
struct Incomplete;

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

/// Custom IO name for a custom-traits instantiation.
inline constexpr char custom_io_name[] = "CustomIO";
}  // namespace

TEST_CASE("detail::is_io recognises IO specialisations with any traits")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;
    using fnfelt::monad::io::detail::is_io;

    using CustomIOTraits = IOTraits<custom_io_name>;

    static_assert(is_io(^^IO<int (*)()>));
    static_assert(is_io(^^IO<int (*)(), CustomIOTraits>));
    static_assert(!is_io(^^int));
}

TEST_CASE("detail::all_template_arguments_are_types distinguishes type and non-type arguments")
{
    using fnfelt::monad::io::detail::all_template_arguments_are_types;

    static_assert(all_template_arguments_are_types(^^std::pair<int, int>));
    static_assert(!all_template_arguments_are_types(^^std::array<int, 2>));
}

TEST_CASE("detail::action_value_meta reflects the action's result")
{
    using fnfelt::monad::io::detail::action_value_meta;

    static_assert(is_same_type(action_value_meta(^^int (*)()), ^^int));
    // Non-callable and incomplete actions have no meaningful result.
    static_assert(is_same_type(action_value_meta(^^int), ^^void));
    static_assert(is_same_type(action_value_meta(^^Incomplete), ^^void));
}

TEST_CASE("detail::io_action_meta reflects an IO's wrapped action")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;
    using fnfelt::monad::io::detail::io_action_meta;

    using CustomIOTraits = IOTraits<custom_io_name>;

    // The action reflection is exposed for IOs with any traits.
    static_assert(is_same_type(io_action_meta(^^IO<int (*)()>), ^^int (*)()));
    static_assert(is_same_type(io_action_meta(^^IO<int (*)(), CustomIOTraits>), ^^int (*)()));
    // Non-IO types have no wrapped action.
    static_assert(is_same_type(io_action_meta(^^int), ^^void));
}

TEST_CASE("detail::io_value_meta reflects an IO's action's result")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;
    using fnfelt::monad::io::detail::io_value_meta;

    using CustomIOTraits = IOTraits<custom_io_name>;

    static_assert(is_same_type(io_value_meta(^^IO<int (*)()>), ^^int));
    static_assert(is_same_type(io_value_meta(^^IO<int (*)(), CustomIOTraits>), ^^int));
    // Non-IO types have no value.
    static_assert(is_same_type(io_value_meta(^^int), ^^void));
}

TEST_CASE("detail::is_directly_invocable checks single-argument const lvalue invocability")
{
    using fnfelt::monad::io::detail::is_directly_invocable;
    using fnfelt::monad::io::detail::is_spread_invocable;

    // Direct: continuation takes the value as a single argument.
    static_assert(is_directly_invocable(^^decltype([](int) { return 1; }), ^^int));
    // A spread-only continuation is not directly invocable.
    static_assert(
        !is_directly_invocable(^^decltype([](int, int) { return 1; }), ^^std::pair<int, int>));
    // A directly invocable continuation is not spread invocable on the whole value.
    static_assert(!is_spread_invocable(^^decltype([](int) { return 1; }), ^^int));
}

TEST_CASE("detail::is_spread_invocable checks template-argument-spread invocability")
{
    using fnfelt::monad::io::detail::is_spread_invocable;

    // Spread: continuation takes the value's template arguments as its parameter pack.
    static_assert(
        is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^std::pair<int, int>));
    static_assert(
        is_spread_invocable(^^decltype([](int, int) { return 1; }), ^^std::tuple<int, int>));
    // A direct-only continuation is not spread invocable.
    static_assert(
        !is_spread_invocable(^^decltype([](std::pair<int, int>) {}), ^^std::pair<int, int>));
}

TEST_CASE("detail::short_type_name renders a structurally safe short name")
{
    using fnfelt::monad::io::detail::short_type_name;

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

#endif  // DOCTEST_CONFIG_DISABLE
