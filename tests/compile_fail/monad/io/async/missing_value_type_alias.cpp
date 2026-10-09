// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>

#include <meta>

// A hand-written async proxy that derives AsyncProxyTag but exposes no value_type alias. Wrapping
// its action in an IO triggers the class-scope validation, which must report the missing alias
// with a readable static_assert.

namespace
{
struct MissingValueProxy : fnfelt::monad::io::AsyncProxyTag
{
};

struct MissingValueAction
{
    constexpr MissingValueProxy operator()() const
    {
        return {};
    }
};
}  // namespace

void construct()
{
    fnfelt::monad::io::IO<MissingValueAction> const io{MissingValueAction{}};
    (void)io;
}
