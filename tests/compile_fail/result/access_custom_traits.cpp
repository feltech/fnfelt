// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/result.hpp>

namespace
{
/// Injected validation proving Result access uses the source policy.
struct RejectAccessTraits
{
    /**
     * Reject the source with a recognizable diagnostic.
     *
     * @param result_meta Source type, unused by the rejecting policy.
     * @return Custom rejection diagnostic.
     */
    static consteval std::string_view validate_access([[maybe_unused]] std::meta::info result_meta)
    {
        return "rejected by custom Result access traits";
    }
};
}  // namespace

void rejected_access()
{
    using access_type = fnfelt::detail::ResultAccess<fnfelt::Result<int> &, RejectAccessTraits>;
    auto source = fnfelt::result::success<fnfelt::Result<int>>(3);
    auto checked_source = access_type::forward(source);
    static_assert(sizeof(checked_source) > 0);
}
