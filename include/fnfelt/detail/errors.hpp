// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file errors.hpp
 *
 * Common error handling utilities.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>

namespace fnfelt::detail
{
/**
 * Builds a readable diagnostic message for a given type.
 *
 * Place type name after error because types can get long and we want to show the error text to
 * the user as early as possible.
 *
 * Concatenation is used because std::format is not constexpr-usable on gcc-16 libstdc++.
 *
 * @param target_meta Target type that has a problem.
 * @param name Optional component or construct name (e.g. "IO").
 * @param preamble Arbitrary preamble before describing the specific problem.
 * @param reason Suffix naming the specific validation failure.
 * @return The full diagnostic message.
 */
consteval std::string construct_type_error_msg(
    std::meta::info target_meta,
    std::string_view name,
    std::string_view preamble,
    std::string_view const reason)
{
    std::string msg = "fnfelt: ";
    if (!name.empty())
    {
        msg += name;
        msg += ' ';
    }
    msg += preamble;
    msg += reason;
    msg += ": ";
    msg += display_string_of(target_meta);
    return msg;
}
}  // namespace fnfelt::detail
