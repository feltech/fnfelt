// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/reader_io/detail/just.hpp>
#include <fnfelt/monad/reader_io/just.hpp>

// RejectJust is a custom traits type whose validate_just rejects every value, so just must fail
// with the friendly static_assert message containing the custom reason and name, even though the
// value itself is valid. Because traits are passed explicitly to just, the traits' name is used
// rather than the default "ReaderIO".

namespace
{
/// Custom traits that reject every just value.
struct RejectJust : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "JustIO";

    static consteval std::string_view validate_just(std::meta::info /*value_meta*/)
    {
        return "rejected by custom just traits";
    }
};
}  // namespace

void apply_just()
{
    auto const reader = fnfelt::monad::reader_io::just<int, RejectJust>(1);
    (void)reader;
}
