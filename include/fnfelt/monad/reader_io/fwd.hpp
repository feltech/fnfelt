// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../reader_io.hpp"
/**
 * @file monad/reader_io/fwd.hpp
 *
 * Forward declarations for the ReaderIO monad.
 */
#pragma once
// Doxygen parses this header before the operation headers (it sorts first), then merges the
// declarations' documentation with definitions documented in their owning headers and warns about
// duplicate @param sections. The declarations are redundant for documentation (the definitions
// are the canonical docs), so hide them from Doxygen.
/// @cond

#include <meta>

#include <fnfelt/static_string.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{
/// Default name for ReaderIO monad diagnostics.
inline constexpr char default_reader_io_name[] = "ReaderIO";
}  // namespace detail

/// Traits validating the action/state pair and consumption of a ReaderIO, naming the
/// instantiation used in diagnostics.
namespace detail
{
template <char const * name_cstr = detail::default_reader_io_name>
struct CreateReaderIOTraits;

/// Traits validating the value provided to `reader_io::just`.
template <char const * name_cstr = detail::default_reader_io_name>
struct JustReaderIOTraits;

/// Traits validating the source IO provided to `reader_io::lift`.
template <char const * name_cstr = detail::default_reader_io_name>
struct LiftReaderIOTraits;

/// Traits validating the source ReaderIO and continuation function provided to
/// `reader_io::and_then`.
template <char const * name_cstr = detail::default_reader_io_name>
struct AndThenReaderIOTraits;

/// Traits validating the source ReaderIO and transformer function provided to
/// `reader_io::transform`.
template <char const * name_cstr = detail::default_reader_io_name>
struct TransformReaderIOTraits;

/// Traits validating a choice reader and its conditional fallback.
template <char const * name_cstr = detail::default_reader_io_name>
struct SelectReaderIOTraits;

/// Traits validating the state-first coroutine function and arguments provided to
/// `reader_io::create_async`.
template <char const * name_cstr = detail::default_reader_io_name>
struct CreateAsyncReaderIOTraits;
}  // namespace detail

/// Traits validating the function and value ReaderIOs provided to `reader_io::ap`.
template <char const * name_cstr = detail::default_reader_io_name>
struct ApReaderIOTraits;

/// Run proxy binding a ReaderIO's action to a state for a single materialisation; defined in
/// async.hpp.
template <class TState, class TAction, class TTraits = detail::CreateReaderIOTraits<>>
struct ReaderRunProxy;

template <class TState, class TAction, class TTraits = detail::CreateReaderIOTraits<>>
class ReaderIO;

template <class TState, class TTraits = detail::CreateReaderIOTraits<>, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

template <char const * name_cstr, class TState, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

// Forward declarations for the free and_then functions; defined and documented in and_then.hpp.
// The source ReaderIO is taken by forwarding reference so rvalue chains move and lvalues are
// copied, and the continuation is forwarded as an rvalue into the composition.
template <class TTraits = detail::AndThenReaderIOTraits<>, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);

// Forward declarations for the free transform functions; defined and documented in transform.hpp.
// The source ReaderIO is taken by forwarding reference so rvalue chains move and lvalues are
// copied, and the transformer is forwarded as an rvalue into the composition.
template <class TTraits = detail::TransformReaderIOTraits<>, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);

template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);
template <class TTraits = detail::SelectReaderIOTraits<>, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback);

template <char const * name_cstr, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback);

/// @endcond

}  // namespace fnfelt::monad::reader_io
