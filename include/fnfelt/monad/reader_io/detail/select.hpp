// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/select.hpp
 *
 * Conditional effects sharing a ReaderIO environment.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/task.hpp>

#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/select.hpp>
#include <fnfelt/monad/io/select.hpp>
#include <fnfelt/monad/reader_io/ReaderIO.hpp>
#include <fnfelt/monad/reader_io/async.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io::detail
{
/// Placeholder for a rejected selective composition.
struct SelectError
{
};

/**
 * Whether a declared reader effect requires asynchronous execution.
 *
 * @tparam TReader Reader type.
 * @return True for an asynchronous inner IO.
 */
template <class TReader>
consteval bool select_reader_is_async()
{
    return io::detail::action_is_async(io::detail::io_action_meta(reader_io_io_meta(^^TReader)));
}

/**
 * Materialise a reader inside its asynchronous child task.
 *
 * @tparam TState Environment type.
 * @tparam TReader Reader type.
 */
template <class TState, class TReader>
struct SelectReaderProxy : io::detail::TransportProxyTag
{
    /// Produced logical value.
    using value_type = typename TReader::value_type;
    /// Reader and retained environment.
    std::tuple<TReader, TState> arg;
    /// Keep the bound reader alive throughout its inner IO execution.
    static constexpr auto fn =
        [](auto, TReader source, TState state) -> lf::task<io::detail::task_result_t<value_type>>
    {
        auto bound = std::move(source).build(std::move(state));
        auto inner = std::move(bound).io();
        co_return co_await io::detail::just_child(std::move(inner));
    };

    /// Bind the selected reader inline during constant evaluation.
    static constexpr auto run_inline = [](TReader source, TState state) -> value_type
    {
        auto bound = std::move(source).build(std::move(state));
        return std::move(bound).io().build().sync_wait();
    };

    /**
     * Retain the reader without materialising it.
     *
     * @param stored Owned reader and environment.
     */
    constexpr explicit SelectReaderProxy(decltype(arg) stored) : arg{std::move(stored)} {}
};

/**
 * Defer reader materialisation until its IO is executed.
 *
 * @tparam TState Environment type.
 * @tparam TReader Reader type.
 */
template <class TState, class TReader>
struct SelectReaderAction
{
    /// Plain value or owning asynchronous proxy.
    using result_type = std::conditional_t<
        select_reader_is_async<TReader>(),
        SelectReaderProxy<TState, TReader>,
        typename TReader::value_type>;
    /// Reader to execute.
    TReader source;
    /// Environment owned until execution.
    TState state;

    /**
     * Execute the retained reader or transfer it to its child task.
     *
     * @param self Retained reader invocation.
     * @return Logical value or asynchronous proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (select_reader_is_async<TReader>())
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
 * Share the original environment between the declared selective effects.
 *
 * @tparam TState Environment type.
 * @tparam TChoice Choice ReaderIO type.
 * @tparam TFallback Fallback ReaderIO type.
 */
template <class TState, class TChoice, class TFallback>
struct SelectAction
{
    /// Choice computation.
    TChoice choice;
    /// Conditional fallback computation.
    TFallback fallback;

    /**
     * Retain both reader invocations without evaluating either.
     *
     * @param self Declared effects.
     * @param state Original environment.
     * @return Selective inner IO.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        auto choice_state = state;
        return io::select(
            io::create(
                SelectReaderAction<TState, TChoice>{FW(self).choice, std::move(choice_state)}),
            io::create(SelectReaderAction<TState, TFallback>{FW(self).fallback, std::move(state)}));
    }
};

/**
 * Describe a rejected reader selection.
 *
 * @tparam choice_meta Choice reflection.
 * @tparam fallback_meta Fallback reflection.
 * @param name Result name.
 * @param reason Validation failure.
 * @return Diagnostic text.
 */
template <std::meta::info choice_meta, std::meta::info fallback_meta>
consteval std::string select_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg = "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " select error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += '{';
    fnfelt::detail::append_string_view(msg, maybe_reader_io_name<choice_meta>());
    msg += "(), ";
    fnfelt::detail::append_string_view(msg, maybe_reader_io_name<fallback_meta>());
    msg += "()}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(choice_meta));
    msg += ", ";
    fnfelt::detail::append_string_view(msg, display_string_of(fallback_meta));
    return msg;
}

/**
 * Validate selective reader computations and their shared environment.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct SelectReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Check matching environments and the expected/function contract.
     *
     * @param choice_meta Choice ReaderIO reflection.
     * @param fallback_meta Fallback ReaderIO reflection.
     * @return Empty on success, otherwise a diagnostic reason.
     */
    static consteval std::string_view validate_select(
        std::meta::info choice_meta, std::meta::info fallback_meta)
    {
        if (!is_reader_io(choice_meta))
        {
            return "choice source is not a ReaderIO";
        }
        if (!is_reader_io(fallback_meta))
        {
            return "fallback source is not a ReaderIO";
        }
        auto const state_meta = reader_io_state_meta(choice_meta);
        if (!is_same_type(state_meta, reader_io_state_meta(fallback_meta)))
        {
            return "choice and fallback must have the same state type";
        }
        if (!is_copy_constructible_type(state_meta))
        {
            return "state must be copy constructible";
        }
        return io::detail::validate_select_values(
            reader_io_value_meta(choice_meta), reader_io_value_meta(fallback_meta));
    }
};
}  // namespace fnfelt::monad::reader_io::detail

#include <fnfelt/macros_pop.hpp>
