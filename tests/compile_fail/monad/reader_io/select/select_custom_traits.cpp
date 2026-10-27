// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/reader_io/select.hpp>

#include <meta>

#include <expected>
#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>

namespace
{
struct RejectSelect : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    static constexpr std::string_view name = "SelectReaderIO";
    static consteval std::string_view validate_select(std::meta::info, std::meta::info)
    {
        return "rejected by custom selective traits";
    }
};
}  // namespace

void rejected_select()
{
    namespace io = fnfelt::monad::io;
    namespace reader_io = fnfelt::monad::reader_io;
    auto selected = reader_io::select<RejectSelect>(
        reader_io::create<int>([](int) { return io::just(std::expected<int, int>{1}); }),
        reader_io::create<int>([](int) { return io::just([](int value) { return value; }); }));
}
