// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/fwd.hpp
 *
 * Implementation-detail forward declarations for the IO monad.
 */
#pragma once

#include <meta>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/// Check whether a reflection is of a (cv-ref-stripped) specialisation of IO, with any traits.
consteval bool is_io(std::meta::info type_meta);
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
