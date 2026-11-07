// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <immer/array.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>
#include <fnfelt/monad/state_io.hpp>
#include <fnfelt/result.hpp>
#include <fnfelt/static_string.hpp>

#include <fnfelt/macros_push.hpp>

namespace
{
namespace state_io = fnfelt::monad::state_io;
namespace reader = fnfelt::monad::reader_io;
namespace io = fnfelt::monad::io;
using fnfelt::Result;
using namespace fnfelt::literals;  // NOLINT
using result_type = Result<int, int>;

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
struct StepAction
{
    TValue value;
    int delta;

    constexpr auto operator()(this auto && self, int state)
    {
        return io::create(ValueAction{std::pair{FW(self).value, state + self.delta}});
    }
};

template <class TValue>
constexpr auto value_state(TValue value, int delta = 0)
{
    return state_io::create<int>(StepAction{std::move(value), delta});
}

template <class TValue>
struct AsyncPairFn
{
    auto operator()(auto, TValue value, int state) const -> lf::task<std::pair<TValue, int>>
    {
        co_return std::pair{std::move(value), state};
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
auto async_value_state(TValue value, int delta = 0)
{
    return state_io::create<int>(
        [value = std::move(value), delta](int state)
        { return io::create_async(AsyncPairFn<TValue>{}, value, state + delta); });
}

constexpr auto poison_handler = [](auto... args) { return (args.nonexistent_member() + ...); };

struct NonassignableState
{
    int value;

    explicit constexpr NonassignableState(int initial) : value{initial} {}
    constexpr NonassignableState(NonassignableState &&) = default;
    NonassignableState & operator=(NonassignableState &&) = delete;
};

struct TupleHandler
{
    constexpr auto operator()(std::tuple<int, int> values) const
    {
        return value_state(std::get<0>(values) + std::get<1>(values), 1);
    }

    constexpr auto operator()(int, int) const
    {
        return value_state(-1, 100);
    }
};

using invalid_result = std::optional<std::expected<int &, fnfelt::Error<int>>>;
using invalid_pair = std::pair<invalid_result, int>;
using invalid_io = io::IO<invalid_pair (*)(), io::detail::CreateIOTraits<"InvalidInner"_ss>>;
using invalid_action = invalid_io (*)(int);
using invalid_state = state_io::
    StateIO<int, invalid_action, state_io::detail::CreateStateIOTraits<"InvalidState"_ss>>;

struct PoisonContinuation
{
    template <class TValue>
    auto operator()(TValue) const
    {
        static_assert(sizeof(TValue) == 0, "invalid source inspected its continuation");
    }
};

struct PoisonStateAction
{
    template <class TState>
    auto operator()(TState) const
    {
        static_assert(sizeof(TState) == 0, "invalid Result inspected a later StateIO source");
    }
};

using poison_state =
    state_io::StateIO<int, PoisonStateAction, state_io::detail::CreateStateIOTraits<>>;

struct InvalidFirstMapper
{
    invalid_state operator()(int) const;

    template <class TValue>
    auto operator()(TValue) const
    {
        static_assert(sizeof(TValue) == 0, "invalid first Result inspected a later mapper");
    }
};

struct DifferentErrorContinuation
{
    decltype(value_state(fnfelt::result::success<Result<int, std::int64_t>>(1))) operator()(
        int) const;
};

struct CustomRecoveryTraits : state_io::detail::OrElseStateIOTraits<"CustomRecovery"_ss>
{
};

struct CustomErrorMappingTraits
    : state_io::detail::TransformErrorStateIOTraits<"CustomErrorMapping"_ss>
{
};

static_assert(
    []
    {
        auto result = value_state(fnfelt::result::success<Result<int>>(2), 1)
                          .or_else(poison_handler)
                          .transform_error(poison_handler)
                          .and_then([](int value) { return value_state(value + 1, 2); })
                          .build(4)
                          .sync_wait();
        return fnfelt::result::get_success(result.first) == 3 && result.second == 7;
    }());
static_assert(
    []
    {
        auto result =
            value_state(fnfelt::result::error<result_type>(3), 1)
                .transform_error([](int error) { return static_cast<std::int64_t>(error); })
                .or_else([](std::int64_t error)
                         { return value_state(static_cast<int>(error) + 2, 2); })
                .build(4)
                .sync_wait();
        return fnfelt::result::get_success(result.first) == 5 && result.second == 7;
    }());
}  // namespace

TEST_CASE("[pure] StateIO lifts whole Results without changing synchronous effects")
{
    auto failed = state_io::lift<int>(io::just(fnfelt::result::error<result_type>(7)));
    auto stopped = state_io::just<int>(fnfelt::result::stopped<result_type>());
    auto successful = state_io::just<int>(fnfelt::result::success<Result<void, int>>());
    static_assert(!state_io::detail::bound_state_is_async<decltype(failed)>());

    auto failure = failed.build(2).sync_wait();
    auto cancellation = stopped.build(3).sync_wait();
    auto success = successful.build(4).sync_wait();
    auto source = reader::create<int>(
        [](int state) { return io::just(fnfelt::result::error<result_type>(state)); });
    auto reader_failure = state_io::lift<int>(source).build(5).sync_wait();
    auto reader_stop =
        state_io::lift<int>(reader::lift<int>(io::just(fnfelt::result::stopped<result_type>())))
            .build(6)
            .sync_wait();

    CHECK_EQ(fnfelt::result::get_error(failure.first), 7);
    CHECK_EQ(failure.second, 2);
    CHECK(fnfelt::result::is_stopped(cancellation.first));
    CHECK_EQ(cancellation.second, 3);
    CHECK(fnfelt::result::is_success(success.first));
    CHECK_EQ(success.second, 4);
    CHECK_EQ(fnfelt::result::get_error(reader_failure.first), 5);
    CHECK_EQ(reader_failure.second, 5);
    CHECK(fnfelt::result::is_stopped(reader_stop.first));
    CHECK_EQ(reader_stop.second, 6);
}

TEST_CASE("[pure] StateIO skips child materialisation on error and stop with latest state")
{
    int source_calls = 0;
    int continuation_calls = 0;
    int child_calls = 0;
    auto continuation = [&](int value)
    {
        ++continuation_calls;
        return state_io::create<int>(
            [&, value](int state)
            {
                ++child_calls;
                return io::just(std::pair{value + 1, state + 100});
            });
    };
    auto source = [&](result_type value)
    {
        return state_io::create<int>(
            [&, value](int state)
            {
                ++source_calls;
                return io::just(std::pair{value, state + 1});
            });
    };
    auto failed = source(fnfelt::result::error<result_type>(7)).and_then(continuation);
    CHECK_EQ(source_calls, 0);
    CHECK_EQ(continuation_calls, 0);

    auto failure = failed.build(2).sync_wait();
    auto cancellation = source(fnfelt::result::stopped<result_type>())
                            .and_then(continuation)
                            .tap(value_state(9, 100))
                            .build(3)
                            .sync_wait();
    auto success =
        source(fnfelt::result::success<result_type>(5)).and_then(continuation).build(4).sync_wait();
    auto promoted =
        state_io::just<int>(2)
            .and_then([](int value)
                      { return value_state(fnfelt::result::success<result_type>(value)); })
            .build(5)
            .sync_wait();

    CHECK_EQ(fnfelt::result::get_error(failure.first), 7);
    CHECK_EQ(failure.second, 3);
    CHECK(fnfelt::result::is_stopped(cancellation.first));
    CHECK_EQ(cancellation.second, 4);
    CHECK_EQ(fnfelt::result::get_success(success.first), 6);
    CHECK_EQ(success.second, 105);
    CHECK_EQ(fnfelt::result::get_success(promoted.first), 2);
    CHECK_EQ(promoted.second, 5);
    CHECK_EQ(source_calls, 3);
    CHECK_EQ(continuation_calls, 1);
    CHECK_EQ(child_calls, 1);
}

TEST_CASE("[pure] StateIO recovery receives latest state and retains its own failed state")
{
    int calls = 0;
    auto recovery = [&calls](int error)
    {
        ++calls;
        return state_io::create<int>([error](int state)
                                     { return io::just(std::pair{error + state, state + 2}); });
    };
    auto recovered = value_state(fnfelt::result::error<result_type>(7), 1)
                         .or_else<"RecoverState"_ss>(recovery)
                         .build(3)
                         .sync_wait();
    auto successful = value_state(fnfelt::result::success<result_type>(8), 1)
                          .or_else(recovery)
                          .build(3)
                          .sync_wait();
    auto stopped = value_state(fnfelt::result::stopped<result_type>(), 1)
                       .or_else(recovery)
                       .build(3)
                       .sync_wait();
    auto failed_child =
        value_state(fnfelt::result::error<result_type>(7), 1)
            .or_else([](int error)
                     { return value_state(fnfelt::result::error<result_type>(error + 1), 2); })
            .build(3)
            .sync_wait();
    auto stopped_child =
        value_state(fnfelt::result::error<result_type>(7), 1)
            .or_else([](int) { return value_state(fnfelt::result::stopped<result_type>(), 2); })
            .build(3)
            .sync_wait();

    CHECK_EQ(fnfelt::result::get_success(recovered.first), 11);
    CHECK_EQ(recovered.second, 6);
    CHECK_EQ(fnfelt::result::get_success(successful.first), 8);
    CHECK_EQ(successful.second, 4);
    CHECK(fnfelt::result::is_stopped(stopped.first));
    CHECK_EQ(stopped.second, 4);
    CHECK_EQ(fnfelt::result::get_error(failed_child.first), 8);
    CHECK_EQ(failed_child.second, 6);
    CHECK(fnfelt::result::is_stopped(stopped_child.first));
    CHECK_EQ(stopped_child.second, 6);
    CHECK_EQ(calls, 1);
}

TEST_CASE("[pure] StateIO maps selected payloads and stores only successful values")
{
    int success_calls = 0;
    int error_calls = 0;
    int store_calls = 0;
    auto pipeline = [&](result_type value)
    {
        return value_state(value, 1)
            .transform(
                [&](int number)
                {
                    ++success_calls;
                    return number * 2;
                })
            .transform_error(
                [&](int error)
                {
                    ++error_calls;
                    return std::to_string(error);
                })
            .store(
                [&](int const & number, int state)
                {
                    ++store_calls;
                    return state + number;
                });
    };

    auto success = pipeline(fnfelt::result::success<result_type>(3)).build(4).sync_wait();
    auto failure = pipeline(fnfelt::result::error<result_type>(7)).build(4).sync_wait();
    auto stopped = pipeline(fnfelt::result::stopped<result_type>()).build(4).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(success.first), 6);
    CHECK_EQ(success.second, 11);
    CHECK_EQ(fnfelt::result::get_error(failure.first), "7");
    CHECK_EQ(failure.second, 5);
    CHECK(fnfelt::result::is_stopped(stopped.first));
    CHECK_EQ(stopped.second, 5);
    CHECK_EQ(success_calls, 1);
    CHECK_EQ(error_calls, 1);
    CHECK_EQ(store_calls, 1);
}

TEST_CASE("[pure] StateIO handles void success and direct before spread dispatch")
{
    auto source = value_state(fnfelt::result::success<Result<void, int>>(), 1);
    auto continued = source.and_then([] { return value_state(5, 2); }).build(3).sync_wait();
    auto mapped = source.transform([] { return 6; }).build(3).sync_wait();
    auto stored = source.store([](int state) { return state + 2; }).build(3).sync_wait();
    auto tapped = source.tap(value_state(7, 2)).build(3).sync_wait();
    auto void_recovery =
        value_state(fnfelt::result::error<Result<void, int>>(3), 1)
            .or_else([](int) { return value_state(fnfelt::result::success<Result<void>>(), 2); })
            .build(3)
            .sync_wait();
    using tuple_type = std::tuple<int, int>;
    auto success = value_state(fnfelt::result::success<Result<tuple_type>>(tuple_type{2, 3}));
    auto failure = value_state(fnfelt::result::error<Result<int, tuple_type>>(tuple_type{2, 3}));
    auto spread_success =
        success.and_then([](int first, int second) { return value_state(first + second, 1); })
            .build(3)
            .sync_wait();
    auto spread_error =
        failure.or_else([](int first, int second) { return value_state(first + second, 1); })
            .build(3)
            .sync_wait();
    auto direct_success = success.and_then(TupleHandler{}).build(3).sync_wait();
    auto direct_error = failure.or_else(TupleHandler{}).build(3).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(continued.first), 5);
    CHECK_EQ(continued.second, 6);
    CHECK_EQ(fnfelt::result::get_success(mapped.first), 6);
    CHECK_EQ(mapped.second, 4);
    CHECK(fnfelt::result::is_success(stored.first));
    CHECK_EQ(stored.second, 6);
    CHECK_EQ(fnfelt::result::get_success(tapped.first), 7);
    CHECK_EQ(tapped.second, 6);
    CHECK(fnfelt::result::is_success(void_recovery.first));
    CHECK_EQ(void_recovery.second, 6);
    CHECK_EQ(fnfelt::result::get_success(spread_success.first), 5);
    CHECK_EQ(fnfelt::result::get_success(spread_error.first), 5);
    CHECK_EQ(fnfelt::result::get_success(direct_success.first), 5);
    CHECK_EQ(fnfelt::result::get_success(direct_error.first), 5);
    CHECK_EQ(direct_success.second, 4);
    CHECK_EQ(direct_error.second, 4);
}

