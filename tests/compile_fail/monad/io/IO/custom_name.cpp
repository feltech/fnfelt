// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>

#include <fnfelt/static_string.hpp>

// FileIOTraits names the IO instantiation "FileIO", so the friendly static_assert message must use
// that name rather than the default "IO".

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
    using FileIOTraits = fnfelt::monad::io::IOTraits<"FileIO"_ss>;
    fnfelt::monad::io::IO<CallableWithArg, FileIOTraits> const io_monad{CallableWithArg{}};
}
