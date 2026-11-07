// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <immer/array.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/result.hpp>
#include <fnfelt/static_string.hpp>

#include <fnfelt/macros_push.hpp>

namespace
{
using namespace fnfelt::literals;  // NOLINT
namespace io = fnfelt::monad::io;
using fnfelt::Infallible;
using fnfelt::Result;

template <class TValue>
struct ValueAction
{
    TValue value;

    constexpr auto operator()(this auto && self)
    {
        return FW(self).value;
    }
};

template <class TValue>
struct AsyncValueFn
{
    auto operator()(auto, TValue value) const -> lf::task<TValue>
    {
        co_return std::move(value);
    }
};

template <class TValue>
constexpr auto value_io(TValue value)
{
    return io::create(ValueAction<TValue>{std::move(value)});
}

template <class TValue>
auto async_value_io(TValue value)
{
    return io::create_async(AsyncValueFn<TValue>{}, std::move(value));
}

constexpr auto add_one = [](int value) { return value_io(value + 1); };
using skippable = Result<int, Infallible>;
using fallible = Result<int, int>;
using combined = Result<int, int>;

// Spell the representation directly so the public Result alias does not reject the declaration
// before an operation has an opportunity to provide its diagnostic context.
using invalid_result = std::optional<std::expected<int &, fnfelt::Error<int>>>;
using invalid_infallible_result = std::optional<std::expected<int &, fnfelt::Error<Infallible>>>;
using invalid_error_result = std::optional<std::expected<int, fnfelt::Error<void>>>;

using invalid_result_action = invalid_result (*)();
using invalid_infallible_action = invalid_infallible_result (*)();
using invalid_error_action = invalid_error_result (*)();

struct InvalidResultProxy : io::AsyncProxyTag
{
    using value_type = invalid_result;
};

struct InvalidAsyncAction
{
    InvalidResultProxy operator()() const;
};

// Reflection can inspect these IO template arguments without constructing an invalid IO value.
using invalid_source = io::IO<invalid_result_action, io::detail::CreateIOTraits<"BadSource"_ss>>;
using invalid_infallible_source =
    io::IO<invalid_infallible_action, io::detail::CreateIOTraits<"BadInfallibleSource"_ss>>;

struct InvalidResultContinuation
{
    invalid_source operator()(int) const;
};

using invalid_result_transformer = invalid_result (*)(int);
using invalid_result_stepper = invalid_result (*)(int, int);

struct UnreachableContinuation
{
    template <class T>
    auto operator()(T) const
    {
        static_assert(sizeof(T) == 0, "invalid Result source instantiated its continuation");
    }
};

struct UnreachableAction
{
    template <class... TArgs>
    auto operator()(TArgs...) const
    {
        static_assert(sizeof...(TArgs) != 0, "diagnostics completed a later poisoned IO");
    }
};

using unreachable_source =
    io::IO<UnreachableAction, io::detail::CreateIOTraits<"UnreachableSource"_ss>>;

struct InvalidFirstMapper
{
    invalid_source operator()(int) const;

    template <class T>
    auto operator()(T) const
    {
        static_assert(sizeof(T) == 0, "diagnostics inspected a later mapped element");
    }
};

// A channel-free recovery must not instantiate the body of an unreachable generic handler.
constexpr auto invalid_recovery = [](auto... args) { return (args.nonexistent_member() + ...); };
static_assert(
    value_io(Result<int>{std::in_place, std::in_place, 1})
        .or_else(invalid_recovery)
        .and_then(add_one)
        .build()
        .sync_wait()
        .value()
        .value() == 2);
static_assert(
    value_io(skippable{std::nullopt}).and_then(add_one).build().sync_wait() == std::nullopt);
static_assert(
    value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}})
        .or_else([](int error) { return value_io(error + 1); })
        .build()
        .sync_wait()
        .value()
        .value() == 5);
}  // namespace

TEST_CASE(
    "[pure] named IO validators reject malformed Results before inspecting wrappers or callbacks")
{
    namespace detail = io::detail;
    constexpr std::string_view reason =
        "Result success payload must be void or a complete non-array object type";
    constexpr std::string_view error_reason =
        "Error payload must be a complete non-array object type";
    static_assert(
        detail::JustIOTraits<"SavedConfig"_ss>::validate_just(^^reason) ==
        "provided value is not a complete type");
    using plain_source = decltype(value_io(1));
    using result_source = decltype(value_io(Result<int, int>{}));
    using function_source =
        io::IO<invalid_result_transformer (*)(), detail::CreateIOTraits<"ConfigFunction"_ss>>;

    static_assert(
        detail::CreateIOTraits<"ReadConfig"_ss>::validate_action(^^invalid_result_action) ==
        reason);
    static_assert(
        detail::CreateIOTraits<"ReadConfig"_ss>::validate_action(^^InvalidAsyncAction) == reason);
    static_assert(
        detail::CreateIOTraits<"ReadConfig"_ss>::validate_action(^^invalid_error_action) ==
        error_reason);
    static_assert(
        detail::JustIOTraits<"SavedConfig"_ss>::validate_just(^^invalid_result) == reason);
    static_assert(
        detail::AndThenIOTraits<"ParseConfig"_ss>::validate_and_then(
            ^^invalid_source, ^^UnreachableContinuation) == reason);
    static_assert(
        detail::AndThenIOTraits<"ParseConfig"_ss>::validate_and_then(
            ^^plain_source, ^^InvalidResultContinuation) == reason);
    static_assert(
        detail::AndThenIOTraits<"ParseConfig"_ss>::validate_and_then(
            ^^result_source, ^^InvalidResultContinuation) == reason);
    static_assert(
        detail::TransformIOTraits<"MapConfig"_ss>::validate_transform(
            ^^invalid_source, ^^UnreachableContinuation) == reason);
    static_assert(
        detail::TransformIOTraits<"MapConfig"_ss>::validate_transform(
            ^^plain_source, ^^invalid_result_transformer) == reason);
    static_assert(
        detail::OrElseIOTraits<"RecoverConfig"_ss>::validate_or_else(
            ^^invalid_source, ^^UnreachableContinuation) == reason);
    static_assert(
        detail::OrElseIOTraits<"RecoverConfig"_ss>::validate_or_else(
            ^^result_source, ^^InvalidResultContinuation) == reason);
    static_assert(
        detail::TransformErrorIOTraits<"MapError"_ss>::validate_transform_error(
            ^^invalid_infallible_source, ^^UnreachableContinuation) == reason);
    static_assert(
        detail::TransformErrorIOTraits<"MapError"_ss>::validate_transform_error(
            ^^result_source, ^^invalid_result_transformer) == reason);

    static_assert(
        detail::RepeatUntilIOTraits<"RetryConfig"_ss>::validate_repeat_until(
            ^^invalid_source, ^^int, ^^UnreachableContinuation) == reason);
    static_assert(
        detail::RepeatUntilIOTraits<"RetryConfig"_ss>::validate_repeat_until(
            ^^plain_source, ^^int, ^^invalid_result_stepper) == reason);
    static_assert(
        detail::SelectIOTraits<"ChooseConfig"_ss>::validate_select(
            ^^invalid_source, ^^plain_source) == reason);
    using choice_source = decltype(value_io(std::expected<int, int>{1}));
    static_assert(
        detail::SelectIOTraits<"ChooseConfig"_ss>::validate_select(
            ^^choice_source, ^^invalid_source) == reason);

    // Error rendering must obey the same boundary as validation, including generic callbacks.
    static_assert(
        detail::ApIOTraits<"ApplyConfig"_ss>::validate_ap(^^function_source, ^^plain_source) ==
        reason);
    static_assert(
        [=]() consteval
        {
            // Exercise the standard traits and full formatter together without failing compilation.
            auto const create_reason =
                detail::CreateIOTraits<"ReadConfig"_ss>::validate_action(^^invalid_result_action);
            auto const create_message =
                detail::create_error_msg<^^invalid_result_action>("ReadConfig", create_reason);
            auto const expected_create_message =
                std::string{"fnfelt: ReadConfig action is invalid: "} + std::string{reason} + ": " +
                std::string{display_string_of(^^invalid_result_action)};
            auto const bind_message =
                detail::and_then_error_msg<^^invalid_source, ^^UnreachableContinuation>(
                    "ParseConfig", reason);
            auto const transform_message =
                detail::transform_error_msg<^^invalid_source, ^^UnreachableContinuation>(
                    "MapConfig", reason);
            auto const recovery_message = detail::or_else_error_msg("RecoverConfig", reason);
            auto const error_mapper_message = detail::transform_error_error_msg("MapError", reason);
            auto const transformer_reason =
                detail::TransformIOTraits<"ParseConfig"_ss>::validate_transform(
                    ^^plain_source, ^^invalid_result_transformer);
            auto const first_result_message =
                detail::transform_error_msg<^^plain_source, ^^invalid_result_transformer>(
                    "ParseConfig", transformer_reason);
            auto const expected_first_result_message =
                std::string{"fnfelt: IO transform error: ParseConfig{(IO()) => <unknown>}: "} +
                std::string{reason} + ": " +
                std::string{display_string_of(^^invalid_result_transformer)};
            auto const application_message =
                detail::ap_error_msg<^^function_source, ^^plain_source>("ApplyConfig", reason);
            auto const repeat_message =
                detail::repeat_until_error_msg<^^invalid_source, ^^int, ^^UnreachableContinuation>(
                    "RetryConfig", reason);
            auto const stepper_message =
                detail::repeat_until_error_msg<^^plain_source, ^^int, ^^invalid_result_stepper>(
                    "StepConfig", reason);
            return create_message == expected_create_message &&
                bind_message.contains("ParseConfig") && bind_message.contains(reason) &&
                bind_message.contains("<unknown>") && transform_message.contains("MapConfig") &&
                transform_message.contains(reason) && transform_message.contains("<unknown>") &&
                recovery_message.contains("RecoverConfig") && recovery_message.contains(reason) &&
                error_mapper_message.contains("MapError") &&
                error_mapper_message.contains(reason) &&
                first_result_message == expected_first_result_message &&
                application_message.contains("ApplyConfig") &&
                application_message.contains(reason) && application_message.contains("<unknown>") &&
                repeat_message.contains("RetryConfig") && repeat_message.contains(reason) &&
                repeat_message.contains("<unknown>") && stepper_message.contains("StepConfig") &&
                stepper_message.contains(reason) && stepper_message.contains("<unknown>");
        }());
}

