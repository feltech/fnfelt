// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file stdexec.hpp
 *
 * Await stdexec senders inside libfork tasks and fnfelt asynchronous actions.
 */
#pragma once

#include <type_traits>
#include <utility>

#include <libfork/core/ext/context.hpp>
#include <stdexec/execution.hpp>

#include "./detail/stdexec.hpp"

namespace fnfelt::interop
{
/**
 * Owned value, sender error, storage exception, or stopped outcome of awaiting a sender.
 *
 * This is an alias for nested std::optional and std::expected, i.e.
 *
 * ```
 * std::optional<std::expected<Value, std::expected<Error, std::exception_ptr>>>
 * ```
 *
 * A disengaged optional means stopped. Success stores monostate for no arguments, a decayed
 * value for one, or a tuple for several. Multiple distinct alternatives use a variant. Errors
 * use the same direct-or-variant convention. A channel with no signatures uses an unreachable
 * monostate placeholder. The inner expected distinguishes sender errors from storage exceptions.
 * Arguments are stored as decayed values; pointer-like values continue to borrow their referents.
 * Direct channel types must meet std::expected's value-type requirements: in_place_t, unexpect_t,
 * and unexpected specializations are rejected. Tuples or variants containing them are supported.
 *
 * @tparam TSender Sender stored by value and consumed as an rvalue.
 * @tparam TEnvironment Receiver environment stored by value and exposed as a const reference.
 */
template <class TSender, class TEnvironment = stdexec::env<>>
using sender_outcome_t =
    detail::SenderTypes<std::decay_t<TSender>, std::decay_t<TEnvironment>>::outcome_type;

/**
 * Suspend a libfork task until a sender completes, returning its owned outcome.
 *
 * co_await yields sender_outcome_t<TSender, TEnvironment>, for example:
 * @code
 * // stdexec::just(42)
 * using integer_outcome = std::optional<
 *     std::expected<int, std::expected<std::monostate, std::exception_ptr>>>;
 *
 * // stdexec::just(42, std::string{"data"})
 * using tuple_outcome = std::optional<std::expected<
 *     std::tuple<int, std::string>, std::expected<std::monostate, std::exception_ptr>>>;
 *
 * // stdexec::just_error(std::error_code{})
 * using error_outcome = std::optional<
 *     std::expected<std::monostate, std::expected<std::error_code, std::exception_ptr>>>;
 * @endcode
 *
 * A disengaged optional means stopped. The inner expected holds either a sender-provided
 * error as its value or a storage exception as its error.
 *
 * Immediately co_await the result using the current task's self.context(); do not suspend or
 * migrate between obtaining that context and awaiting. The libfork pool, external I/O service,
 * and any borrowed resources must outlive completion. Application I/O contexts can be supplied
 * through ordinary arguments or ReaderIO state. A supplied stop token affects the sender, not
 * the surrounding fnfelt computation. The bridge requires no separate heap allocation; senders
 * and their completion values may allocate.
 *
 * @code
 * auto action = [](auto self, auto sender)
 *     -> lf::task<fnfelt::interop::sender_outcome_t<decltype(sender)>> {
 *     co_return co_await fnfelt::interop::await_sender(self.context(), std::move(sender));
 * };
 * auto program = fnfelt::monad::io::create_async(action, stdexec::just(42));
 * @endcode
 *
 * @param current Current libfork worker context, obtained from self.context().
 * @param sender Sender copied or moved into the awaitable.
 * @param environment Properties exposed through the receiver's get_env().
 * @return Awaitable yielding sender_outcome_t<TSender, TEnvironment>.
 */
template <class TSender, class TEnvironment = stdexec::env<>>
[[nodiscard]] auto await_sender(
    lf::worker_context * current, TSender && sender, TEnvironment && environment = {})
{
    return detail::SenderAwaiter<std::decay_t<TSender>, std::decay_t<TEnvironment>>(
        current, std::forward<TSender>(sender), std::forward<TEnvironment>(environment));
}
}  // namespace fnfelt::interop
