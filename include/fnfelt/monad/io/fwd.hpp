// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/fwd.hpp
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

#include <tuple>

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
 * Detection (tag plus `value_type`) determines IO typing, not executability: an executable proxy
 * must additionally pass execution validation. Arguments are stored in a public member named `arg`
 * whose type is a `std::tuple` specialisation, and are spread into a stateless static entry `fn`,
 * invoked as `fn(self, args...)` and returning an `lf::task`.
 */
struct AsyncProxyTag
{
};

namespace detail
{
/// Internal proxies may transport non-assignable values through reconstructing boxes.
struct TransportProxyTag : AsyncProxyTag
{
};
}  // namespace detail

/// @cond
/// Traits validating an IO's action, naming the instantiation used in diagnostics.
namespace detail
{
template <char const * name_cstr = detail::default_io_name>
struct CreateIOTraits;

template <char const * name_cstr = detail::default_io_name>
struct TapIOTraits;

/// Traits validating a value lifted by `io::just`.
template <char const * name_cstr = detail::default_io_name>
struct JustIOTraits;

/// Traits validating the source IO and continuation function provided to `io::and_then`.
template <char const * name_cstr = detail::default_io_name>
struct AndThenIOTraits;

/// Traits validating the source IO and transformer function provided to `io::transform`.
template <char const * name_cstr = detail::default_io_name>
struct TransformIOTraits;

/// Traits validating a choice IO and its conditional fallback.
template <char const * name_cstr = detail::default_io_name>
struct SelectIOTraits;

/// Traits validating the IO-wrapped function and value provided to `io::ap`.
template <char const * name_cstr = detail::default_io_name>
struct ApIOTraits;

/// Traits validating a tuple of IOs provided to `io::sequence`.
template <char const * name_cstr = detail::default_io_name>
struct SequenceIOTraits;

/// Traits validating a tuple of values and a continuation provided to `io::traverse`.
template <char const * name_cstr = detail::default_io_name>
struct TraverseIOTraits;

/// Traits validating the source IO, state and stepper function provided to `io::repeat_until`.
template <char const * name_cstr = detail::default_io_name>
struct RepeatUntilIOTraits;

/// Traits validating the stateless coroutine function provided to `io::create_async`.
template <char const * name_cstr = detail::default_io_name>
struct CreateAsyncIOTraits;
}  // namespace detail

template <class TAction, class TTraits = detail::CreateIOTraits<>>
class IO;

/// Run proxy holding an IO's action; defined in async.hpp.
template <class TAction, class TTraits = detail::CreateIOTraits<>>
struct IORunProxy;

// Forward declarations for the free create factory functions; defined and documented in
// create.hpp. The action is taken by forwarding reference so lvalues are copied and rvalues are
// moved.
template <class TTraits = detail::CreateIOTraits<>, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

template <char const * name_cstr, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

// Forward declarations for the free and_then functions; defined and documented in and_then.hpp.
// The IOs are taken by forwarding reference so rvalue chains move and lvalues are copied. Invalid
// pairs (including non-IO sources) are rejected by validation inside the definitions.
template <class TTraits = detail::AndThenIOTraits<>, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

// Forward declarations for the free transform functions; defined and documented in transform.hpp.
// The source IO is taken by forwarding reference so rvalue chains move and lvalues are copied.
// Invalid pairs (including non-IO sources) are rejected by validation inside the definitions.
template <class TTraits = detail::TransformIOTraits<>, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);

template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);

template <class TTraits = detail::SelectIOTraits<>, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback);

template <char const * name_cstr, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback);

// Forward declarations for the free traverse functions; defined and documented in traverse.hpp. The
// first argument is taken by value so an rvalue moves its element values and an lvalue copies
// them, and the continuation is taken by value. A non-tuple first argument reaches validation
// rather than failing overload resolution; invalid tuples and continuations are rejected by
// validation inside the definitions.
template <class TTraits = detail::TraverseIOTraits<>, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation);

template <char const * name_cstr, class TTuple, class TContinuation>
[[nodiscard]] constexpr auto traverse(TTuple values, TContinuation continuation);

// Forward declarations for the free repeat_until functions; defined and documented in
// repeat_until.hpp. The source IO is taken by forwarding reference so rvalue chains move and
// lvalues are copied, and the state and stepper are taken by forwarding reference (decayed into
// the composition). Invalid combinations (including non-IO sources) are rejected by validation
// inside the definitions.
template <
    class TTraits = detail::RepeatUntilIOTraits<>,
    class TSource,
    class TInitialState,
    class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitialState && initial_state, TStepper && stepper);

template <char const * name_cstr, class TSource, class TInitialState, class TStepper>
[[nodiscard]] constexpr auto repeat_until(
    TSource && source, TInitialState && initial_state, TStepper && stepper);
template <class TTraits = detail::TapIOTraits<>, class TSource, class TNext>
[[nodiscard]] constexpr auto tap(TSource && source, TNext && next);

template <char const * name_cstr, class TSource, class TNext>
[[nodiscard]] constexpr auto tap(TSource && source, TNext && next);
/// @endcond
}  // namespace fnfelt::monad::io
