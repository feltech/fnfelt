// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file io.hpp
 *
 * Umbrella header for the IO monad.
 */
#pragma once

#include <fnfelt/monad/io/IO.hpp>            // IWYU pragma: export
#include <fnfelt/monad/io/and_then.hpp>      // IWYU pragma: export
#include <fnfelt/monad/io/ap.hpp>            // IWYU pragma: export
#include <fnfelt/monad/io/async.hpp>         // IWYU pragma: export
#include <fnfelt/monad/io/create.hpp>        // IWYU pragma: export
#include <fnfelt/monad/io/detail.hpp>        // IWYU pragma: export
#include <fnfelt/monad/io/fanout.hpp>        // IWYU pragma: export
#include <fnfelt/monad/io/fwd.hpp>           // IWYU pragma: export
#include <fnfelt/monad/io/just.hpp>          // IWYU pragma: export
#include <fnfelt/monad/io/repeat_until.hpp>  // IWYU pragma: export
#include <fnfelt/monad/io/select.hpp>        // IWYU pragma: export
#include <fnfelt/monad/io/sequence.hpp>      // IWYU pragma: export
#include <fnfelt/monad/io/split.hpp>         // IWYU pragma: export
#include <fnfelt/monad/io/tap.hpp>           // IWYU pragma: export
#include <fnfelt/monad/io/transform.hpp>     // IWYU pragma: export
#include <fnfelt/monad/io/traverse.hpp>      // IWYU pragma: export