TEST_CASE("[pure] sequence validates each Result before collecting it as data")
{
    namespace detail = io::detail;
    using traits = detail::SequenceIOTraits<"CollectConfig"_ss>;
    using plain_source = decltype(value_io(1));
    using invalid_tuple = std::tuple<plain_source, invalid_source>;
    using invalid_array = immer::array<invalid_source>;
    constexpr std::string_view reason =
        "Result success payload must be void or a complete non-array object type";

    // These declarations leave the invalid wrappers incomplete. Validation must inspect their
    // payload metadata before collection builds a tuple or array of wrapper objects.
    static_assert(traits::validate_sequence(^^invalid_tuple) == reason);
    static_assert(traits::validate_sequence(^^invalid_array) == reason);
    static_assert(traits::validate_sequence(^^std::tuple<invalid_infallible_source>) == reason);
    using invalid_error_source = io::IO<invalid_error_action, detail::CreateIOTraits<>>;
    static_assert(
        traits::validate_sequence(^^immer::array<invalid_error_source>) ==
        "Error payload must be a complete non-array object type");
    using void_result_source = decltype(value_io(Result<void>{}));
    using unmarked_source = decltype(value_io(std::optional<std::expected<int, int>>{}));
    static_assert(
        traits::validate_sequence(^^std::tuple<void_result_source, unmarked_source>).empty());
    static_assert(traits::validate_sequence(^^immer::array<void_result_source>).empty());

    // split and fanout reuse sequence validation for the IOs returned by their continuations.
    using continuations = std::tuple<InvalidResultContinuation>;
    static_assert(
        detail::SplitIOTraits<"SplitConfig"_ss>::validate_split(
            ^^continuations, ^^std::tuple<int>) == reason);
    static_assert(
        detail::FanoutIOTraits<"FanoutConfig"_ss>::validate_fanout(^^continuations, ^^int &) ==
        reason);

    static_assert(
        [=]() consteval
        {
            auto const offending = std::string{display_string_of(dealias(^^invalid_source))};
            auto const tuple_message = detail::sequence_error_msg<^^invalid_tuple>(
                traits::name, traits::validate_sequence(^^invalid_tuple));
            auto const array_message = detail::sequence_error_msg<^^invalid_array>(
                traits::name, traits::validate_sequence(^^invalid_array));
            return tuple_message ==
                std::string{"fnfelt: IO sequence error: CollectConfig{(IO, BadSource)}: "} +
                    std::string{reason} + ": " + offending &&
                array_message ==
                std::string{"fnfelt: IO sequence error: CollectConfig{[BadSource]}: "} +
                    std::string{reason} + ": " + offending;
        }());

    // A valid Result remains one collected value: stopping or failing one element does not
    // discard its siblings or promote the collection itself into a Result.
    using result_type = Result<int, int>;
    auto tuple_values = io::sequence(
                            value_io(fnfelt::result::stopped<result_type>()),
                            value_io(fnfelt::result::error<result_type>(7)))
                            .build()
                            .sync_wait();
    auto array_values = io::sequence(
                            immer::array{
                                value_io(fnfelt::result::success<result_type>(3)),
                                value_io(fnfelt::result::stopped<result_type>())})
                            .build()
                            .sync_wait();

    static_assert(std::is_same_v<decltype(tuple_values), std::tuple<result_type, result_type>>);
    static_assert(std::is_same_v<decltype(array_values), immer::array<result_type>>);
    CHECK(fnfelt::result::is_stopped(std::get<0>(tuple_values)));
    CHECK_EQ(fnfelt::result::get_error(std::get<1>(tuple_values)), 7);
    CHECK_EQ(fnfelt::result::get_success(array_values[0]), 3);
    CHECK(fnfelt::result::is_stopped(array_values[1]));
}

