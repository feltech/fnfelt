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
}  // namespace fnfelt::detail
