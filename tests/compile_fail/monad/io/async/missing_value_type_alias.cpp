// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/create.hpp>

#include <meta>

// A hand-written async proxy that derives AsyncProxyTag but exposes no value_type alias. Creating
// an IO from its action triggers validation, which must report the missing alias with a readable
// static_assert.

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
    auto const io = fnfelt::monad::io::create(MissingValueAction{});
    (void)io;
}
