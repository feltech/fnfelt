// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file fwd.hpp
 *
 * Forward declarations for the IO monad.
 */
#pragma once
// Doxygen parses this header before transform.hpp (it sorts first), then merges the two overloads'
// documentation and warns about duplicate @param sections. The declarations are redundant for
// documentation (the definitions in transform.hpp are the canonical docs), so hide them from
// Doxygen.
/// @cond

#include <meta>

#include <fnfelt/monad/io/detail/fwd.hpp>
#include <fnfelt/static_string.hpp>

namespace fnfelt::monad::io
{
// Declare IO before including detail/create.hpp: the detail headers reflect on it (^^IO), and the
// include cycle fwd.hpp -> detail/create.hpp -> detail.hpp -> fwd.hpp resolves through this early
// declaration plus the pragma-once guards.
template <class TAction, class TTraits>
class IO;
}  // namespace fnfelt::monad::io

namespace fnfelt::monad::io
{
namespace detail
{
/// Default name for IO monad diagnostics.
inline constexpr char default_io_name[] = "IO";
}  // namespace detail

/// @endcond
/**
 * Canonical tag marking an IO action as asynchronous.
 *
 * An action is detected as asynchronous when its invocation result derives from this tag and the
 * result exposes a `value_type` alias naming the asynchronous result type. User-defined async
 * proxies derive from it.
 *
 * Detection (tag plus `value_type`) determines IO typing, but acceptance by the default traits as
 * an executable IO is a stronger contract: the proxy must also follow the execution protocol.
 * Arguments are stored in a public member named `arg` whose type is a `std::tuple`
 * specialisation, and are spread into a stateless static entry `fn`, invoked as `fn(self, args...)`
 * and returning an `lf::task`. Custom traits may relax this by overriding `validate_async_proxy`
 * (see CreateIOTraits).
 */
struct AsyncProxyTag
{
};

/// @cond
/// Traits validating an IO's action, naming the instantiation used in diagnostics.
template <char const * name_cstr = detail::default_io_name>
struct CreateIOTraits;

/// Traits validating a value lifted by `io::just`.
template <char const * name_cstr = detail::default_io_name>
struct JustIOTraits;

/// Traits validating the source IO and continuation function provided to `io::and_then`.
template <char const * name_cstr = detail::default_io_name>
struct AndThenIOTraits;

/// Traits validating the source IO and transformer function provided to `io::transform`.
template <char const * name_cstr = detail::default_io_name>
struct TransformIOTraits;

/// Traits validating the IO-wrapped function and value provided to `io::ap`.
template <char const * name_cstr = detail::default_io_name>
struct ApIOTraits;

/// Traits validating a tuple of IOs provided to `io::sequence`.
template <char const * name_cstr = detail::default_io_name>
struct SequenceIOTraits;

/// Traits validating the stateless coroutine function provided to `io::create_async`.
template <char const * name_cstr = detail::default_io_name>
struct CreateAsyncIOTraits;

template <class TAction, class TTraits = CreateIOTraits<>>
class IO;

/// Run proxy returned by IO's call operator, defined in async.hpp.
template <class TAction>
struct RunProxy;

// Forward declarations for the free create factory functions; defined and documented in
// create.hpp. The action is taken by forwarding reference so lvalues are copied and rvalues are
// moved.
template <class TTraits = CreateIOTraits<>, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

template <char const * name_cstr, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

// Forward declarations for the free and_then functions used by IO's member and_then overloads;
// defined and documented in and_then.hpp. The IOs are taken by forwarding reference so rvalue
// chains move and lvalues are copied, and the member's `self` may be an lvalue or an rvalue.
// Invalid pairs (including non-IO sources) are rejected by validation inside the definitions.
template <class TTraits = AndThenIOTraits<>, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

// Forward declarations for the free transform functions used by IO's member transform overloads;
// defined and documented in transform.hpp. The source IO is taken by forwarding reference so rvalue
// chains move and lvalues are copied, and the member's `self` may be an lvalue or an rvalue.
// Invalid pairs (including non-IO sources) are rejected by validation inside the definitions.
//
template <class TTraits = TransformIOTraits<>, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);

template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);
/// @endcond
}  // namespace fnfelt::monad::io