TEST_CASE("[pure] StateIO preserves nonassignable state and moves successful payloads")
{
    auto source = state_io::lift<NonassignableState>(io::create(
        ValueAction{
            fnfelt::result::success<Result<std::unique_ptr<int>>>(std::make_unique<int>(9))}));
    auto computation = std::move(source)
                           .or_else(poison_handler)
                           .transform_error(poison_handler)
                           .store([](std::unique_ptr<int> const & value, NonassignableState state)
                                  { return NonassignableState{state.value + *value}; })
                           .and_then([](std::unique_ptr<int> value)
                                     { return state_io::just<NonassignableState>(*value); });

    auto output = std::move(computation).build(NonassignableState{1}).sync_wait();
    auto failure =
        state_io::lift<NonassignableState>(io::just(fnfelt::result::error<result_type>(4)))
            .or_else([](int error) { return state_io::just<NonassignableState>(error + 1); })
            .build(NonassignableState{2})
            .sync_wait();

    CHECK_EQ(fnfelt::result::get_success(output.first), 9);
    CHECK_EQ(output.second.value, 10);
    CHECK_EQ(fnfelt::result::get_success(failure.first), 5);
    CHECK_EQ(failure.second.value, 2);
}

TEST_CASE("[pure] StateIO keeps nested Results and standard wrappers as data")
{
    auto optional_value =
        value_state(std::optional<int>{})
            .and_then([](std::optional<int> value) { return value_state(value.has_value()); })
            .build(3)
            .sync_wait();
    auto expected_value =
        value_state(std::expected<int, int>{std::unexpect, 5})
            .transform([](std::expected<int, int> value) { return value.error(); })
            .build(3)
            .sync_wait();
    auto nested = value_state(fnfelt::result::success<result_type>(3))
                      .transform([](int) { return fnfelt::result::stopped<result_type>(); })
                      .build(3)
                      .sync_wait();
    auto nested_error =
        value_state(fnfelt::result::error<result_type>(3))
            .transform_error([](int) { return fnfelt::result::stopped<result_type>(); })
            .build(3)
            .sync_wait();

    CHECK_FALSE(optional_value.first);
    CHECK_EQ(expected_value.first, 5);
    CHECK(fnfelt::result::is_success(nested.first));
    CHECK(fnfelt::result::is_stopped(fnfelt::result::get_success(nested.first)));
    CHECK(fnfelt::result::is_error(nested_error.first));
    CHECK(fnfelt::result::is_stopped(fnfelt::result::get_error(nested_error.first)));
}

