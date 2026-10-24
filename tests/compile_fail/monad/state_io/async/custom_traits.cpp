// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <expected>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/async.hpp>
#include <fnfelt/monad/state_io/just.hpp>

namespace
{
struct Reject : fnfelt::monad::state_io::detail::CreateAsyncStateIOTraits<>
{
    static constexpr std::string_view name = "CustomStateIO";
    static consteval std::string_view validate_create_async(std::meta::info, std::meta::info)
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
    auto result = state_io::create_async<int, Reject>(
        [](auto, int state) -> lf::task<std::pair<int, int>> { co_return std::pair{1, state}; });
    (void)result;
}
