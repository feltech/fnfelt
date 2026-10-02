// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file static_string.hpp
 *
 * Short helper for promoting compile-time strings to static storage.
 */
#pragma once

#include <meta>

#include <cstddef>
#include <string_view>

namespace fnfelt::literals
{
/**
 * Promotes a string literal to static storage.
 *
 * Wraps std::define_static_string so the result can be used as a `char const *` template argument,
 * e.g. `IOTraits<"FileIO"_ss>`.
 *
 * @param str String literal characters.
 * @param len Length of the literal, excluding the null terminator.
 * @return Pointer to the null-terminated static copy.
 */
consteval char const * operator""_ss(char const * str, std::size_t len)
{
    return std::define_static_string(std::string_view{str, len});
}
}  // namespace fnfelt::literals
