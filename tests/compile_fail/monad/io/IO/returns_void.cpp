// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>

// A function returning void is callable with no arguments but does not return a value, so IO must
// fail with the friendly static_assert message naming the failing precondition.

void construct()
{
    fnfelt::monad::io::IO<void (*)()> const io_monad{nullptr};
}