// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/create.hpp>

#include <fnfelt/static_string.hpp>

// create with a name runs the same class-scope validation as direct IO construction, so the
// friendly static_assert message must use the given name ("FileIO") rather than the default "IO".

namespace
{
/// Action that is not callable with no arguments.
struct CallableWithArg
{
    int operator()(int) const
    {
        return 1;
    }
};
}  // namespace

void construct()
{
    using namespace fnfelt::literals;
    auto io = fnfelt::monad::io::create<"FileIO"_ss>(CallableWithArg{});
}
