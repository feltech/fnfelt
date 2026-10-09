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
// Doxygen parses this header before transform.hpp (it sorts first), then merges the two overloads'
// documentation and warns about duplicate @param sections. The declarations are redundant for
// documentation (the definitions in transform.hpp are the canonical docs), so hide them from
// Doxygen.
/// @cond

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
/// @endcond

/**
 * Canonical tag marking an IO action as asynchronous.
 *
 * An action is detected as asynchronous when its invocation result derives from this tag and the
 * result exposes a `value_type` alias naming the asynchronous result type. User-defined async
 * proxies derive from it.
 */
struct AsyncProxyTag
{
};

/// @cond
/// Default traits for the IO monad, naming the instantiation used in diagnostics.
template <char const * name_cstr = default_io_name>
struct IOTraits;

template <class TAction, class TTraits = IOTraits<>>
class IO;

/// Run proxy returned by IO's call operator, defined in async.hpp.
template <class TAction>
struct RunProxy;

// Forward declarations for the free and_then functions used by IO's member and_then overloads;
// defined and documented in and_then.hpp. The IOs are taken by forwarding reference so rvalue
// chains move and lvalues are copied, and the member's `self` may be an lvalue or an rvalue.
// Invalid pairs (including non-IO sources) are rejected by validation inside the definitions.
template <class TTraits = IOTraits<>, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

// Forward declarations for the free transform functions used by IO's member transform overloads;
// defined and documented in transform.hpp. The source IO is taken by forwarding reference so rvalue
// chains move and lvalues are copied, and the member's `self` may be an lvalue or an rvalue.
// Invalid pairs (including non-IO sources) are rejected by validation inside the definitions.
//
template <class TTraits = IOTraits<>, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);

template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);
/// @endcond
}  // namespace fnfelt::monad::io
