// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file fwd.hpp
 *
 * Forward declarations for the IO monad, including its traits.
 */
#pragma once

#include <meta>

#include <fnfelt/static_string.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/// Check whether a reflection is of a (cv-ref-stripped) specialisation of IO, with any traits.
consteval bool is_io(std::meta::info type_meta);
}  // namespace detail

/// Default name for IO monad diagnostics.
inline constexpr char default_io_name[] = "IO";  // NOLINT(*-avoid-c-arrays)

/// Default traits for the IO monad, naming the instantiation used in diagnostics.
template <char const * name_cstr = default_io_name>
struct IOTraits;

template <class TAction, class TTraits = IOTraits<>>
class IO;

// Forward declarations for the free and_then functions used by IO's member and_then overloads;
// defined and documented in and_then.hpp. The IOs are taken by forwarding reference so rvalue
// chains move and lvalues are copied, and the member's `self` may be an lvalue or an rvalue.
// Invalid pairs (including non-IO sources) are rejected by validation inside the definitions.
template <class TTraits = IOTraits<>, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);
}  // namespace fnfelt::monad::io