TEST_CASE("[pure] StateIO tuple and array sequences collect whole Results and all state updates")
{
    auto successful = value_state(fnfelt::result::success<result_type>(1), 1);
    auto failed = value_state(fnfelt::result::error<result_type>(2), 2);
    auto stopped = value_state(fnfelt::result::stopped<result_type>(), 3);

    auto tuple = state_io::sequence<int>(successful, failed, stopped).build(4).sync_wait();
    auto array =
        state_io::sequence<int>(immer::array{successful, failed, stopped}).build(4).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(std::get<0>(tuple.first)), 1);
    CHECK_EQ(fnfelt::result::get_error(std::get<1>(tuple.first)), 2);
    CHECK(fnfelt::result::is_stopped(std::get<2>(tuple.first)));
    CHECK_EQ(tuple.second, 10);
    REQUIRE_EQ(array.first.size(), 3);
    CHECK_EQ(fnfelt::result::get_success(array.first[0]), 1);
    CHECK_EQ(fnfelt::result::get_error(array.first[1]), 2);
    CHECK(fnfelt::result::is_stopped(array.first[2]));
    CHECK_EQ(array.second, 10);
}

TEST_CASE("[pure] StateIO exposes named and custom traits recovery free functions")
{
    auto source = value_state(fnfelt::result::error<result_type>(3), 1);
    auto recovery = [](int error) { return value_state(error + 1, 2); };
    auto mapper = [](int error) { return error * 2; };
    auto custom = state_io::or_else<CustomRecoveryTraits>(source, recovery);
    auto named = state_io::or_else<"NamedRecovery"_ss>(source, recovery);
    auto custom_map = state_io::transform_error<CustomErrorMappingTraits>(source, mapper);
    auto named_map = state_io::transform_error<"NamedMapping"_ss>(source, mapper);

    CHECK_EQ(fnfelt::result::get_success(custom.build(3).sync_wait().first), 4);
    CHECK_EQ(fnfelt::result::get_success(named.build(3).sync_wait().first), 4);
    CHECK_EQ(fnfelt::result::get_error(custom_map.build(3).sync_wait().first), 6);
    CHECK_EQ(fnfelt::result::get_error(named_map.build(3).sync_wait().first), 6);
}

