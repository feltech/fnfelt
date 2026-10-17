// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file monad/io/detail/sequence.hpp
 *
 * Implementation details for collecting IOs into an IO.
 */
#pragma once

#include <meta>

#include <cstddef>
#include <exception>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <libfork/core/control_flow.hpp>
#include <libfork/core/eventually.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/async.hpp>
#include <fnfelt/monad/io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io::detail
{
/**
 * Whether a reflection names a `std::tuple`.
 *
 * @param type_meta Reflection of the candidate type.
 * @return True if the reflection names a `std::tuple`.
 */
consteval bool is_sequence_tuple(std::meta::info type_meta)
{
    if (!is_type(type_meta))
    {
        return false;
    }
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^std::tuple;
}

/**
 * Whether a reflection names a specialisation of `immer::array`.
 *
 * @param type_meta Reflection of the candidate type.
 * @return True if the reflection names an immutable array.
 */
consteval bool is_sequence_array(std::meta::info type_meta)
{
    if (!is_type(type_meta))
    {
        return false;
    }
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^immer::array;
}

/**
 * Tombstone returned by `io::sequence` when the tuple of IOs is invalid.
 */
struct SequenceError
{
};

/**
 * Async proxy collecting tuple elements under the awaiting scheduler, in input order.
 *
 * @tparam TIOs Element IO types.
 */
template <class... TIOs>
struct TupleSequenceProxy : TransportProxyTag
{
    /// Collected value tuple.
    using value_type = std::tuple<typename TIOs::value_type...>;
    /// Element tuple passed to the driver.
    std::tuple<std::tuple<TIOs...>> arg;

    /// Fork each element into a stable slot and join before collecting any values.
    static constexpr auto fn = [](auto,
                                  std::tuple<TIOs...> ios) -> lf::task<task_result_t<value_type>>
    {
        std::tuple<lf::eventually<task_result_t<typename TIOs::value_type>>...> outputs;
        std::exception_ptr launch_error;
        try
        {
            template for (constexpr auto idx : std::define_static_array(
                              std::views::iota(std::size_t{0}, sizeof...(TIOs))))
            {
                co_await fork_child(std::get<idx>(outputs), std::get<idx>(std::move(ios)));
            }
        }
        catch (...)
        {
            launch_error = std::current_exception();
        }
        co_await lf::join;
        if (launch_error)
        {
            std::rethrow_exception(launch_error);
        }
        co_return pack_result<value_type>(std::apply(
            [](auto &... slots) { return value_type{unpack_result(std::move(*slots))...}; },
            outputs));
    };

    /// Collect children inline during constant evaluation.
    static constexpr auto run_inline = [](std::tuple<TIOs...> ios) -> value_type
    {
        return std::apply(
            [](auto &&... children) { return value_type{FW(children).build().sync_wait()...}; },
            std::move(ios));
    };

    /**
     * Store the element tuple.
     *
     * @param arg_in Arguments to the async driver.
     */
    constexpr explicit TupleSequenceProxy(decltype(arg) arg_in) : arg{std::move(arg_in)} {}
};

/**
 * Action that runs a tuple of IOs and collects their values into a tuple, in order.
 *
 * Runtime elements are forked into indexed result slots and joined under one scheduler.
 * Constant evaluation runs the elements inline.
 *
 * The action's value category is forwarded to the stored tuple, so running an rvalue moves the
 * element IOs.
 *
 * @tparam TIOs Element IO types.
 */
template <class... TIOs>
struct TupleSequenceAction
{
    /// Collected value tuple.
    using value_type = std::tuple<typename TIOs::value_type...>;
    /// Proxy retaining the element tuple.
    using result_type = TupleSequenceProxy<TIOs...>;
    /// Element IOs to run, in the order their values are collected.
    std::tuple<TIOs...> ios;

    /**
     * Retain every element for execution.
     *
     * @param self The action to run (explicit object parameter).
     * @return Collection proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        return result_type{{FW(self).ios}};
    }
};

/**
 * Async proxy collecting array elements under the awaiting scheduler, in input order.
 *
 * @tparam TIO Element IO type.
 * @tparam TMemoryPolicy Immutable array memory policy.
 */
template <class TIO, class TMemoryPolicy>
struct ArraySequenceProxy : TransportProxyTag
{
    /// Collected value array.
    using value_type = immer::array<typename TIO::value_type, TMemoryPolicy>;
    /// Shared element array passed to the driver.
    std::tuple<immer::array<TIO, TMemoryPolicy>> arg;

    /// Fork each element into a stable slot and join before collecting any values.
    static constexpr auto fn =
        [](auto, immer::array<TIO, TMemoryPolicy> ios) -> lf::task<task_result_t<value_type>>
    {
        // Enable mutation.
        auto mut_ios = std::move(ios).transient();
        // Copy-on-write reference to underlying IOs, so we can std::move. If no other references to
        // the storage exist, then we can move out the IOs with no copies needed, otherwise array
        // storage will be copied and the resulting IO copies moved out, below.
        auto cow_ios = std::span{mut_ios.data_mut(), mut_ios.size()};
        // Storage for eventual results.
        std::vector<lf::eventually<task_result_t<typename TIO::value_type>>> outputs(
            cow_ios.size());
        std::exception_ptr launch_error;
        try
        {
            for (std::size_t idx = 0; idx < cow_ios.size(); ++idx)
            {
                co_await fork_child(outputs[idx], std::move(cow_ios[idx]));
            }
        }
        catch (...)
        {
            launch_error = std::current_exception();
        }
        co_await lf::join;
        if (launch_error)
        {
            std::rethrow_exception(launch_error);
        }

        auto values = outputs |
            std::views::transform([](auto & slot) -> auto & { return result_ref(*slot); });
        co_return value_type{
            std::make_move_iterator(values.begin()), std::make_move_iterator(values.end())};
    };

    /**
     * Store the shared element array.
     *
     * @param arg_in Arguments to the async driver.
     */
    constexpr explicit ArraySequenceProxy(decltype(arg) arg_in) : arg{std::move(arg_in)} {}
};

/**
 * Action collecting a runtime-sized immutable array in input order.
 *
 * Elements overlap under one scheduler. Element IOs
 * are moved from transient storage, copying first only when the storage is shared.
 *
 * @tparam TIO Copyable element IO type.
 * @tparam TMemoryPolicy Immutable array memory policy.
 */
template <class TIO, class TMemoryPolicy>
struct ArraySequenceAction
{
    /// Collected value array.
    using value_type = immer::array<typename TIO::value_type, TMemoryPolicy>;
    /// Proxy retaining the immutable element array.
    using result_type = ArraySequenceProxy<TIO, TMemoryPolicy>;
    /// Shared immutable element IOs.
    immer::array<TIO, TMemoryPolicy> ios;

    /**
     * Retain the elements for scheduler execution.
     *
     * @param self Action holding the array (explicit object parameter).
     * @return Collection proxy.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        return result_type{{FW(self).ios}};
    }
};

/**
 * Diagnostic string for an invalid tuple of IOs.
 *
 * The message includes the sequence name, the element list `(<io>, <io>, ...)` with `<unknown>` for
 * elements that are not IOs, the validation reason, and the offending element's display string (the
 * first element that is a reference or not an IO, otherwise the first element, otherwise the input
 * itself). Unsupported inputs use `<unknown>` instead of an element list.
 * Qualifiers are stripped before a reflection is named or spliced: a qualified reflection
 * has no identifier, and a reference cannot be spliced into a type, so naming one is a hard error.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never fail to evaluate or throw.
 *
 * @tparam tuple_meta Reflection of the tuple of IOs.
 * @param io_name Name of the IO instantiation, from its traits.
 * @param reason Human-readable validation failure reason, from its traits.
 * @return Diagnostic string.
 */
template <std::meta::info tuple_meta>
consteval std::string sequence_error_msg(std::string_view io_name, std::string_view reason)
{
    // Normalise type aliases and qualifiers without querying a non-type reflection as a type.
    constexpr std::meta::info target_meta = []
    {
        if constexpr (is_type(tuple_meta))
        {
            return dealias(remove_cvref(tuple_meta));
        }
        return tuple_meta;
    }();
    std::meta::info offending = target_meta;
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO sequence error: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += "{";
    if constexpr (is_sequence_array(target_meta))
    {
        constexpr auto element_meta = template_arguments_of(target_meta)[0];
        msg += "[";
        fnfelt::detail::append_string_view(msg, maybe_io_name<element_meta>());
        msg += "]";
        offending = remove_cvref(element_meta);
    }
    else if constexpr (is_sequence_tuple(target_meta))
    {
        msg += "(";
        std::size_t index = 0;
        bool is_offending_set = false;
        template for (constexpr auto element_meta :
                      std::define_static_array(template_arguments_of(target_meta)))
        {
            if (index > 0)
            {
                msg += ", ";
            }
            constexpr std::meta::info named_meta = remove_cvref(element_meta);
            fnfelt::detail::append_string_view(msg, maybe_io_name<named_meta>());
            if (index == 0)
            {
                offending = named_meta;
            }
            if (!is_offending_set &&
                (is_reference_type(element_meta) || !is_io(named_meta) ||
                 (is_io(named_meta) && is_void_type(io_value_meta(named_meta)))))
            {
                offending = named_meta;
                is_offending_set = true;
            }
            ++index;
        }
        msg += ")";
    }
    else
    {
        msg += "<unknown>";
    }
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(offending));
    return msg;
}
}  // namespace fnfelt::monad::io::detail

namespace fnfelt::monad::io::detail
{
/**
 * Traits validating the tuple or immutable array of IOs provided to `io::sequence`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct SequenceIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the tuple or immutable array of IOs.
     *
     * Checks, in order: the argument is a `std::tuple` specialisation, and every element of it is a
     * non-reference IO that produces a value. An empty tuple is accepted: it sequences to an IO of
     * an empty tuple. An `immer::array` is accepted when its element type is an IO producing a
     * value, including when the array is empty.
     *
     * @param tuple_meta Reflection of the tuple or immutable array of element types.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_sequence(std::meta::info tuple_meta)
    {
        if (!is_type(tuple_meta))
        {
            return "provided tuple is not a type";
        }
        tuple_meta = dealias(remove_cvref(tuple_meta));
        if (detail::is_sequence_array(tuple_meta))
        {
            auto const element_meta = template_arguments_of(tuple_meta)[0];
            if (!detail::is_io(element_meta))
            {
                return "element of the array is not an IO";
            }
            if (is_void_type(detail::io_value_meta(element_meta)))
            {
                return "element of the array produces no value";
            }
            return {};
        }
        if (!detail::is_sequence_tuple(tuple_meta))
        {
            return "provided tuple is not a std::tuple";
        }
        for (auto const element_meta : template_arguments_of(tuple_meta))
        {
            if (is_reference_type(element_meta))
            {
                return "element of the tuple must not be a reference type";
            }
            if (!detail::is_io(element_meta))
            {
                return "element of the tuple is not an IO";
            }
            if (is_void_type(detail::io_value_meta(element_meta)))
            {
                return "element of the tuple produces no value";
            }
        }
        return {};
    }
};
}  // namespace fnfelt::monad::io::detail

#include <fnfelt/macros_pop.hpp>
