// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <expected>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/ap.hpp>
#include <fnfelt/monad/state_io/just.hpp>

namespace
{
struct Reject : fnfelt::monad::state_io::detail::ApStateIOTraits<>
{
    static constexpr std::string_view name = "CustomStateIO";
    static consteval std::string_view validate_ap(std::meta::info, std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    namespace state_io = fnfelt::monad::state_io;
    namespace io = fnfelt::monad::io;
    [[maybe_unused]] auto source = state_io::just<int>(1);
    auto result =
        state_io::ap<Reject>(state_io::just<int>([](int value) { return value; }), source);
    (void)result;
}