TEST_CASE("[pure] StateIO routes Result outcomes through mixed asynchronous effects")
{
    lf::lazy_pool pool{2};
    auto computation =
        async_value_state(fnfelt::result::error<result_type>(4), 1)
            .transform_error([](int error) { return static_cast<std::int64_t>(error); })
            .or_else([](std::int64_t error)
                     { return async_value_state(static_cast<int>(error) + 1, 2); })
            .and_then([](int value) { return value_state(value * 2, 3); });
    auto sync_source_async_child =
        value_state(fnfelt::result::success<result_type>(2), 1)
            .and_then([](int value) { return async_value_state(value + 1, 2); });
    auto stopped = async_value_state(fnfelt::result::stopped<result_type>(), 1)
                       .or_else([](int error) { return async_value_state(error, 2); });

    auto output = std::move(computation).build(3).sync_wait(pool);
    auto success = std::move(sync_source_async_child).build(3).sync_wait(pool);
    auto cancellation = std::move(stopped).build(3).sync_wait(pool);

    CHECK_EQ(fnfelt::result::get_success(output.first), 10);
    CHECK_EQ(output.second, 9);
    CHECK_EQ(fnfelt::result::get_success(success.first), 3);
    CHECK_EQ(success.second, 6);
    CHECK(fnfelt::result::is_stopped(cancellation.first));
    CHECK_EQ(cancellation.second, 4);
}

