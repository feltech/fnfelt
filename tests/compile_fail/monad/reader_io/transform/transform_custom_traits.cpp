// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/transform.hpp>
#include <fnfelt/monad/reader_io/transform.hpp>

// RejectTransform is a custom traits type whose validate_transform rejects every source/
// transformer pair, so transform must fail with the friendly static_assert message containing the
// custom reason, even though the pair itself is valid. Because traits are passed explicitly, the
// traits' name is used rather than the default "ReaderIO".

namespace
{
/// Valid int -> IO action.
struct IntAction
{
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(1);
    }
};

/// Transformer taking the produced value and returning a plain value. Declared only.
struct TransformerFn
{
    int operator()(int const & /*value*/) const;
};

/// Custom traits that accept valid actions but reject every transform pair.
struct RejectTransform : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "RejectReader";

    static consteval std::string_view validate_transform(
        std::meta::info /*source_meta*/, std::meta::info /*transformer_meta*/)
    {
        return "rejected by custom transform traits";
    }
};
}  // namespace

void transform_fn()
{
    auto const transformed = fnfelt::monad::reader_io::transform<RejectTransform>(
        fnfelt::monad::reader_io::create<int>(IntAction{}), TransformerFn{});
    (void)transformed;
}
