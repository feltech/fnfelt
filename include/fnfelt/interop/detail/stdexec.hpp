// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file stdexec.hpp
 *
 * Owned sender completions and libfork suspension support.
 *
 * A "completion" is one of
 *
 * stdexec::set_value(receiver, values...); // Finished successfully.
 * stdexec::set_error(receiver, error);     // Failed.
 * stdexec::set_stopped(receiver);          // Finished without a value or error.
 *
 * A sender may advertise multiple value and error completion signatures. Each started operation
 * completes exactly once through one of these calls.
 *
 * In the general case, all possible supported completion signatures are packaged as
 * optional<expected<variant<tuple>, expected<variant, exception_ptr>>.
 *
 * In the special (but common) case of single overloads with a single value, this can reduce to
 * optional<expected<value, expected<error, exception_ptr>>.
 *
 * The outer optional represents "stopped", i.e no result when unset.
 */
#pragma once

#include <meta>

#include <exception>
#include <expected>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <libfork/core/ext/context.hpp>
#include <libfork/core/ext/handles.hpp>
#include <stdexec/execution.hpp>

namespace fnfelt::interop::detail
{
/**
 * Validate whether completion arguments can be stored by value across suspension.
 *
 * Produces diagnostics for arguments that cannot be retained, moved, or safely destroyed.
 * Returns an empty string on success, otherwise a diagnostic.
 */
inline constexpr auto validate_completion = [](std::meta::info source) consteval -> std::string_view
{
    auto const owned = decay(source);
    if (!is_object_type(owned) || !is_complete_type(owned))
    {
        return "fnfelt: sender completion must decay to a complete object type";
    }
    if (!is_constructible_type(owned, {source}))
    {
        return "fnfelt: sender completion must be constructible as an owned value";
    }
    if (!is_move_constructible_type(owned))
    {
        return "fnfelt: sender completion must be move-constructible";
    }
    if (!is_nothrow_destructible_type(owned))
    {
        return "fnfelt: sender completion must be nothrow-destructible";
    }
    return {};
};

/**
 * Decayed completion argument type with validated storage requirements.
 *
 * Provides a by-value representation of an advertised completion argument, with an injectable
 * validator for testing diagnostics.
 */
template <class TArg, auto validator = validate_completion>
struct CompletionArgument
{
    static_assert(validator(^^TArg).empty(), validator(^^TArg));
    using type = std::decay_t<TArg>;  ///< Owned argument type.
};

/**
 * Tuple representation of a successful completion with multiple arguments.
 *
 * Retains each argument as a validated decayed value so the completion has one storable result.
 */
template <class... TArgs>
struct CompletionValue
{
    using type =
        std::tuple<typename CompletionArgument<TArgs>::type...>;  ///< Owned argument tuple.
};

/**
 * Direct value representation of a successful completion with one argument.
 *
 * Avoids a single-element tuple while retaining the argument's validated decayed type.
 */
template <class TArg>
struct CompletionValue<TArg>
{
    using type = CompletionArgument<TArg>::type;  ///< Owned argument.
};

/**
 * Monostate representation of a successful completion without arguments.
 *
 * Gives empty success a value distinct from stopped completion.
 */
template <>
struct CompletionValue<>
{
    using type = std::monostate;  ///< Empty success value.
};

/// Owned value corresponding to a completion's arguments.
template <class... TArgs>
using completion_value_t = CompletionValue<TArgs...>::type;

/**
 * Collapse duplicate alternatives, retaining a placeholder for an unreachable channel.
 *
 * E.g. a sender that can complete with a std::string, int&, or int collapses to two value
 * alternatives: std::variant<std::string, int>.
 *
 * @param candidates Normalized alternative types.
 * @return Direct type, variant, or empty-channel placeholder.
 */
consteval std::meta::info alternative_meta(std::initializer_list<std::meta::info> const candidates)
{
    std::vector<std::meta::info> unique;
    for (auto const candidate : candidates)
    {
        bool is_duplicate = false;
        for (auto const existing : unique)
        {
            is_duplicate = is_duplicate || is_same_type(candidate, existing);
        }
        if (!is_duplicate)
        {
            unique.push_back(candidate);
        }
    }
    if (unique.empty())
    {
        return ^^std::monostate;
    }
    if (unique.size() == 1)
    {
        return unique.front();
    }
    return substitute(^^std::variant, unique);
}

/// Validated decayed type or variant of distinct alternatives.
template <class... TTypes>
using alternatives_t = [:alternative_meta({^^typename CompletionArgument<TTypes>::type...}):];

/**
 * Validate a normalized channel's eligibility as the value type of std::expected.
 *
 * Provides an adapter diagnostic for reserved direct payload types before result storage is formed.
 * Returns an empty string on success, otherwise a diagnostic.
 */
inline constexpr auto validate_expected_value = [](std::meta::info value)
                                                    consteval -> std::string_view
{
    value = dealias(value);
    if (is_same_type(value, ^^std::in_place_t) || is_same_type(value, ^^std::unexpect_t) ||
        (has_template_arguments(value) && template_of(value) == ^^std::unexpected))
    {
        return "fnfelt: a direct sender completion cannot be in_place_t, unexpect_t, or "
               "unexpected; std::expected forbids these value types";
    }
    return {};
};

/**
 * Completion and outcome types for a sender and its receiver environment.
 *
 * Provides normalized success and error alternatives for an rvalue sender with a const-reference
 * environment, including validation of the types used in the materialized outcome.
 */
template <class TSender, class TEnvironment, auto validator = validate_expected_value>
struct SenderTypes
{
    /// Owned success alternatives.
    using value_type = stdexec::
        value_types_of_t<TSender &&, TEnvironment const &, completion_value_t, alternatives_t>;
    /// Owned sender error alternatives.
    using error_type = stdexec::error_types_of_t<TSender &&, TEnvironment const &, alternatives_t>;
    static_assert(validator(^^value_type).empty(), validator(^^value_type));
    static_assert(validator(^^error_type).empty(), validator(^^error_type));

private:
    // Avoid cascading standard-library errors after reporting a rejected channel type.
    using safe_value_type =
        std::conditional_t<validator(^^value_type).empty(), value_type, std::monostate>;
    using safe_error_type =
        std::conditional_t<validator(^^error_type).empty(), error_type, std::monostate>;

public:
    /// Materialized terminal completion.
    using outcome_type = std::optional<
        std::expected<safe_value_type, std::expected<safe_error_type, std::exception_ptr>>>;
};

/**
 * Storage linking a suspended task with its non-owning sender receiver.
 *
 * Retains the receiver environment and terminal outcome, and identifies the libfork continuation
 * and originating worker for completion handoff. Must remain alive until completion is published.
 */
template <class TSender, class TEnvironment>
struct BridgeState
{
    using environment_type = TEnvironment;             ///< Receiver query properties.
    using types = SenderTypes<TSender, TEnvironment>;  ///< Completion type computation.
    using value_type = types::value_type;              ///< Owned success alternatives.
    using error_type = types::error_type;              ///< Owned error alternatives.
    using outcome_type = types::outcome_type;          ///< Materialized completion.

