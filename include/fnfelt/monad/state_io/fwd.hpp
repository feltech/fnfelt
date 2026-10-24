// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/fwd.hpp
 *
 * Forward declarations for the StateIO monad transformer.
 */
#pragma once
/// @cond
#include <meta>

#include <fnfelt/static_string.hpp>

namespace fnfelt::monad::state_io
{
namespace detail
{
inline constexpr char default_state_io_name[] = "StateIO";
template <char const * name_cstr = default_state_io_name>
struct CreateStateIOTraits;
template <char const * name_cstr = default_state_io_name>
struct LiftStateIOTraits;
template <char const * name_cstr = default_state_io_name>
struct JustStateIOTraits;
template <char const * name_cstr = default_state_io_name>
struct AndThenStateIOTraits;
template <char const * name_cstr = default_state_io_name>
struct TransformStateIOTraits;
template <char const * name_cstr = default_state_io_name>
struct SelectStateIOTraits;
template <char const * name_cstr = default_state_io_name>
struct ApStateIOTraits;
}  // namespace detail

template <class TState, class TAction, class TTraits = detail::CreateStateIOTraits<>>
class StateIO;
template <class TState, class TAction, class TTraits = detail::CreateStateIOTraits<>>
struct StateRunProxy;
template <class TState, class TTraits = detail::CreateStateIOTraits<>, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);
template <class TTraits = detail::AndThenStateIOTraits<>, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);
template <class TTraits = detail::TransformStateIOTraits<>, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);
template <class TTraits = detail::SelectStateIOTraits<>, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback);
template <char const * name_cstr, class TChoice, class TFallback>
[[nodiscard]] constexpr auto select(TChoice && choice, TFallback && fallback);
template <char const * name_cstr, class TSource, class TTransformer>
// cpplint mistakes this for std::transform; no <algorithm> is used here.
// NOLINTNEXTLINE(build/include_what_you_use)
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer);
template <char const * name_cstr, class TSource, class TContinuation>
[[nodiscard]] constexpr auto and_then(TSource && source, TContinuation && continuation);
template <char const * name_cstr, class TState, class TAction>
[[nodiscard]] constexpr auto create(TAction && action);
}  // namespace fnfelt::monad::state_io
/// @endcond