TEST_CASE("[pure] traverse validates each mapped Result before invoking the continuation")
{
    namespace detail = io::detail;
    using traits = detail::TraverseIOTraits<"ReadConfigs"_ss>;
    using invalid_mapper = invalid_source (*)(int);
    using tuple_type = std::tuple<int>;
    using array_type = immer::array<int>;
    constexpr std::string_view reason =
        "Result success payload must be void or a complete non-array object type";

    // Only a mapper declaration is needed: the validator must reject its child IO before any
    // input is mapped or Immer's copy-on-write requirements inspect the mapped collection.
    static_assert(traits::validate_traverse(^^tuple_type, ^^invalid_mapper) == reason);
    static_assert(traits::validate_traverse(^^array_type, ^^invalid_mapper) == reason);
    static_assert(
        [=]() consteval
        {
            auto const tuple_message = detail::traverse_error_msg<^^tuple_type, ^^invalid_mapper>(
                traits::name, traits::validate_traverse(^^tuple_type, ^^invalid_mapper));
            auto const array_message = detail::traverse_error_msg<^^array_type, ^^invalid_mapper>(
                traits::name, traits::validate_traverse(^^array_type, ^^invalid_mapper));
            auto const suffix = std::string{reason} + ": " + std::string{display_string_of(^^int)};
            return tuple_message ==
                std::string{"fnfelt: IO traverse error: ReadConfigs{(int) => BadSource}: "} +
                    suffix &&
                array_message ==
                std::string{
                    "fnfelt: IO traverse error: ReadConfigs{immer::array<int> => BadSource}: "} +
                    suffix;
        }());

    // Input Result values remain ordinary collection elements; the mapper receives each whole
    // value, and stopped mapped Results are collected without stopping the traversal.
    using result_type = Result<int>;
    auto mapper = [](result_type value) { return value_io(std::move(value)); };
    auto values =
        io::traverse(
            std::tuple{
                fnfelt::result::success<result_type>(4), fnfelt::result::stopped<result_type>()},
            mapper)
            .build()
            .sync_wait();

    CHECK_EQ(fnfelt::result::get_success(std::get<0>(values)), 4);
    CHECK(fnfelt::result::is_stopped(std::get<1>(values)));
}

TEST_CASE("[pure] branch collection validators retain context before inferring later callbacks")
{
    namespace detail = io::detail;
    using fanout_traits = detail::FanoutIOTraits<"FanoutConfig"_ss>;
    using split_traits = detail::SplitIOTraits<"SplitConfig"_ss>;
    using traverse_traits = detail::TraverseIOTraits<"TraverseConfig"_ss>;
    using invalid_mapper = invalid_source (*)(int);
    using invalid_raw_mapper = invalid_result (*)(int);
    using plain_source = decltype(value_io(1));
    using plain_mapper = plain_source (*)(int);
    using invalid_first = std::tuple<invalid_mapper, UnreachableContinuation>;
    using invalid_second = std::tuple<plain_mapper, invalid_mapper>;
    constexpr std::string_view reason =
        "Result success payload must be void or a complete non-array object type";

    // The first malformed branch is rejected before invocation or a later deduced body.
    static_assert(fanout_traits::validate_fanout(^^invalid_first, ^^int &) == reason);
    static_assert(split_traits::validate_split(^^invalid_first, ^^std::tuple<int, int>) == reason);
    static_assert(
        fanout_traits::validate_fanout(^^std::tuple<invalid_raw_mapper>, ^^int &) == reason);
    static_assert(
        split_traits::validate_split(^^std::tuple<invalid_raw_mapper>, ^^std::tuple<int>) ==
        reason);
    static_assert(
        traverse_traits::validate_traverse(^^std::tuple<int>, ^^invalid_raw_mapper) == reason);
    static_assert(
        traverse_traits::validate_traverse(^^immer::array<int>, ^^invalid_raw_mapper) == reason);
    static_assert(
        [=]() consteval
        {
            using functions_type = std::tuple<invalid_raw_mapper>;
            auto const callback_name =
                std::string{display_string_of(dealias(^^invalid_raw_mapper))};
            auto const fanout_message =
                detail::continuations_error_msg<^^functions_type, ^^int &, true>(
                    "fanout", fanout_traits::name, reason);
            auto const split_message =
                detail::continuations_error_msg<^^functions_type, ^^std::tuple<int>, false>(
                    "split", split_traits::name, reason);
            auto const suffix = std::string{"{("} + callback_name +
                ") => IO}: " + std::string{reason} + ": " + callback_name;
            auto const traverse_suffix =
                std::string{reason} + ": " + std::string{display_string_of(^^int)};
            return fanout_message == "fnfelt: IO fanout error: FanoutConfig" + suffix &&
                split_message == "fnfelt: IO split error: SplitConfig" + suffix &&
                detail::traverse_error_msg<^^std::tuple<int>, ^^invalid_raw_mapper>(
                    traverse_traits::name, reason) ==
                "fnfelt: IO traverse error: TraverseConfig{(int) => <unknown>}: " +
                    traverse_suffix &&
                detail::traverse_error_msg<^^immer::array<int>, ^^invalid_raw_mapper>(
                    traverse_traits::name, reason) ==
                "fnfelt: IO traverse error: TraverseConfig{immer::array<int> => <unknown>}: " +
                    traverse_suffix;
        }());
    using invalid_first_input = std::tuple<int, std::string>;
    static_assert(
        traverse_traits::validate_traverse(^^invalid_first_input, ^^InvalidFirstMapper) == reason);
    static_assert(
        [=]() consteval
        {
            constexpr auto second_meta =
                remove_cvref(template_arguments_of(dealias(^^invalid_first_input))[1]);
            auto const message =
                detail::traverse_error_msg<^^invalid_first_input, ^^InvalidFirstMapper>(
                    traverse_traits::name, reason);
            return message ==
                std::string{"fnfelt: IO traverse error: TraverseConfig{(int, "} +
                std::string{detail::maybe_element_name<second_meta>()} +
                ") => BadSource}: " + std::string{reason} + ": " +
                std::string{display_string_of(^^int)};
        }());

    static_assert(
        [=]() consteval
        {
            auto const callback_name = std::string{display_string_of(dealias(^^invalid_mapper))};
            auto const plain_name = std::string{display_string_of(dealias(^^plain_mapper))};
            auto const poison_name = std::string{display_string_of(^^UnreachableContinuation)};
            auto const fanout_message =
                detail::continuations_error_msg<^^invalid_first, ^^int &, true>(
                    "fanout", fanout_traits::name, reason);
            auto const split_message =
                detail::continuations_error_msg<^^invalid_first, ^^std::tuple<int, int>, false>(
                    "split", split_traits::name, reason);
            auto const expected_suffix = std::string{"{("} + callback_name + ", " + poison_name +
                ") => IO}: " + std::string{reason} + ": " + callback_name;
            auto const second_message =
                detail::continuations_error_msg<^^invalid_second, ^^int &, true>(
                    "fanout",
                    fanout_traits::name,
                    fanout_traits::validate_fanout(^^invalid_second, ^^int &));
            return fanout_message == "fnfelt: IO fanout error: FanoutConfig" + expected_suffix &&
                split_message == "fnfelt: IO split error: SplitConfig" + expected_suffix &&
                second_message ==
                "fnfelt: IO fanout error: FanoutConfig{(" + plain_name + ", " + callback_name +
                    ") => IO}: " + std::string{reason} + ": " + callback_name;
        }());

    // Wrong callback protocols still identify the failing branch in the outer operation.
    using wrong_second = std::tuple<plain_mapper, int (*)(std::string)>;
    constexpr auto wrong_reason = fanout_traits::validate_fanout(^^wrong_second, ^^int &);
    static_assert(wrong_reason == "continuation must accept the shared input as an lvalue");
    static_assert(detail::continuations_error_msg<^^wrong_second, ^^int &, true>(
                      "fanout", fanout_traits::name, wrong_reason)
                      .ends_with(display_string_of(^^int (*)(std::string))));

    // A wrong-arity declared IO return cannot justify inspecting its deduced action body.
    using wrong_arity_mapper = unreachable_source (*)(int, int);
    using wrong_arity_functions = std::tuple<wrong_arity_mapper>;
    static_assert(
        fanout_traits::validate_fanout(^^wrong_arity_functions, ^^int &) ==
        "continuation must accept the shared input as an lvalue");
    static_assert(
        split_traits::validate_split(^^wrong_arity_functions, ^^std::tuple<int>) ==
        "continuation must accept its corresponding input value");
    static_assert(
        traverse_traits::validate_traverse(^^std::tuple<int>, ^^wrong_arity_mapper) ==
        "continuation function does not accept the tuple element's value");
    static_assert(detail::continuations_error_msg<^^wrong_arity_functions, ^^int &, true>(
                      "fanout",
                      fanout_traits::name,
                      fanout_traits::validate_fanout(^^wrong_arity_functions, ^^int &))
                      .ends_with(display_string_of(dealias(^^wrong_arity_mapper))));
    static_assert(detail::traverse_error_msg<^^std::tuple<int>, ^^wrong_arity_mapper>(
                      traverse_traits::name,
                      traverse_traits::validate_traverse(^^std::tuple<int>, ^^wrong_arity_mapper))
                      .contains("TraverseConfig{(int) => <unknown>}"));

    // Reference continuations fail their own shape validation before any generic body is read.
    constexpr auto reference_reason =
        traverse_traits::validate_traverse(^^std::tuple<int>, ^^UnreachableContinuation &);
    static_assert(reference_reason == "continuation function must not be a reference type");
    static_assert(detail::traverse_error_msg<^^std::tuple<int>, ^^UnreachableContinuation &>(
                      traverse_traits::name, reference_reason)
                      .contains("TraverseConfig{(int) => <unknown>}"));

    // Merely printing a later IO's name must not instantiate its poisoned action.
    using poisoned_tuple = std::tuple<invalid_source, unreachable_source>;
    static_assert(
        detail::SequenceIOTraits<"CollectConfig"_ss>::validate_sequence(^^poisoned_tuple) ==
        reason);
    static_assert(detail::maybe_io_name<^^unreachable_source const &>() == "UnreachableSource");
    static_assert(detail::sequence_error_msg<^^poisoned_tuple>("CollectConfig", reason)
                      .contains("{(BadSource, UnreachableSource)}"));

    // Array storage fails before a mapper can be inspected; rendering obeys that same boundary.
    using noncopyable_array = immer::array<std::unique_ptr<int>>;
    constexpr auto storage_reason =
        traverse_traits::validate_traverse(^^noncopyable_array, ^^UnreachableContinuation);
    static_assert(storage_reason == "array element must be copy constructible");
    static_assert(
        [=]() consteval
        {
            auto const message =
                detail::traverse_error_msg<^^noncopyable_array, ^^UnreachableContinuation>(
                    traverse_traits::name, storage_reason);
            return message.contains("TraverseConfig{immer::array<") &&
                message.contains(" => <unknown>}: array element must be copy constructible: ") &&
                message.ends_with(display_string_of(^^UnreachableContinuation));
        }());

    // Result inputs and outputs remain whole branch data, without routing their stop/error state.
    using result_type = Result<int>;
    auto fanout_values = io::fanout([](result_type & value) { return value_io(value); })(
                             fnfelt::result::stopped<result_type>())
                             .build()
                             .sync_wait();
    auto split_values = io::split([](result_type value) { return value_io(std::move(value)); })(
                            std::tuple{fnfelt::result::stopped<result_type>()})
                            .build()
                            .sync_wait();

    CHECK(fnfelt::result::is_stopped(std::get<0>(fanout_values)));
    CHECK(fnfelt::result::is_stopped(std::get<0>(split_values)));
}