    TEnvironment environment;             ///< Properties exposed to the sender.
    outcome_type outcome;                 ///< Result inspected only after completion.
    lf::worker_context * return_context;  ///< Originating libfork worker.
    lf::submit_handle continuation{};     ///< Suspended task's submission handle.

    /**
     * Record completion and make the suspended task runnable.
     *
     * @param tag Completion channel.
     * @param args Completion arguments.
     */
    template <class TTag, class... TArgs>
    void complete(TTag tag, TArgs &&... args) noexcept
    {
        try
        {
            store(tag, std::forward<TArgs>(args)...);
        }
        catch (...)
        {
            static_assert(
                noexcept(outcome.emplace(std::unexpect, std::unexpect, std::current_exception())));
            outcome.emplace(std::unexpect, std::unexpect, std::current_exception());
        }

        auto * target = return_context;
        auto * const pending = continuation;
        // Publishing can resume the coroutine and destroy this object and its receiver.
        target->schedule(pending);
    }

private:
    template <class... TValues>
    void store(stdexec::set_value_t, TValues &&... values)
    {
        using alternative_type = completion_value_t<TValues...>;
        // A single normalized alternative is stored directly; multiple alternatives use a variant.
        // The direct alternative can itself be a variant, so compare exact types rather than
        // checking whether value_type is a variant. Only the wrapper needs in_place_type.
        if constexpr (std::is_same_v<value_type, alternative_type>)
        {
            outcome.emplace(std::in_place, std::forward<TValues>(values)...);
        }
        else
        {
            outcome.emplace(
                std::in_place,
                std::in_place_type<alternative_type>,
                std::forward<TValues>(values)...);
        }
    }

    template <class TError>
    void store(stdexec::set_error_t, TError && error)
    {
        using alternative_type = std::decay_t<TError>;
        if constexpr (std::is_same_v<error_type, alternative_type>)
        {
            outcome.emplace(std::unexpect, std::in_place, std::forward<TError>(error));
        }
        else
        {
            outcome.emplace(
                std::unexpect,
                std::in_place,
                std::in_place_type<alternative_type>,
                std::forward<TError>(error));
        }
    }

    void store(stdexec::set_stopped_t) noexcept
    {
        outcome.reset();
    }
};

/**
 * Non-owning stdexec receiver that delivers terminal completions to bridge storage.
 *
 * Exposes the stored execution properties and makes the suspended libfork task runnable on
 * completion. The bridge storage must remain valid until that completion is published.
 */
template <class TState>
struct Receiver
{
    using receiver_concept = stdexec::receiver_tag;  ///< Sender/receiver protocol marker.

    TState & bridge_state;  ///< Storage owned by the suspended awaiter.

    /**
     * Expose execution properties to the sender.
     *
     * @return Stored receiver environment.
     */
    auto get_env() const noexcept -> TState::environment_type const &
    {
        return bridge_state.environment;
    }

