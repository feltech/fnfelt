// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/on.hpp
 *
 * Scoped execution of IO on a worker context.
 */
#pragma once

#include <meta>

#include <exception>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <libfork/core/eventually.hpp>
#include <libfork/core/scheduler.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/result.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io::detail
{
/// Placeholder for a rejected context-scoped IO.
struct OnError
{
};

/**
 * Execute a source on a target context and restore the awaiting context.
 *
 * @tparam TOriginal Original IO type.
 */
template <class TOriginal>
struct OnProxy : TransportProxyTag
{
    /// Unchanged logical source value.
    using value_type = typename TOriginal::value_type;
    /// Target context and owned source.
    std::tuple<lf::worker_context *, TOriginal> arg;

    /// Restore the origin before delivering a value or propagating an exception.
    static constexpr auto fn = [](auto self,
                                  lf::worker_context * target_context,
                                  TOriginal original) -> lf::task<task_result_t<value_type>>
    {
        auto * original_context = self.context();
        co_await lf::resume_on(target_context);

        lf::eventually<task_result_t<value_type>> output;
        std::exception_ptr error;
        try
        {
            output = co_await just_child(std::move(original));
        }
        catch (...)
        {
            error = std::current_exception();
        }

        // Awaiting an asynchronous child can resume this task on another context.
        // Await outside the handler: suspension is not permitted in a catch block.
        co_await lf::resume_on(original_context);
        if (error)
        {
            std::rethrow_exception(error);
        }
        co_return std::move(*output);
    };

    /**
     * Retain the target and source until execution.
     *
     * @param args Arguments passed to the driver.
     */
    constexpr explicit OnProxy(decltype(arg) args) : arg{std::move(args)} {}
};

/**
 * Deferred context-scoped source execution.
 *
 * @tparam TOriginal Original IO type.
 */
template <class TOriginal>
struct OnAction
{
    /// Asynchronous scoped execution.
    using result_type = OnProxy<TOriginal>;
    /// Borrowed destination context.
    lf::worker_context * target_context;
    /// Owned deferred source.
    TOriginal original;

    /**
     * Retain the source for asynchronous execution.
     *
     * @param self Stored action.
     * @return Owning execution proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        return result_type{{self.target_context, FW(self).original}};
    }
};

/**
 * Describe a rejected context-scoped source.
 *
 * @param name Diagnostic IO name.
 * @param reason Validation failure.
 * @return Friendly diagnostic.
 */
consteval std::string on_error_msg(std::string_view name, std::string_view reason)
{
    std::string message = "fnfelt: IO on error: ";
    message += name;
    message += ": ";
    message += reason;
    return message;
}

/**
 * Validation traits for context-scoped IO.
 *
 * @tparam name_cstr Diagnostic name with static storage duration.
 */
template <char const * name_cstr>
struct OnIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the source without instantiating execution.
     *
     * @param source_meta Source IO type.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate_on(std::meta::info source_meta)
    {
        if (!is_io(source_meta))
        {
            return "source is not an IO";
        }
        return fnfelt::detail::ResultTypeInfo::from(io_value_meta(source_meta)).validate();
    }
};
}  // namespace fnfelt::monad::io::detail

#include <fnfelt/macros_pop.hpp>