TEST_CASE("[pure] StateIO asynchronous lifting retains whole Results and owned state")
{
    lf::lazy_pool pool{2};
    auto failed = state_io::lift<NonassignableState>(
        io::create_async(AsyncValueFn<result_type>{}, fnfelt::result::error<result_type>(7)));
    static_assert(state_io::detail::bound_state_is_async<decltype(failed)>());
    auto stopped_reader = reader::lift<int>(
        io::create_async(AsyncValueFn<result_type>{}, fnfelt::result::stopped<result_type>()));
    auto stopped = state_io::lift<int>(stopped_reader);
    auto successful = state_io::lift<int>(io::create_async(
        AsyncValueFn<Result<void, int>>{}, fnfelt::result::success<Result<void, int>>()));

    // These lifts await the unwrapped source IO and attach state even without a payload.
    auto failure = std::move(failed).build(NonassignableState{3}).sync_wait(pool);
    auto cancellation = stopped.build(4).sync_wait(pool);
    auto success = successful.build(5).sync_wait(pool);

    CHECK_EQ(fnfelt::result::get_error(failure.first), 7);
    CHECK_EQ(failure.second.value, 3);
    CHECK(fnfelt::result::is_stopped(cancellation.first));
    CHECK_EQ(cancellation.second, 4);
    CHECK(fnfelt::result::is_success(success.first));
    CHECK_EQ(success.second, 5);
}

