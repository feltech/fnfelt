// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/traverse.hpp>
#include <fnfelt/static_string.hpp>

// The first argument of the named-traits traverse is a single deduced by-value type rather than a
// `std::tuple` specialisation, so a well-formed continuation with a non-tuple argument still
// reaches validation through the named overload, which must reject it with the friendly
// static_assert naming the traits name and the "not a std::tuple" reason.

void traverse_named_first_arg_not_tuple()
{
    using namespace fnfelt::literals;  // NOLINT
    auto const traversed = fnfelt::monad::io::traverse<"Traverse"_ss>(
        42, [](int value) { return fnfelt::monad::io::just(value); });
    (void)traversed;
}
