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
template <class TAction, class TTraits = void>
class IO;
}  // namespace fnfelt::monad::io
