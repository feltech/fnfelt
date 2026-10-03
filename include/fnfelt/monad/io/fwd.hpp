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
#include <fnfelt/static_string.hpp>

namespace fnfelt::monad::io
{
using namespace fnfelt::literals;  // NOLINT

/// Default traits for the IO monad, naming the instantiation used in diagnostics.
template <char const * name_cstr = "IO"_ss>
struct IOTraits;

template <class TAction, class TTraits = IOTraits<>>
class IO;

// Forward declarations for the free bind functions used by IO's member bind overloads;
// defined and documented in bind.hpp.
template <class TTraits = IOTraits<>, class TSourceAction, class TSourceTraits, class TKleisli>
[[nodiscard]] constexpr auto bind(IO<TSourceAction, TSourceTraits> source, TKleisli kleisli);

template <char const * name_cstr, class TSourceAction, class TSourceTraits, class TKleisli>
[[nodiscard]] constexpr auto bind(IO<TSourceAction, TSourceTraits> source, TKleisli kleisli);
}  // namespace fnfelt::monad::io