TEST_CASE("[pure] IO and_then keeps plain chains plain and promotes explicit results")
{
    auto plain = value_io(2).and_then(add_one);
    static_assert(std::is_same_v<typename decltype(plain)::value_type, int>);
    CHECK_EQ(plain.build().sync_wait(), 3);

    auto promoted =
        value_io(2)
            .and_then([](int value)
                      { return value_io(Result<int>{std::in_place, std::in_place, value}); })
            .and_then(add_one);
    static_assert(std::is_same_v<typename decltype(promoted)::value_type, Result<int>>);
    CHECK_EQ(promoted.build().sync_wait().value().value(), 3);

    auto cancelled = value_io(skippable{std::in_place, std::in_place, 2}).and_then(add_one);
    static_assert(std::is_same_v<typename decltype(cancelled)::value_type, skippable>);
    CHECK_EQ(cancelled.build().sync_wait().value().value(), 3);

    auto failed = value_io(fallible{std::in_place, std::in_place, 2}).and_then(add_one);
    static_assert(std::is_same_v<typename decltype(failed)::value_type, fallible>);
    CHECK_EQ(failed.build().sync_wait().value().value(), 3);
}

TEST_CASE("[pure] Typed Result factories introduce routing in auto return actions")
{
    int success_calls = 0;
    int recovery_calls = 0;
    auto program = [&](int input)
    {
        return io::create(
                   [input]
                   {
                       if (input == -1)
                       {
                           return fnfelt::result::stopped<combined>();
                       }
                       if (input < 0)
                       {
                           return fnfelt::result::error<combined>(input);
                       }
                       return fnfelt::result::success<combined>(input);
                   })
            .and_then(
                [&](int value)
                {
                    ++success_calls;
                    return io::just(value + 1);
                })
            .or_else(
                [&](int error)
                {
                    ++recovery_calls;
                    return io::just(-error);
                });
    };
    auto success = program(2);
    auto error = program(-3);
    auto stopped = program(-1);
    static_assert(std::is_same_v<typename decltype(success)::value_type, Result<int>>);
    CHECK_EQ(success_calls, 0);
    CHECK_EQ(recovery_calls, 0);

    CHECK_EQ(success.build().sync_wait().value().value(), 3);
    CHECK_EQ(error.build().sync_wait().value().value(), 3);
    CHECK_FALSE(stopped.build().sync_wait());

    CHECK_EQ(success_calls, 1);
    CHECK_EQ(recovery_calls, 1);
}

