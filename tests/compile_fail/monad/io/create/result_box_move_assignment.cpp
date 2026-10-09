// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io.hpp>

void assign_result()
{
    auto result = [value = 42] { return value; };
    using box_type = fnfelt::monad::io::detail::ResultBox<decltype(result)>;
    static_assert(!lf::returnable<decltype(result)>);
    static_assert(lf::returnable<box_type>);
    static_assert(!std::is_default_constructible_v<box_type>);
    static_assert(std::is_move_constructible_v<box_type>);

    auto source = box_type{result};
    auto destination = box_type{result};
    destination = std::move(source);
}
