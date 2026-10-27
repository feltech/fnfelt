// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <expected>
#include <string_view>

#include <fnfelt/monad/state_io/just.hpp>
#include <fnfelt/monad/state_io/select.hpp>

namespace
{
struct Reject : fnfelt::monad::state_io::detail::SelectStateIOTraits<>
{
    static constexpr std::string_view name = "CustomStateIO";
    static consteval std::string_view validate_select(std::meta::info, std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    namespace state_io = fnfelt::monad::state_io;
    auto choice = state_io::just<int>(std::expected<int, int>{42});
    auto fallback = state_io::just<int>([](int value) { return value * 2; });
    auto result = state_io::select<Reject>(choice, fallback);
    (void)result;
}
