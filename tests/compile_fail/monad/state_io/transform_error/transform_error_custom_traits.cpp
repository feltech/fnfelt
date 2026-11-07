// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/state_io.hpp>
#include <fnfelt/result.hpp>

namespace
{
struct RejectMapping : fnfelt::monad::state_io::detail::TransformErrorStateIOTraits<>
{
    static constexpr std::string_view name = "RejectState";

    static consteval std::string_view validate_transform_error(std::meta::info, std::meta::info)
    {
        return "rejected by custom error mapping traits";
    }
};
}  // namespace

void rejected_mapping()
{
    auto source =
        fnfelt::monad::state_io::just<int>(fnfelt::result::success<fnfelt::Result<int, int>>(1));
    auto mapped = source.transform_error<RejectMapping>([](int error) { return error + 1; });
    (void)mapped;
}
