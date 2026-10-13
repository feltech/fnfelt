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

/// Traits validating an IO's action, naming the instantiation used in diagnostics.
template <char const * name_cstr = detail::default_io_name>
struct CreateIOTraits;

/// Traits validating a value lifted by `io::just`.
template <char const * name_cstr = detail::default_io_name>
struct JustIOTraits;

template <class TAction, class TTraits = CreateIOTraits<>>
class IO;

// Forward declarations for the free create factory functions; defined and documented in
// create.hpp. The action is taken by forwarding reference so lvalues are copied and rvalues are
// moved.
template <class TTraits = CreateIOTraits<>, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);

template <char const * name_cstr, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);
}  // namespace fnfelt::monad::io
