// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/type_traits.hpp>

#include <string_view>

namespace
{
/// Stub validator returning a fixed reason, to isolate the diagnostic plumbing.
struct StubValidator
{
    consteval std::string_view operator()(std::meta::info) const
    {
        return "stub validator reason";
    }
};
}  // namespace

// Unspecialise<..>::specialise<..> plumbs its validator into
// detail::template_of_if_valid. Passing a stub validator here proves the message plumbing fires
// exactly one diagnostic; the default validator's individual conditions are unit-tested in
// tests/test_type_traits.cpp.

using Rebound = fnfelt::Unspecialise<int, StubValidator{}>::specialise<double>;

static_assert(sizeof(Rebound) >= 0);