    /**
     * Deliver successful completion.
     *
     * @param values Successful result arguments.
     */
    template <class... TValues>
    void set_value(TValues &&... values) && noexcept
    {
        bridge_state.complete(stdexec::set_value, std::forward<TValues>(values)...);
    }

    /**
     * Deliver failed completion.
     *
     * @param error Sender's error argument.
     */
    template <class TError>
    void set_error(TError && error) && noexcept
    {
        bridge_state.complete(stdexec::set_error, std::forward<TError>(error));
    }

    /// Deliver stopped completion.
    void set_stopped() && noexcept
    {
        bridge_state.complete(stdexec::set_stopped);
    }
};

/**
 * Deferred conversion that constructs a factory result directly in its destination.
 *
 * Useful with std::optional::emplace for non-movable types returned by a factory, avoiding a move
 * from an already constructed temporary.
 */
template <class TFactory>
struct FromFactory
{
    TFactory factory;  ///< Deferred construction callable.

    using result_type = std::invoke_result_t<TFactory &&>;  ///< Constructed object type.

    /**
     * Construct the deferred result.
     *
     * @return Factory result.
     */
    // Implicit conversion lets optional::emplace construct an immovable factory result.
    // ReSharper disable once CppNonExplicitConversionOperator
    constexpr operator result_type() && noexcept(std::is_nothrow_invocable_v<TFactory &&>)
    {
        return std::invoke(std::move(factory));
    }
};

/**
 * Libfork awaitable that materializes a sender's terminal outcome.
 *
 * Owns the sender, receiver environment, and connected operation without a separate bridge
 * allocation. Movable only before connection; must be immediately awaited with the current
 * libfork worker context so its connected operation remains at a stable address.
 */
template <class TSender, class TEnvironment>
class SenderAwaiter
{
    using state_type = BridgeState<TSender, TEnvironment>;
    using operation_type = stdexec::connect_result_t<TSender &&, Receiver<state_type>>;

    TSender sender_;
    state_type bridge_state_;
    std::optional<operation_type> operation_;

public:
    /**
     * Own the operation's inputs before connecting it.
     *
     * @param current Originating libfork worker.
     * @param source Sender to consume.
     * @param environment Receiver query properties.
     */
    template <class TSource, class TSourceEnvironment>
    SenderAwaiter(
        lf::worker_context * current, TSource && source, TSourceEnvironment && environment)
        pre(current != nullptr)
        : sender_(std::forward<TSource>(source)),
          bridge_state_{
              .environment = std::forward<TSourceEnvironment>(environment),
              .outcome = {},
              .return_context = current,
              .continuation = {}}
    {
    }

    /**
     * Move an unconnected awaiter.
     *
     * @param other Unconnected source awaiter.
     */
    SenderAwaiter(SenderAwaiter && other) noexcept(
        std::is_nothrow_move_constructible_v<TSender> &&
        std::is_nothrow_move_constructible_v<TEnvironment>) pre(!other.operation_.has_value())
        : sender_(std::move(other.sender_)),
          bridge_state_{
              .environment = std::move(other.bridge_state_.environment),
              .outcome = {},
              .return_context = other.bridge_state_.return_context,
              .continuation = {}}
    {
    }

    SenderAwaiter(SenderAwaiter const &) = delete;
    SenderAwaiter & operator=(SenderAwaiter const &) = delete;
    SenderAwaiter & operator=(SenderAwaiter &&) = delete;

    /**
     * Connect at the awaiter's final address.
     *
     * Libfork calls this hook while processing co_await, after moving the awaiter into its
     * coroutine wrapper and before suspension. The sender starts later in await_suspend().
     *
     * @return False, requesting suspension.
     */
    bool await_ready() pre(!operation_.has_value())
    {
        // Libfork moves this awaiter before calling await_ready(), so connect only now:
        // the receiver must refer to bridge_state_ at its final address.
        //
        // The operation state may be immovable. emplace(connect(...)) would first create
        // a temporary operation, then try to move it into the optional. Pass FromFactory
        // instead: its implicit conversion calls connect() while the optional constructs
        // its payload. Guaranteed copy elision initializes the returned operation directly
        // in the optional's storage, without moving it.
        operation_.emplace(
            FromFactory{[this]
                        {
                            return stdexec::connect(
                                std::move(sender_), Receiver<state_type>{bridge_state_});
                        }});
        return false;
    }

    /**
     * Start the connected operation after saving the suspended task.
     *
     * @param continuation Suspended libfork task.
     */
    void await_suspend(lf::submit_handle continuation) noexcept
    {
        bridge_state_.continuation = continuation;
        stdexec::start(*operation_);
    }

    /**
     * Consume the recorded completion.
     *
     * @return Owned terminal outcome.
     */
    auto await_resume() -> state_type::outcome_type
    {
        return std::move(bridge_state_.outcome);
    }
};
}  // namespace fnfelt::interop::detail
