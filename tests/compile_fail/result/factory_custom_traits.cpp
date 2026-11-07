// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/result.hpp>

namespace
{
/// Injected validation proving all Result factories use the destination policy.
struct RejectFactoryTraits
{
    /**
     * Reject the destination with a recognizable diagnostic.
     *
     * @param result_meta Destination type, unused by the rejecting policy.
     * @return Custom rejection diagnostic.
     */
    static consteval std::string_view validate_construction(
        [[maybe_unused]] std::meta::info result_meta)
    {
        return "rejected by custom Result factory traits";
    }
};
}  // namespace

void rejected_factory()
{
    using factory_type = fnfelt::detail::ResultFactory<fnfelt::Result<int>, RejectFactoryTraits>;
    auto stopped = factory_type::stopped();
    static_assert(sizeof(stopped) > 0);
}