TEST_CASE("[pure] IO runtime results support compile-time value queries")
{
    int executions = 0;
    auto inspect = []<class T>(T const & value)
    {
        if constexpr (fnfelt::result::is_result(value))
        {
            return fnfelt::result::get_success(value);
        }
        else
        {
            return value;
        }
    };
    auto result_io = io::create(
        [&executions]
        {
            ++executions;
            return fnfelt::result::success<combined>(7);
        });
    auto plain_io = io::create(
        [&executions]
        {
            ++executions;
            return 11;
        });
    CHECK_EQ(executions, 0);

    auto outcome = result_io.build().sync_wait();
    if constexpr (fnfelt::result::is_result(outcome))
    {
        CHECK_EQ(fnfelt::result::get_success(outcome), 7);
    }
    auto plain = plain_io.build().sync_wait();
    static_assert(!fnfelt::result::is_result(plain));

    CHECK_EQ(inspect(outcome), 7);
    CHECK_EQ(inspect(plain), 11);
    CHECK_EQ(executions, 2);
}

TEST_CASE("[pure] IO and_then joins skip and matching error capabilities")
{
    auto error_step = [](int value)
    { return value_io(fallible{std::in_place, std::in_place, value + 1}); };
    auto skip_step = [](int value)
    { return value_io(skippable{std::in_place, std::in_place, value + 1}); };
    auto forward = value_io(skippable{std::in_place, std::in_place, 2}).and_then(error_step);
    auto reverse = value_io(fallible{std::in_place, std::in_place, 2}).and_then(skip_step);
    static_assert(std::is_same_v<typename decltype(forward)::value_type, combined>);
    static_assert(std::is_same_v<typename decltype(reverse)::value_type, combined>);
    CHECK_EQ(forward.build().sync_wait().value().value(), 3);
    CHECK_EQ(reverse.build().sync_wait().value().value(), 3);

    auto same_domain = value_io(fallible{std::in_place, std::in_place, 2}).and_then(error_step);
    static_assert(std::is_same_v<typename decltype(same_domain)::value_type, fallible>);
    CHECK_EQ(same_domain.build().sync_wait().value().value(), 3);
    auto introduced = value_io(Result<int>{std::in_place, std::in_place, 2}).and_then(error_step);
    static_assert(std::is_same_v<typename decltype(introduced)::value_type, fallible>);
    CHECK_EQ(introduced.build().sync_wait().value().value(), 3);
}

TEST_CASE("[pure] IO terminal results bypass continuation invocation and effects")
{
    int invocations = 0;
    int effects = 0;
    auto continuation = [&](auto value)
    {
        static_assert(std::is_same_v<decltype(value), int>);
        ++invocations;
        return io::create(
            [&, value]
            {
                ++effects;
                return value + 1;
            });
    };
    auto skipped = value_io(combined{std::nullopt}).and_then(continuation);
    auto failed =
        value_io(combined{std::in_place, std::unexpect, fnfelt::Error{7}}).and_then(continuation);
    CHECK_EQ(invocations, 0);
    CHECK_EQ(effects, 0);
    CHECK(skipped.build().sync_wait() == std::nullopt);
    CHECK_EQ(failed.build().sync_wait().value().error().value, 7);
    CHECK_EQ(invocations, 0);
    CHECK_EQ(effects, 0);
    CHECK_EQ(
        value_io(combined{std::in_place, std::in_place, 3})
            .and_then(continuation)
            .build()
            .sync_wait()
            .value()
            .value(),
        4);
    CHECK_EQ(invocations, 1);
    CHECK_EQ(effects, 1);
}

TEST_CASE("[pure] IO and_then adopts returned results including terminal states")
{
    auto returned_error =
        value_io(skippable{std::in_place, std::in_place, 3})
            .and_then(
                []([[maybe_unused]] int value)
                { return value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{9}}); });
    static_assert(std::is_same_v<typename decltype(returned_error)::value_type, combined>);
    CHECK_EQ(returned_error.build().sync_wait().value().error().value, 9);
    auto returned_skip = value_io(fallible{std::in_place, std::in_place, 3})
                             .and_then([](int) { return value_io(skippable{std::nullopt}); });
    static_assert(std::is_same_v<typename decltype(returned_skip)::value_type, combined>);
    CHECK(returned_skip.build().sync_wait() == std::nullopt);
}

TEST_CASE("[pure] IO standard wrappers remain data outside and inside results")
{
    using ordinary_nested = std::optional<std::expected<int, int>>;
    using direct_result = std::optional<std::expected<int, fnfelt::Error<int>>>;
    auto nested_data = value_io(ordinary_nested{std::in_place, std::unexpect, 12})
                           .and_then([](auto value) { return value_io(value->error()); });
    static_assert(std::is_same_v<typename decltype(nested_data)::value_type, int>);
    CHECK_EQ(nested_data.build().sync_wait(), 12);
    auto recognized = value_io(direct_result{std::in_place, std::in_place, 4}).and_then(add_one);
    static_assert(std::is_same_v<typename decltype(recognized)::value_type, Result<int, int>>);
    CHECK_EQ(recognized.build().sync_wait()->value(), 5);

    auto optional_data = value_io(std::optional<int>{})
                             .and_then([](auto value) { return value_io(value.value_or(8)); });
    static_assert(std::is_same_v<typename decltype(optional_data)::value_type, int>);
    CHECK_EQ(optional_data.build().sync_wait(), 8);
    using expected_data = std::expected<std::optional<int>, std::optional<int>>;
    auto raw = value_io(expected_data{std::unexpect})
                   .and_then([](auto value) { return value_io(value.has_value() ? 1 : 9); });
    CHECK_EQ(raw.build().sync_wait(), 9);

    using marked_optional = Result<std::optional<int>, int>;
    auto marked = value_io(marked_optional{std::in_place, std::in_place, std::nullopt})
                      .and_then([](auto value) { return value_io(value.value_or(7)); });
    static_assert(std::is_same_v<typename decltype(marked)::value_type, combined>);
    CHECK_EQ(marked.build().sync_wait().value().value(), 7);
    using marked_expected = Result<expected_data>;
    CHECK_EQ(
        value_io(marked_expected{std::in_place, std::in_place, std::unexpect})
            .and_then([](auto value) { return value_io(value.error().value_or(6)); })
            .build()
            .sync_wait()
            .value()
            .value(),
        6);

    // Promotion does not recursively inspect the successful data returned by a plain continuation.
    auto returned = value_io(combined{std::in_place, std::in_place, 3})
                        .and_then([](int) { return value_io(expected_data{std::unexpect}); });
    using returned_type = Result<expected_data, int>;
    static_assert(std::is_same_v<typename decltype(returned)::value_type, returned_type>);
    auto result = returned.build().sync_wait();
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result.value().value().has_value());
    CHECK_FALSE(result.value().value().error().has_value());
}

TEST_CASE("[pure] IO result payloads retain tuple spreading")
{
    using source_type = Result<std::pair<int, int>, int>;
    auto spread = value_io(source_type{std::in_place, std::in_place, std::pair{2, 3}})
                      .and_then([](int left, int right) { return value_io(left + right); });
    static_assert(std::is_same_v<typename decltype(spread)::value_type, combined>);
    CHECK_EQ(spread.build().sync_wait().value().value(), 5);
    auto whole = value_io(source_type{std::in_place, std::in_place, std::pair{2, 3}})
                     .and_then([](auto pair) { return value_io(pair.first * pair.second); });
    CHECK_EQ(whole.build().sync_wait().value().value(), 6);
}