TEST_CASE("[pure] StateIO Result validation preserves operation context before unsafe completion")
{
    namespace detail = state_io::detail;
    constexpr auto reason =
        std::string_view{"Result success payload must be void or a complete non-array object type"};
    using source_type = decltype(value_state(fnfelt::result::success<result_type>(1)));
    using plain_source = decltype(value_state(1));
    using invalid_continuation = invalid_state (*)(int);
    using different_success = decltype([](int) { return value_state(std::string{"value"}); });
    using tag_mapper = decltype([](int) { return std::in_place; });
    using unexpected_mapper = decltype([](int) { return std::unexpected{1}; });

    static_assert(
        detail::CreateStateIOTraits<>::validate_action(^^invalid_action, ^^int) == reason);
    static_assert(
        detail::AndThenStateIOTraits<>::validate_and_then(^^invalid_state, ^^PoisonContinuation) ==
        reason);
    static_assert(
        detail::OrElseStateIOTraits<>::validate_or_else(^^invalid_state, ^^PoisonContinuation) ==
        reason);
    static_assert(
        detail::TransformStateIOTraits<>::validate_transform(
            ^^invalid_state, ^^PoisonContinuation) == reason);
    static_assert(
        detail::TransformErrorStateIOTraits<>::validate_transform_error(
            ^^invalid_state, ^^PoisonContinuation) == reason);
    static_assert(
        detail::StoreStateIOTraits<>::validate_store(^^invalid_state, ^^PoisonContinuation) ==
        reason);
    static_assert(
        detail::SequenceStateIOTraits<>::validate_element(^^invalid_state, ^^int) == reason);
    static_assert(
        detail::AndThenStateIOTraits<>::validate_and_then(^^source_type, ^^invalid_continuation) ==
        reason);
    static_assert(
        detail::OrElseStateIOTraits<>::validate_or_else(^^source_type, ^^invalid_continuation) ==
        reason);
    static_assert(!detail::AndThenStateIOTraits<>::validate_and_then(
                       ^^source_type, ^^DifferentErrorContinuation)
                       .empty());
    static_assert(
        !detail::OrElseStateIOTraits<>::validate_or_else(^^source_type, ^^different_success)
             .empty());
    static_assert(
        detail::OrElseStateIOTraits<>::validate_or_else(^^plain_source, ^^PoisonContinuation) ==
        "source StateIO does not produce a Result");
    static_assert(
        !detail::TransformStateIOTraits<>::validate_transform(^^source_type, ^^tag_mapper).empty());
    static_assert(
        !detail::TransformStateIOTraits<>::validate_transform(^^source_type, ^^unexpected_mapper)
             .empty());
    static_assert(
        detail::TransformStateIOTraits<>::validate_transform(^^plain_source, ^^tag_mapper).empty());
    constexpr auto has_context = [=] consteval
    {
        auto const message = detail::or_else_error_msg<^^invalid_state, ^^PoisonContinuation>(
            "RecoverConfig", reason);
        auto const collision = detail::or_else_error_msg<^^invalid_state, ^^PoisonContinuation>(
            "and_then error", reason);
        auto const map_collision =
            detail::transform_error_error_msg<^^invalid_state, ^^PoisonContinuation>(
                "transform error", reason);
        return message.contains(
                   "RecoverConfig or_else error: RecoverConfig{InvalidState() => "
                   "PoisonContinuation}") &&
            message.contains(reason) &&
            collision.starts_with("fnfelt: and_then error or_else error: and_then error{") &&
            map_collision.starts_with(
                "fnfelt: transform error transform_error error: transform error{");
    }();

    CHECK(has_context);
}

