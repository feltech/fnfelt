// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/StateIO.hpp>
#include <fnfelt/monad/state_io/detail/create.hpp>

// RejectSyncWait is a custom traits type that rejects every proxy consumption, so the run must
// fail with the friendly static_assert message containing the custom reason and name.

namespace
{
/// Custom traits that reject every proxy consumption.
struct RejectSyncWait : fnfelt::monad::state_io::detail::CreateStateIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "RejectProxy";

    static consteval std::string_view validate_sync_wait(std::meta::info /*proxy_meta*/)
    {
        return "rejected by custom sync_wait traits";
    }
};

/// Valid action producing an IO.
struct IntAction
{
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(std::pair{1, 0});
    }
};
}  // namespace

void consume()
{
    fnfelt::monad::state_io::StateIO<int, IntAction, RejectSyncWait> reader{IntAction{}};
    auto const proxy = reader.build(0);
    auto const result = std::move(proxy).sync_wait();
    (void)result;
}
