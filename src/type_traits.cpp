// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/type_traits.hpp>

#include <doctest/doctest.h>

#include <meta>

#include <array>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
/// Test container template, accepting any number of type arguments.
template <class...>
struct Box
{
};

/// Alias template, used to exercise the "is not a class template" condition.
template <class T>
using alias_t = std::vector<T>;

/// Validator that accepts anything, to prove an injected validator drives the same pipeline.
struct AllowAll
{
    consteval std::string_view operator()(
        std::meta::info, std::initializer_list<std::meta::info>) const
    {
        return {};
    }
};
}  // namespace

TEST_CASE("Unspecialise rebinds std containers")
{
    static_assert(std::is_same_v<
                  fnfelt::Unspecialise<std::vector<int>>::specialise<double>,
                  std::vector<double>>);
    static_assert(std::is_same_v<fnfelt::Unspecialise<Box<int>>::specialise<char>, Box<char>>);
}

TEST_CASE("rebind_t alias")
{
    static_assert(std::is_same_v<fnfelt::rebind_t<std::vector<int>, double>, std::vector<double>>);
    static_assert(std::is_same_v<
                  fnfelt::rebind_t<std::optional<int>, std::string>,
                  std::optional<std::string>>);
    static_assert(
        std::is_same_v<fnfelt::rebind_t<std::map<int, double>, int, float>, std::map<int, float>>);
}

TEST_CASE("detail::type_specialisation classifies containers")
{
    using fnfelt::detail::type_specialisation;

    static_assert(type_specialisation<std::vector<int>>);
    static_assert(type_specialisation<std::optional<int>>);
    static_assert(type_specialisation<std::pair<int, double>>);
    static_assert(type_specialisation<Box<int>>);

    static_assert(!type_specialisation<std::array<int, 3>>);
    static_assert(!type_specialisation<int>);
    static_assert(!type_specialisation<void(int)>);
    static_assert(!type_specialisation<std::vector<int> *>);
    static_assert(!type_specialisation<std::vector<int> const &>);
}

TEST_CASE("detail::rebindable reports supported and unsupported containers")
{
    using fnfelt::detail::rebindable;

    // Positive: type-parameter-only specializations with usable replacement arguments.
    static_assert(rebindable<std::vector<int>, double>);
    static_assert(rebindable<std::optional<int>, std::string>);
    static_assert(rebindable<std::pair<int, double>, std::string, float>);
    static_assert(rebindable<Box<int>, char>);

    // Positive: a two-type-argument container can be rebound to two new arguments.
    static_assert(rebindable<std::map<int, double>, int, float>);

    // Negative: not a specialization.
    static_assert(!rebindable<int, double>);
    static_assert(!rebindable<void(int), double>);

    // Negative: cv-qualified, reference and pointer types.
    static_assert(!rebindable<std::vector<int> const, double>);
    static_assert(!rebindable<std::vector<int> &, double>);
    static_assert(!rebindable<std::vector<int> *, double>);

    // Negative: template with non-type parameters.
    static_assert(!rebindable<std::array<int, 3>, double>);

    // Negative: type-parameter-only template but unusable replacement arguments.
    static_assert(!rebindable<std::map<int, double>, float>);
}

namespace
{
/// Constraint proving detail::rebindable is usable in a requires-clause, not only in static_assert.
template <class TContainer>
concept double_rebindable = fnfelt::detail::rebindable<TContainer, double>;
}  // namespace

TEST_CASE("detail::rebindable is usable as a constraint")
{
    static_assert(double_rebindable<std::vector<int>>);
    static_assert(!double_rebindable<int>);
    static_assert(!double_rebindable<std::array<int, 3>>);
}

namespace
{
/// Minimal custom allocator, to pin the allocator-reversion semantics below.
template <class T>
struct TrackingAlloc
{
    using value_type = T;
};
}  // namespace

TEST_CASE("rebind_t replaces all template arguments")
{
    // Intended semantics: rebind replaces ALL template arguments - the custom allocator is
    // discarded and the family default (std::allocator) is restored.
    static_assert(std::is_same_v<
                  fnfelt::rebind_t<std::vector<int, TrackingAlloc<int>>, double>,
                  std::vector<double>>);
}

TEST_CASE("detail::validate_template_of names the first failing condition")
{
    using fnfelt::detail::validate_template_of;

    // Success: a type-parameter-only class template specialization has no reason to report.
    static_assert(validate_template_of(^^std::vector<int>).empty());
    static_assert(validate_template_of(^^std::pair<int, double>).empty());
    static_assert(validate_template_of(^^std::optional<int>).empty());
    static_assert(validate_template_of(^^Box<int>).empty());
    static_assert(validate_template_of(^^Box<>).empty());

    // Failure conditions, in the order the validator checks them.
    static_assert(validate_template_of(^^std::vector) == "is not a type");
    static_assert(validate_template_of(^^std::vector<int> const) == "is cv-qualified");
    static_assert(validate_template_of(^^std::vector<int> volatile) == "is cv-qualified");
    static_assert(validate_template_of(^^std::vector<int> &) == "is a reference type");
    static_assert(validate_template_of(^^std::vector<int> *) == "is a pointer type");
    static_assert(validate_template_of(^^int) == "is not a template");
    // An alias template is transparent as a /type/, but its reflection is not a class template.
    static_assert(validate_template_of(^^alias_t<int>) == "is not a class template");
    static_assert(
        validate_template_of(^^std::array<int, 3>) == "has a non-type template parameter");
}

TEST_CASE("detail::validate_rebind names the first failing condition")
{
    using fnfelt::detail::validate_rebind;

    // Success, including a replacement pack of zero type arguments.
    static_assert(validate_rebind(^^std::vector<int>, {^^double}).empty());
    static_assert(validate_rebind(^^std::map<int, double>, {^^int, ^^float}).empty());
    static_assert(validate_rebind(^^Box<int>, {^^char}).empty());
    static_assert(validate_rebind(^^Box<int>, {}).empty());

    // Failure conditions, in the order the validator checks them.
    static_assert(validate_rebind(^^std::vector, {^^double}) == "is not a type");
    static_assert(validate_rebind(^^std::vector<int> const, {^^double}) == "is cv-qualified");
    static_assert(validate_rebind(^^std::vector<int> volatile, {^^double}) == "is cv-qualified");
    static_assert(validate_rebind(^^std::vector<int> &, {^^double}) == "is a reference type");
    static_assert(validate_rebind(^^std::vector<int> *, {^^double}) == "is a pointer type");
    static_assert(validate_rebind(^^int, {^^double}) == "is not a template");
    static_assert(validate_rebind(^^alias_t<int>, {^^double}) == "is not a class template");
    static_assert(
        validate_rebind(^^std::array<int, 3>, {^^double}) == "has a non-type template parameter");
    static_assert(
        validate_rebind(^^std::map<int, double>, {^^float}) ==
        "cannot be specialized with the requested type arguments");
}

TEST_CASE("detail::construct_type_error_msg composes a readable diagnostic")
{
    using fnfelt::detail::construct_type_error_msg;

    static_assert(
        construct_type_error_msg(^^int, "Name", "preamble: ", "reason") ==
        "fnfelt: Name preamble: reason: int");
}

TEST_CASE("detail::Rebind accepts an injected validator")
{
    static_assert(std::is_same_v<
                  fnfelt::detail::Rebind<std::vector<int>, AllowAll{}, double>::type,
                  std::vector<double>>);
}
#endif  // DOCTEST_CONFIG_DISABLE