TEST_CASE("[pure] IO results support void and move-only successful payloads")
{
    using void_source = Result<void, int>;
    auto success = value_io(void_source{std::in_place}).and_then([] { return value_io(5); });
    static_assert(std::is_same_v<typename decltype(success)::value_type, combined>);
    CHECK_EQ(success.build().sync_wait().value().value(), 5);
    CHECK_EQ(
        value_io(void_source{std::in_place, std::unexpect, fnfelt::Error{8}})
            .and_then([] { return value_io(5); })
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        8);
    CHECK(
        value_io(void_source{std::nullopt})
            .and_then([] { return value_io(5); })
            .build()
            .sync_wait() == std::nullopt);

    auto returned_void = value_io(skippable{std::in_place, std::in_place, 1})
                             .and_then([](int) { return value_io(Result<void>{std::in_place}); });
    static_assert(
        std::is_same_v<typename decltype(returned_void)::value_type, Result<void, Infallible>>);
    CHECK(returned_void.build().sync_wait().has_value());
    using owned = Result<std::unique_ptr<int>, Infallible>;
    auto moved = value_io(owned{std::in_place, std::in_place, std::make_unique<int>(6)})
                     .and_then([](std::unique_ptr<int> value) { return value_io(*value); });
    CHECK_EQ(std::move(moved).build().sync_wait().value().value(), 6);
}

TEST_CASE("[pure] IO result binding supports mixed asynchronous execution")
{
    CHECK_EQ(
        async_value_io(skippable{std::in_place, std::in_place, 3})
            .and_then(add_one)
            .build()
            .sync_wait()
            .value()
            .value(),
        4);
    auto next =
        value_io(skippable{std::in_place, std::in_place, 3})
            .and_then(
                [](int value)
                { return async_value_io(fallible{std::in_place, std::in_place, value + 1}); });
    static_assert(std::is_same_v<typename decltype(next)::value_type, combined>);
    CHECK_EQ(next.build().sync_wait().value().value(), 4);
    int invocations = 0;
    auto continuation = [&](int value)
    {
        ++invocations;
        return async_value_io(value);
    };
    CHECK(
        async_value_io(combined{std::nullopt}).and_then(continuation).build().sync_wait() ==
        std::nullopt);
    CHECK_EQ(
        async_value_io(combined{std::in_place, std::unexpect, fnfelt::Error{9}})
            .and_then(continuation)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        9);
    CHECK_EQ(invocations, 0);
}

TEST_CASE("[pure] IO transports and recovers asynchronous nontrivial errors")
{
    using source_type = Result<int, std::string>;
    constexpr auto fail = [](auto, std::string error) -> lf::task<source_type>
    {
        // Construct the failure when the action executes, rather than storing an inactive
        // expected value alternative in the asynchronous action's input tuple.
        co_return source_type{std::in_place, std::unexpect, fnfelt::Error{std::move(error)}};
    };
    auto source = io::create_async(fail, std::string{"failure details"});
    int invocations = 0;
    auto propagated = source.and_then(
        [&](int value)
        {
            ++invocations;
            return value_io(value + 1);
        });
    static_assert(std::is_same_v<typename decltype(propagated)::value_type, source_type>);
    CHECK_EQ(propagated.build().sync_wait().value().error().value, "failure details");
    CHECK_EQ(invocations, 0);
    auto recovered = source.or_else([](std::string error)
                                    { return async_value_io(static_cast<int>(error.size())); });
    static_assert(std::is_same_v<typename decltype(recovered)::value_type, skippable>);
    CHECK_EQ(recovered.build().sync_wait().value().value(), 15);
}

TEST_CASE("[pure] IO or_else replaces a handled error domain")
{
    using replacement = Result<int, std::string>;
    auto recover = [](int error)
    {
        return value_io(
            replacement{std::in_place, std::unexpect, fnfelt::Error{std::to_string(error)}});
    };
    auto success = value_io(fallible{std::in_place, std::in_place, 3}).or_else(recover);
    static_assert(std::is_same_v<typename decltype(success)::value_type, replacement>);
    CHECK_EQ(success.build().sync_wait().value().value(), 3);
    CHECK_EQ(
        value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}})
            .or_else(recover)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        "4");
    auto recovered = value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}})
                         .or_else(add_one)
                         .and_then(add_one);
    static_assert(std::is_same_v<typename decltype(recovered)::value_type, Result<int>>);
    CHECK_EQ(recovered.build().sync_wait().value().value(), 6);
}

TEST_CASE("[pure] IO or_else ignores invalid overloads for impossible channels")
{
    struct ErrorRecovery
    {
        auto operator()(int error) const
        {
            return value_io(error + 1);
        }
        auto operator()() const
        {
            return std::string{"not an IO"};
        }
    };
    CHECK_EQ(
        value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}})
            .or_else(ErrorRecovery{})
            .build()
            .sync_wait()
            .value()
            .value(),
        5);
    CHECK(
        value_io(skippable{std::nullopt}).or_else(invalid_recovery).build().sync_wait() ==
        std::nullopt);
    CHECK(value_io(combined{std::nullopt}).or_else(add_one).build().sync_wait() == std::nullopt);
    // Neither signature nor diagnostic-name inference may inspect this generic body.
    CHECK_EQ(
        async_value_io(Result<int>{std::in_place, std::in_place, 3})
            .or_else(invalid_recovery)
            .build()
            .sync_wait()
            .value()
            .value(),
        3);
    auto owned =
        value_io(
            Result<std::unique_ptr<int>>{std::in_place, std::in_place, std::make_unique<int>(9)})
            .or_else(invalid_recovery);
    CHECK_EQ(*std::move(owned).build().sync_wait().value().value(), 9);
}

TEST_CASE("[pure] IO asynchronous recovery and successful void retain nominal states")
{
    CHECK_EQ(
        async_value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}})
            .or_else([](int error) { return async_value_io(error + 1); })
            .build()
            .sync_wait()
            .value()
            .value(),
        5);
    using source = Result<void, int>;
    auto recover = [](int) { return value_io(Result<void, std::string>{std::in_place}); };
    auto success = value_io(source{std::in_place}).or_else(recover);
    static_assert(
        std::is_same_v<typename decltype(success)::value_type, Result<void, std::string>>);
    CHECK(success.build().sync_wait().has_value());
    CHECK(value_io(source{std::in_place, std::unexpect, fnfelt::Error{4}})
              .or_else(recover)
              .build()
              .sync_wait()
              .has_value());
    CHECK(value_io(source{std::nullopt}).or_else(recover).build().sync_wait() == std::nullopt);
    auto skip_void = value_io(Result<void, Infallible>{std::nullopt})
                         .or_else([] { return value_io(Result<void>{std::in_place}); });
    static_assert(std::is_same_v<typename decltype(skip_void)::value_type, Result<void>>);
    CHECK_FALSE(skip_void.build().sync_wait().has_value());
}

