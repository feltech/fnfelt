// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/on.hpp>

namespace
{
struct Reject : fnfelt::monad::io::detail::OnIOTraits<>
{
    static constexpr std::string_view name = "CustomIO";
    static consteval std::string_view validate_on(std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    namespace io = fnfelt::monad::io;
    auto result = io::on<Reject>(nullptr, io::just(1));
    (void)result;
}