TEST_CASE("[pure] StateIO collection and control validators reject malformed Results first")
{
    namespace detail = state_io::detail;
    constexpr auto reason =
        std::string_view{"Result success payload must be void or a complete non-array object type"};
    using plain_source = decltype(value_state(1));
    using choice_source = decltype(value_state(std::expected<int, int>{1}));
    using poison_source = decltype(value_state(PoisonContinuation{}));
    using invalid_mapper = invalid_state (*)(int);
    using invalid_first = std::tuple<invalid_mapper, PoisonContinuation>;
    using invalid_value_io = io::IO<invalid_result (*)(), io::detail::CreateIOTraits<>>;

    // Reject the first malformed domain before invoking any later generic callback.
    static_assert(detail::LiftStateIOTraits<>::validate_lift(^^invalid_value_io, ^^int) == reason);
    static_assert(
        detail::SelectStateIOTraits<>::validate_select(^^invalid_state, ^^plain_source) == reason);
    static_assert(
        detail::SelectStateIOTraits<>::validate_select(^^invalid_state, ^^poison_state) == reason);
    static_assert(
        detail::SelectStateIOTraits<>::validate_select(^^choice_source, ^^invalid_state) == reason);
    static_assert(
        detail::ApStateIOTraits<>::validate_ap(^^invalid_state, ^^plain_source) == reason);
    static_assert(
        detail::ApStateIOTraits<>::validate_ap(^^invalid_state, ^^poison_state) == reason);
    static_assert(
        detail::ApStateIOTraits<>::validate_ap(^^poison_source, ^^invalid_state) == reason);
    static_assert(
        detail::RepeatUntilStateIOTraits<>::validate_repeat_until(
            ^^invalid_state, ^^int, ^^PoisonContinuation) == reason);
    static_assert(
        detail::FanoutStateIOTraits<>::validate_fanout(^^invalid_first, ^^int &, ^^int) == reason);
    static_assert(
        detail::SplitStateIOTraits<>::validate_split(
            ^^invalid_first, ^^std::tuple<int, int>, ^^int) == reason);
    static_assert(
        detail::TraverseStateIOTraits<>::validate_traverse(
            ^^std::tuple<int, std::string>, ^^InvalidFirstMapper, ^^int) == reason);
    static_assert(
        detail::TraverseStateIOTraits<>::validate_traverse(
            ^^immer::array<int>, ^^invalid_mapper, ^^int) == reason);
}

#include <fnfelt/macros_pop.hpp>

#endif  // DOCTEST_CONFIG_DISABLE