TEST_CASE("[pure] IO mixed recovery preserves owned payloads and terminal states")
{
    using source_result = Result<std::unique_ptr<int>, std::unique_ptr<double>>;
    struct Recovery
    {
        std::unique_ptr<int> offset;

        auto operator()(std::unique_ptr<double> error) &&
        {
            return value_io(std::make_unique<int>(static_cast<int>(*error) + *offset));
        }
    };

    // A synchronous child promotes its plain payload under the asynchronous source's Result.
    auto recovered =
        async_value_io(
            source_result{std::in_place, std::unexpect, fnfelt::Error{std::make_unique<double>(4)}})
            .or_else(Recovery{std::make_unique<int>(2)});
    static_assert(
        std::is_same_v<typename decltype(recovered)::value_type, Result<std::unique_ptr<int>>>);

    auto success = std::move(recovered).build().sync_wait();

    REQUIRE((success && success->has_value()));
    CHECK_EQ(***success, 6);

    using replacement = Result<std::unique_ptr<int>, std::unique_ptr<std::string>>;
    int recoveries = 0;
    auto replace_error = [&](std::unique_ptr<double> error)
    {
        ++recoveries;
        return value_io(
            replacement{
                std::in_place,
                std::unexpect,
                fnfelt::Error{
                    std::make_unique<std::string>(std::to_string(static_cast<int>(*error)))}});
    };
    auto failed =
        async_value_io(
            source_result{std::in_place, std::unexpect, fnfelt::Error{std::make_unique<double>(7)}})
            .or_else(replace_error);
    static_assert(std::is_same_v<typename decltype(failed)::value_type, replacement>);

    // A child already producing the final Result retains its move-only error unchanged.
    auto failure = std::move(failed).build().sync_wait();

    REQUIRE((failure && !failure->has_value()));
    CHECK_EQ(*failure->error().value, "7");
    CHECK_EQ(recoveries, 1);

    auto unchanged =
        async_value_io(source_result{std::in_place, std::in_place, std::make_unique<int>(9)})
            .or_else(replace_error);
    auto stopped = async_value_io(source_result{std::nullopt}).or_else(replace_error);

    auto original = std::move(unchanged).build().sync_wait();
    auto cancelled = std::move(stopped).build().sync_wait();

    REQUIRE((original && original->has_value()));
    CHECK_EQ(***original, 9);
    CHECK(cancelled == std::nullopt);
    CHECK_EQ(recoveries, 1);
}

TEST_CASE("[pure] IO mixed recovery propagates handler and synchronous child exceptions")
{
    int recoveries = 0;
    int effects = 0;
    auto recovery = [&]([[maybe_unused]] int error) -> decltype(value_io(0))
    {
        ++recoveries;
        throw std::runtime_error{"recovery failed"};
    };
    auto throwing_handler =
        async_value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}}).or_else(recovery);
    auto throwing_child = async_value_io(fallible{std::in_place, std::unexpect, fnfelt::Error{4}})
                              .or_else(
                                  [&]([[maybe_unused]] int error)
                                  {
                                      ++recoveries;
                                      return io::create(
                                          [&]() -> int
                                          {
                                              ++effects;
                                              throw std::runtime_error{"recovery effect failed"};
                                          });
                                  });

    // Construction remains lazy, and neither exception is converted into a reported error.
    CHECK_EQ(recoveries, 0);
    CHECK_EQ(effects, 0);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(throwing_handler.build().sync_wait()),
        "recovery failed",
        std::runtime_error);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(throwing_child.build().sync_wait()),
        "recovery effect failed",
        std::runtime_error);

    CHECK_EQ(recoveries, 2);
    CHECK_EQ(effects, 1);
}

TEST_CASE("[pure] IO bind regrouping preserves success error and skip states")
{
    auto first = [](int value)
    {
        if (value == 1)
            return value_io(combined{std::in_place, std::unexpect, fnfelt::Error{7}});
        if (value == 2)
            return value_io(combined{std::nullopt});
        return value_io(combined{std::in_place, std::in_place, value});
    };
    auto second = [](int value)
    {
        if (value == 3)
            return value_io(combined{std::nullopt});
        if (value == 4)
            return value_io(combined{std::in_place, std::unexpect, fnfelt::Error{8}});
        return value_io(combined{std::in_place, std::in_place, value + 1});
    };
    auto check = [&](combined source_value)
    {
        auto source = value_io(source_value);
        auto left = source.and_then(first).and_then(second);
        auto right = source.and_then([=](int value) { return first(value).and_then(second); });
        static_assert(std::is_same_v<typename decltype(left)::value_type, combined>);
        static_assert(std::is_same_v<typename decltype(right)::value_type, combined>);
        auto lhs = left.build().sync_wait();
        auto rhs = right.build().sync_wait();
        CHECK_EQ(lhs == std::nullopt, rhs == std::nullopt);
        CHECK_EQ((lhs && !lhs->has_value()), (rhs && !rhs->has_value()));
        CHECK_EQ(lhs.has_value(), rhs.has_value());
        if (lhs && lhs->has_value())
            CHECK_EQ(lhs.value().value(), rhs.value().value());
        if ((lhs && !lhs->has_value()))
            CHECK_EQ(lhs.value().error().value, rhs.value().error().value);
    };
    check(combined{std::nullopt});
    check(combined{std::in_place, std::unexpect, fnfelt::Error{6}});
    for (int value = 1; value <= 5; ++value) check(combined{std::in_place, std::in_place, value});
}

TEST_CASE("[pure] IO branch composition moves owned errors and rvalue-only recovery")
{
    using source = Result<std::unique_ptr<int>, std::unique_ptr<double>>;
    auto bound =
        value_io(source{std::in_place, std::unexpect, fnfelt::Error{std::make_unique<double>(7)}})
            .and_then(
                [](std::unique_ptr<int> value)
                {
                    return value_io(
                        Result<int, std::unique_ptr<double>>{std::in_place, std::in_place, *value});
                });
    auto failure = std::move(bound).build().sync_wait();
    REQUIRE((failure && !failure->has_value()));
    CHECK_EQ(*failure.value().error().value, 7);
    struct Recovery
    {
        std::unique_ptr<int> offset;
        auto operator()(std::unique_ptr<double> error) &&
        {
            return value_io(std::make_unique<int>(static_cast<int>(*error) + *offset));
        }
    };
    auto recovered =
        value_io(source{std::in_place, std::unexpect, fnfelt::Error{std::make_unique<double>(4)}})
            .or_else(Recovery{std::make_unique<int>(2)});
    CHECK_EQ(*std::move(recovered).build().sync_wait().value().value(), 6);
    auto throws = value_io(skippable{std::in_place, std::in_place, 1})
                      .and_then(
                          [](int) -> decltype(value_io(1))
                          { throw std::runtime_error{"continuation failed"}; });
    CHECK_THROWS_AS(static_cast<void>(throws.build().sync_wait()), std::runtime_error);
}

TEST_CASE("[pure] IO treats nested optional and expected error domains as opaque")
{
    using error_type = std::expected<int, std::optional<std::string>>;
    using source = Result<int, error_type>;
    auto bound =
        value_io(source{std::in_place, std::unexpect, fnfelt::Error{error_type{std::unexpect}}})
            .and_then(add_one);
    static_assert(std::is_same_v<typename decltype(bound)::value_type, source>);
    auto failed = bound.build().sync_wait();
    REQUIRE((failed && !failed->has_value()));
    REQUIRE_FALSE(failed.value().error().value.has_value());
    CHECK_FALSE(failed.value().error().value.error().has_value());
    auto recovered =
        value_io(source{std::in_place, std::unexpect, fnfelt::Error{error_type{std::unexpect}}})
            .or_else([](error_type error)
                     { return value_io(error.has_value() ? error.value() : 9); });
    static_assert(std::is_same_v<typename decltype(recovered)::value_type, skippable>);
    CHECK_EQ(recovered.build().sync_wait().value().value(), 9);
}

