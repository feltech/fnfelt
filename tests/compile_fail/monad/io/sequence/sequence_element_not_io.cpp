// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <tuple>

#include <fnfelt/monad/io/sequence.hpp>

// The default sequence traits require every element of the tuple to be an IO. The second element is
// a plain int, so sequence must fail with the friendly static_assert message naming the reason and
// the offending element.

void sequence_plain_element()
{
    namespace io = fnfelt::monad::io;
    auto const sequenced = io::sequence(std::make_tuple(io::just(1), 2));
}
