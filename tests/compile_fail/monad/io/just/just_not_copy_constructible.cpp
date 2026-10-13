// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <memory>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/just.hpp>

// just must reject a move-only value: the value is stored in the action and copied out on each
// lvalue run, so a std::unique_ptr cannot be lifted. The failure must be the friendly static_assert
// naming the value's short type and the copy-constructibility reason, not a hard error from the IO
// class.

void lift_move_only()
{
    auto const io = fnfelt::monad::io::just(std::make_unique<int>(1));
}
