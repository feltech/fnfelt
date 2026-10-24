// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/bind.hpp
 *
 * Bind readers to owned environments for execution inside IO compositions.
 */
#pragma once

#include <meta>

#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/reader_io/ReaderIO.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/**
 * Whether a reader's inner IO requires the scheduler.
 *
 * @tparam TReader Validated ReaderIO type.
 * @return True for asynchronous readers.
 */
template <class TReader>
consteval bool bound_reader_is_async()
{
    return io::detail::action_is_async(io::detail::io_action_meta(reader_io_io_meta(^^TReader)));
}

/**
 * Owned asynchronous reader invocation inside the awaiting IO tree.
 *
 * @tparam TState Environment type.
 * @tparam TReader ReaderIO type.
 */
template <class TState, class TReader>
struct BoundReaderProxy : io::detail::TransportProxyTag
{
    /// Produced value.
    using value_type = typename TReader::value_type;
    /// Owned reader and environment.
    std::tuple<TReader, TState> arg;

    /// Materialise the reader inside its child task and retain the bound proxy until completion.
    static constexpr auto fn =
        [](auto, TReader source, TState state) -> lf::task<io::detail::task_result_t<value_type>>
    {
        // Materialisation may throw: keep it inside the task so IO's fork/call slot owns the
        // exception and joins outstanding siblings before propagating it.
        auto bound = std::move(source).build(std::move(state));
        auto inner = std::move(bound).io();
        co_return co_await io::detail::just_child(std::move(inner));
    };

    /// Bind and execute the reader inline during constant evaluation.
    static constexpr auto run_inline = [](TReader source, TState state) -> value_type
    {
        auto bound = std::move(source).build(std::move(state));
        return std::move(bound).io().build().sync_wait();
    };

    /**
     * Retain the reader and its environment.
     *
     * @param stored Owned task arguments.
     */
    constexpr explicit BoundReaderProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * IO action owning a reader and its environment until execution.
 *
 * @tparam TState Environment type.
 * @tparam TReader ReaderIO type.
 */
template <class TState, class TReader>
struct BoundReaderAction
{
    /// Plain result or owning asynchronous proxy.
    using result_type = std::conditional_t<
        bound_reader_is_async<TReader>(),
        BoundReaderProxy<TState, TReader>,
        typename TReader::value_type>;
    /// Source reader.
    TReader source;
    /// Owned environment.
    TState state;

    /**
     * Invoke the reader inline or transfer ownership to an asynchronous child.
     *
     * @param self Bound action to consume.
     * @return Plain value or asynchronous proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (bound_reader_is_async<TReader>())
        {
            return result_type{{FW(self).source, FW(self).state}};
        }
        else
        {
            auto bound = FW(self).source.build(FW(self).state);
            return std::move(bound).io().build().sync_wait();
        }
    }
};

/**
 * Copy an environment into a lazy IO retaining the reader's value category.
 *
 * @tparam TState Environment type, preserving its cv-qualification.
 * @tparam TReader Deduced ReaderIO type.
 * @param source Reader to bind.
 * @param state Environment to copy.
 * @return IO invoking the reader when executed.
 */
template <class TState, class TReader>
constexpr auto bind_reader(TReader && source, TState const & state)
{
    return io::create(BoundReaderAction<TState, std::remove_cvref_t<TReader>>{FW(source), state});
}

/**
 * Reflect the IO wrapping a validated reader and environment.
 *
 * @param state_meta Complete environment type.
 * @param reader_meta Validated ReaderIO type.
 * @return Bound IO type reflection.
 */
consteval std::meta::info bound_reader_io_meta(
    std::meta::info state_meta, std::meta::info reader_meta)
{
    auto const action_meta = substitute(^^BoundReaderAction, {state_meta, reader_meta});
    return substitute(^^io::IO, {action_meta, ^^io::detail::CreateIOTraits<>});
}
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
