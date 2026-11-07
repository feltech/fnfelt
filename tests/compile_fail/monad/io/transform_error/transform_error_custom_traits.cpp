// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/result.hpp>

namespace
{
/// Reject valid error mappings to verify trait injection diagnostics.
struct RejectErrorMap : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "RejectedErrorMap";
    static consteval std::string_view validate_transform_error(std::meta::info, std::meta::info)
    {
        return "rejected by custom error mapper traits";
    }
};
}  // namespace

void rejected_mapping()
{
    auto source = fnfelt::monad::io::create(
        [] { return fnfelt::Result<int, int>{std::in_place, std::unexpect, fnfelt::Error{1}}; });
    auto result = source.transform_error<RejectErrorMap>([](int error) { return error + 1; });
}
