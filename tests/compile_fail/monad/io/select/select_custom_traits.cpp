// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <expected>
#include <string_view>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/select.hpp>

namespace
{
struct RejectSelect : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "SelectIO";
    static consteval std::string_view validate_select(std::meta::info, std::meta::info)
    {
        return "rejected by custom selective traits";
    }
};
}  // namespace

void rejected_select()
{
    namespace io = fnfelt::monad::io;
    auto selected = io::select<RejectSelect>(
        io::create([]() -> std::expected<int, int> { return 1; }),
        io::create([] { return [](int value) { return value; }; }));
}
