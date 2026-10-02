// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file fwd.hpp
 *
 * Forward declarations for the IO monad, including its traits.
 */
#pragma once

namespace fnfelt::monad::io
{
/// Default name for IO monad diagnostics.
inline constexpr char default_io_name[] = "IO";

/// Default traits for the IO monad, naming the instantiation used in diagnostics.
template <char const * name_cstr = default_io_name>
struct IOTraits;

template <class TAction, class TTraits = IOTraits<>>
class IO;
}  // namespace fnfelt::monad::io
