// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <tuple>

#include <fnfelt/monad/io/sequence.hpp>

// The default sequence traits reject a reference element: the sequence's action stores its elements
// by value, so an IO reference has nothing to run, and it is rejected before the IO check because
// that check sees through references.

void sequence_reference_element()
{
    auto io = fnfelt::monad::io::just(1);
    std::tuple<decltype(io) &> refs(io);
    (void)fnfelt::monad::io::sequence(refs);
}
