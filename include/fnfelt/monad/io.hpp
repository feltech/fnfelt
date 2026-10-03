// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file io.hpp
 *
 * Umbrella header for the IO monad.
 *
 * Includes the IO class with its delegating member `bind` forms, the free `bind` and `ap`
 * operations, and the supporting declarations, traits and implementation details.
 */
#pragma once

#include <fnfelt/monad/io/IO.hpp>      // IWYU pragma: export
#include <fnfelt/monad/io/detail.hpp>  // IWYU pragma: export
#include <fnfelt/monad/io/fwd.hpp>     // IWYU pragma: export
#include <fnfelt/monad/io/traits.hpp>  // IWYU pragma: export
