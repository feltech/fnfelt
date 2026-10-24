// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/ReaderIO.hpp>

namespace
{
struct RejectBuild
{
    static constexpr std::string_view name = "RejectBuild";
    static consteval std::string_view validate_build(std::meta::info)
    {
        return "rejected by custom build traits";
    }
};
}  // namespace

void rejected_build()
{
    auto action = [](int state) { return fnfelt::monad::io::just(state); };
    auto source = fnfelt::monad::reader_io::ReaderIO<int, decltype(action)>{action};
    auto result = source.build<RejectBuild>(42);
    (void)result;
}
