// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/just.hpp>

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
    auto action = [] { return 1; };
    auto source = fnfelt::monad::io::IO<decltype(action)>{action};
    auto result = source.build<RejectBuild>();
    (void)result;
}