TEST_CASE("[pure] IO transform maps only successful payloads and retains capabilities")
{
    int invocations = 0;
    auto mapper = [&](auto value)
    {
        ++invocations;
        return std::to_string(value);
    };
    auto mapped = value_io(combined{std::in_place, std::in_place, 3}).transform(mapper);
    static_assert(std::is_same_v<typename decltype(mapped)::value_type, Result<std::string, int>>);
    CHECK_EQ(invocations, 0);
    CHECK_EQ(mapped.build().sync_wait().value().value(), "3");
    CHECK_EQ(invocations, 1);
    CHECK(value_io(combined{std::nullopt}).transform(mapper).build().sync_wait() == std::nullopt);
    CHECK_EQ(
        async_value_io(combined{std::in_place, std::unexpect, fnfelt::Error{4}})
            .transform(mapper)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        4);
    CHECK_EQ(invocations, 1);
    CHECK_EQ(
        async_value_io(combined{std::in_place, std::in_place, 5})
            .transform(mapper)
            .build()
            .sync_wait()
            .value()
            .value(),
        "5");
    CHECK_EQ(invocations, 2);
    CHECK_EQ(
        value_io(Result<void>{std::in_place})
            .transform([] { return 7; })
            .build()
            .sync_wait()
            .value()
            .value(),
        7);
    CHECK_EQ(
        value_io(Result<std::pair<int, int>>{std::in_place, std::in_place, std::pair{2, 3}})
            .transform([](int left, int right) { return left + right; })
            .build()
            .sync_wait()
            .value()
            .value(),
        5);
}

TEST_CASE("[pure] IO transform keeps returned Result nested while and_then adopts it")
{
    using inner = Result<int, std::string>;
    auto mapper = []([[maybe_unused]] int value)
    { return inner{std::in_place, std::unexpect, fnfelt::Error{std::string{"data"}}}; };
    auto mapped = value_io(combined{std::in_place, std::in_place, 3}).transform(mapper);
    using nested = Result<inner, int>;
    static_assert(std::is_same_v<typename decltype(mapped)::value_type, nested>);
    auto result = mapped.build().sync_wait();
    REQUIRE(result.has_value());
    CHECK_EQ(result.value().value().value().error().value, "data");
    auto adopted = value_io(Result<int>{std::in_place, std::in_place, 3})
                       .and_then([=](int value) { return value_io(mapper(value)); });
    static_assert(std::is_same_v<typename decltype(adopted)::value_type, inner>);
    CHECK_EQ(adopted.build().sync_wait().value().error().value, "data");
    auto raw = value_io(3).transform(mapper);
    static_assert(std::is_same_v<typename decltype(raw)::value_type, inner>);
    CHECK_EQ(raw.build().sync_wait().value().error().value, "data");
    auto optional = value_io(combined{std::in_place, std::in_place, 3})
                        .transform([](int) { return std::optional<int>{}; });
    REQUIRE(optional.build().sync_wait().has_value());
    CHECK_FALSE(optional.build().sync_wait().value().value().has_value());
}

TEST_CASE("[pure] IO tap discards success payload and runs only the next IO")
{
    int effects = 0;
    auto next = io::create(
        [&]
        {
            ++effects;
            return 9;
        });
    auto tapped = value_io(combined{std::in_place, std::in_place, 3}).tap(next);
    static_assert(std::is_same_v<typename decltype(tapped)::value_type, combined>);
    CHECK_EQ(effects, 0);
    CHECK_EQ(tapped.build().sync_wait().value().value(), 9);
    CHECK_EQ(effects, 1);
    CHECK(value_io(combined{std::nullopt}).tap(next).build().sync_wait() == std::nullopt);
    CHECK_EQ(
        value_io(combined{std::in_place, std::unexpect, fnfelt::Error{4}})
            .tap(next)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        4);
    CHECK_EQ(effects, 1);
    CHECK_EQ(
        value_io(Result<void, int>{std::in_place}).tap(next).build().sync_wait().value().value(),
        9);
    CHECK_EQ(effects, 2);
    CHECK_EQ(
        async_value_io(Result<void>{std::in_place})
            .tap(async_value_io(8))
            .build()
            .sync_wait()
            .value()
            .value(),
        8);
    CHECK_EQ(value_io(std::optional<int>{}).tap(value_io(7)).build().sync_wait(), 7);
}

TEST_CASE("[pure] IO result validators reject incompatible reachable channels before substitution")
{
    using bind_traits = io::detail::AndThenIOTraits<>;
    using recovery_traits = io::detail::OrElseIOTraits<>;
    using transform_traits = io::detail::TransformIOTraits<>;
    using source_type = decltype(value_io(fallible{std::in_place, std::in_place, 1}));
    using mixed_source = decltype(value_io(combined{std::in_place, std::in_place, 1}));
    using not_io = decltype([](int) { return 1; });
    using wrong_success = decltype([](int) { return value_io(std::string{}); });
    using unmatched = decltype([](std::string) { return value_io(1); });
    using new_error =
        decltype([]([[maybe_unused]] int value)
                 { return value_io(Result<int, std::string>{std::in_place, std::in_place, 1}); });
    using cancel_error =
        decltype([]
                 { return value_io(Result<int, std::string>{std::in_place, std::in_place, 1}); });
    static_assert(!recovery_traits::validate_or_else(^^int, ^^not_io).empty());
    static_assert(!recovery_traits::validate_or_else(^^source_type, ^^not_io).empty());
    static_assert(!recovery_traits::validate_or_else(^^source_type, ^^wrong_success).empty());
    static_assert(!recovery_traits::validate_or_else(^^source_type, ^^unmatched).empty());
    static_assert(!bind_traits::validate_and_then(^^source_type, ^^new_error).empty());
    static_assert(!recovery_traits::validate_or_else(^^mixed_source, ^^cancel_error).empty());
    static_assert(recovery_traits::validate_or_else(^^source_type, ^^new_error).empty());
    static_assert(
        !recovery_traits::validate_or_else(^^io::IO<int (*)()>, ^^decltype(invalid_recovery))
             .empty());
    using tag_mapper = decltype([](int) { return std::in_place; });
    using unexpected_mapper = decltype([](int) { return std::unexpected{1}; });
    static_assert(!transform_traits::validate_transform(^^source_type, ^^tag_mapper).empty());
    static_assert(
        !transform_traits::validate_transform(^^source_type, ^^unexpected_mapper).empty());
    using tag_continuation = decltype([](int) { return value_io(std::nullopt); });
    static_assert(bind_traits::validate_and_then(^^source_type, ^^tag_continuation).empty());
}

#include <fnfelt/macros_pop.hpp>

#endif  // DOCTEST_CONFIG_DISABLE
