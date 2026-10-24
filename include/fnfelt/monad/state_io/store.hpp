// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../state_io.hpp"
/**
 * @file monad/state_io/store.hpp
 *
 * Replace state using a logical result while preserving that result.
 */
#pragma once
#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/state_io/create.hpp>
#include <fnfelt/monad/state_io/detail/store.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::state_io
{
/**
 * Replace updated state while preserving the source's logical value.
 *
 * Invoke the mutator exactly once as an rvalue with `const value&` and owned updated state;
 * it returns replacement state of the same type. Pair and tuple elements are spread when direct
 * invocation is unavailable, with owned state last. Logical values and
 * state may be move-only. The mutator runs only after successful source execution.
 *
 * @param source Source computation.
 * @param mutator State replacement function.
 * @return StateIO producing the preserved value and replacement state.
 */
template <class TTraits, class TSource, class TMutator>
[[nodiscard]] constexpr auto store(TSource && source, TMutator && mutator)
{
    using source_type = std::remove_cvref_t<TSource>;
    using mutator_type = std::decay_t<TMutator>;
    constexpr auto reason = TTraits::validate_store(^^source_type, ^^mutator_type);
    static_assert(
        reason.empty(),
        detail::create_error_msg<^^source_type, ^^mutator_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return state_io::create<typename source_type::state_type, TTraits>(
            detail::StoreAction{FW(source), FW(mutator)});
    }
    else
    {
        return detail::StoreError{};
    }
}

/**
 * Replace state while preserving the logical value and name the result.
 *
 * @param source Source computation.
 * @param mutator State replacement function.
 * @return Named StateIO.
 */
template <char const * name_cstr, class TSource, class TMutator>
[[nodiscard]] constexpr auto store(TSource && source, TMutator && mutator)
{
    return state_io::store<detail::StoreStateIOTraits<name_cstr>>(FW(source), FW(mutator));
}
}  // namespace fnfelt::monad::state_io

#include <fnfelt/macros_pop.hpp>
