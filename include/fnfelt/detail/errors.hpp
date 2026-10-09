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
 * Append a view to a string one character at a time.
 *
 * Appending one character at a time is constant-evaluable where `operator+=` is not.
 *
 * @param target String to append to.
 * @param view Characters to append.
 */
consteval void append_string_view(std::string & target, std::string_view view)
{
    // `std::string::operator+=(std::string_view)` compares the view's data pointer against null.
    // When that data comes from a non-type template parameter string, gcc 16.2 refuses to
    // constant-evaluate the comparison under `-fsanitize=undefined`; copying the characters
    // sidesteps it.
    for (char const character : view)
    {
        target.push_back(character);
    }
}

/**
 * Builds a readable diagnostic message for a given type.
 *
 * Place type name after error because types can get long and we want to show the error text to
 * the user as early as possible.
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
    // Concatenation is used because std::format is not constexpr-usable on gcc-16 libstdc++.
    std::string msg = "fnfelt: ";
    if (!name.empty())
    {
        append_string_view(msg, name);
        msg += ' ';
    }
    append_string_view(msg, preamble);
    append_string_view(msg, reason);
    msg += ": ";
    append_string_view(msg, display_string_of(target_meta));
    return msg;
}
}  // namespace fnfelt::detail
