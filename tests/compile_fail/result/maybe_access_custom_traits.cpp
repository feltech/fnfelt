// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>
#include <utility>

#include <fnfelt/result.hpp>

namespace
{
/// Reject optional extraction after the ordinary Result access policy accepts its source.
struct RejectMaybeAccessTraits : fnfelt::detail::ResultTraits
{
    /**
     * Reject consuming payload access with a recognizable diagnostic.
     *
     * @param result_meta Source expression type, unused by the rejecting policy.
     * @param is_error_branch Selected payload branch, unused by the rejecting policy.
     * @return Custom rejection diagnostic.
     */
    static consteval std::string_view validate_maybe_access(
        [[maybe_unused]] std::meta::info result_meta, [[maybe_unused]] bool is_error_branch)
    {
        return "rejected by custom Result optional access traits";
    }
};
}  // namespace

void rejected_optional_access()
{
    using access_type = fnfelt::detail::ResultAccess<fnfelt::Result<int>, RejectMaybeAccessTraits>;
    auto source = fnfelt::result::success<fnfelt::Result<int>>(3);
    auto extracted = access_type::maybe_get<false>(std::move(source));
    static_assert(sizeof(extracted) > 0);
}
