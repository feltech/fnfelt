// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <string_view>

#include <fnfelt/monad/reader_io/get_state.hpp>

namespace
{
struct Reject : fnfelt::monad::reader_io::detail::GetStateReaderIOTraits<>
{
    static constexpr std::string_view name = "CustomReaderIO";
    static consteval std::string_view validate_get_state(std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    auto result = fnfelt::monad::reader_io::get_state<int, Reject>();
    (void)result;
}
