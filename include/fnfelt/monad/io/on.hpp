// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file monad/io/on.hpp
 *
 * Scoped IO execution on a worker context.
 */
#pragma once

#include <meta>

#include <type_traits>

#include <libfork/core/scheduler.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/on.hpp>
#include <fnfelt/monad/io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
/**
 * Start an IO on a worker context and restore the original context on completion.
 *
 * The original context is captured when execution begins. Values, Result outcomes and exceptions
 * are delivered after returning to it. The target must be non-null, remain alive and service work
 * throughout execution. Child computations may switch contexts or fork work; this operation does
 * not pin their execution to the target.
 *
 * The source is stored by value, copying lvalues and moving rvalues. The returned IO is always
 * asynchronous, including for a synchronous source.
 *
 * @tparam TTraits Validation and result IO traits.
 * @tparam TOriginal Original IO expression type.
 * @param target_context Borrowed destination worker context.
 * @param original Deferred computation.
 * @return Asynchronous IO producing the unchanged source value.
 */
template <class TTraits, class TOriginal>
[[nodiscard]] constexpr auto on(lf::worker_context * target_context, TOriginal && original)
{
    using original_type = std::remove_cvref_t<TOriginal>;
    static constexpr auto reason = TTraits::validate_on(^^original_type);
    static_assert(reason.empty(), detail::on_error_msg(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        return io::create<TTraits>(detail::OnAction<original_type>{target_context, FW(original)});
    }
    else
    {
        return detail::OnError{};
    }
}

/**
 * Start an IO on a worker context, naming the scoped computation.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 * @tparam TOriginal Original IO expression type.
 * @param target_context Borrowed destination worker context.
 * @param original Deferred computation.
 * @return Named asynchronous IO restoring the original context on completion.
 */
template <char const * name_cstr, class TOriginal>
[[nodiscard]] constexpr auto on(lf::worker_context * target_context, TOriginal && original)
{
    return io::on<detail::OnIOTraits<name_cstr>>(target_context, FW(original));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
