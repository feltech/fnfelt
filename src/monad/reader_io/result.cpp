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
#include <tuple>
#include <type_traits>
#include <utility>

#include <immer/array.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>
#include <fnfelt/result.hpp>
#include <fnfelt/static_string.hpp>

#include <fnfelt/macros_push.hpp>

namespace
{
namespace reader = fnfelt::monad::reader_io;
namespace io = fnfelt::monad::io;
using fnfelt::Result;
using namespace fnfelt::literals;  // NOLINT

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
constexpr auto value_reader(TValue value)
{
    return reader::lift<int>(io::create(ValueAction<TValue>{std::move(value)}));
}

template <class TValue>
struct AsyncValueFn
{
    auto operator()(auto, TValue value) const -> lf::task<TValue>
    {
        co_return std::move(value);
    }
};

template <class TValue>
auto async_value_reader(TValue value)
{
    return reader::lift<int>(io::create_async(AsyncValueFn<TValue>{}, std::move(value)));
}

constexpr auto poison_handler = [](auto... args) { return (args.nonexistent_member() + ...); };
using result_type = Result<int, int>;
using invalid_result = std::optional<std::expected<int &, fnfelt::Error<int>>>;
using invalid_io = io::IO<invalid_result (*)(), io::detail::CreateIOTraits<"InvalidInner"_ss>>;
using invalid_reader_action = invalid_io (*)(int);
using invalid_reader = reader::
    ReaderIO<int, invalid_reader_action, reader::detail::CreateReaderIOTraits<"InvalidReader"_ss>>;

struct PoisonContinuation
{
    template <class TValue>
    auto operator()(TValue) const
    {
        static_assert(sizeof(TValue) == 0, "invalid source inspected its continuation");
    }
};

struct TupleHandler
{
    constexpr auto operator()(std::tuple<int, int> value) const
    {
        return value_reader(std::get<0>(value) + std::get<1>(value));
    }

    constexpr auto operator()(int, int) const
    {
        return value_reader(-1);
    }
};

struct DifferentErrorContinuation
{
    decltype(value_reader(fnfelt::result::success<Result<int, std::int64_t>>(1))) operator()(
        int) const;
};

struct CustomRecoveryTraits : reader::detail::OrElseReaderIOTraits<"CustomRecovery"_ss>
{
};

struct CustomErrorMappingTraits : reader::detail::TransformErrorReaderIOTraits<"CustomMapping"_ss>
{
};

static_assert(
    value_reader(Result<int>{std::in_place, std::in_place, 2})
        .or_else(poison_handler)
        .transform_error(poison_handler)
        .and_then([](int value) { return value_reader(value + 1); })
        .build(0)
        .sync_wait()
        .value()
        .value() == 3);
static_assert(
    value_reader(fnfelt::result::error<result_type>(3))
        .transform_error([](int error) { return static_cast<std::int64_t>(error); })
        .or_else([](std::int64_t error) { return value_reader(static_cast<int>(error) + 2); })
        .build(0)
        .sync_wait()
        .value()
        .value() == 5);
}  // namespace

TEST_CASE("[pure] ReaderIO promotes plain results and skips unsuccessful continuations")
{
    int calls = 0;
    auto continuation = [&calls](int value)
    {
        ++calls;
        return value_reader(fnfelt::result::success<result_type>(value + 1));
    };
    auto promoted = reader::just<int>(2).and_then(continuation).build(9).sync_wait();
    CHECK_EQ(fnfelt::result::get_success(promoted), 3);
    CHECK_EQ(calls, 1);

    auto failed = value_reader(fnfelt::result::error<result_type>(7))
                      .and_then(continuation)
                      .transform(
                          [&calls](int value)
                          {
                              ++calls;
                              return value;
                          })
                      .build(9)
                      .sync_wait();
    auto stopped = value_reader(fnfelt::result::stopped<result_type>())
                       .and_then(continuation)
                       .build(9)
                       .sync_wait();

    CHECK_EQ(fnfelt::result::get_error(failed), 7);
    CHECK(fnfelt::result::is_stopped(stopped));
    CHECK_EQ(calls, 1);
}

TEST_CASE("[pure] ReaderIO recovery uses its own copy of the original environment")
{
    auto source = reader::create<int>(
        [](int state)
        {
            state += 10;
            return io::just(fnfelt::result::error<result_type>(state));
        });
    int calls = 0;
    auto recover = [&calls](int error)
    {
        ++calls;
        return reader::create<int>([error](int state) { return io::just(error + state); });
    };
    auto recovered_reader = source.or_else<"RecoverReader"_ss>(recover);
    CHECK_EQ(calls, 0);

    auto recovered = recovered_reader.build(3).sync_wait();
    auto replayed = recovered_reader.build(4).sync_wait();
    auto successful =
        value_reader(fnfelt::result::success<result_type>(8)).or_else(recover).build(3).sync_wait();
    auto stopped =
        value_reader(fnfelt::result::stopped<result_type>()).or_else(recover).build(3).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(recovered), 16);
    CHECK_EQ(fnfelt::result::get_success(replayed), 18);
    CHECK_EQ(fnfelt::result::get_success(successful), 8);
    CHECK(fnfelt::result::is_stopped(stopped));
    CHECK_EQ(calls, 2);
}

TEST_CASE("[pure] ReaderIO maps errors and leaves nested Result payloads opaque")
{
    int calls = 0;
    auto mapper = [&calls](int error)
    {
        ++calls;
        return std::to_string(error);
    };
    auto failed = value_reader(fnfelt::result::error<result_type>(7))
                      .transform_error<"MapReaderError"_ss>(mapper)
                      .build(0)
                      .sync_wait();
    auto successful = value_reader(fnfelt::result::success<result_type>(8))
                          .transform_error(mapper)
                          .build(0)
                          .sync_wait();
    auto stopped = value_reader(fnfelt::result::stopped<result_type>())
                       .transform_error(mapper)
                       .build(0)
                       .sync_wait();
    auto nested = value_reader(fnfelt::result::success<result_type>(8))
                      .transform([](int) { return fnfelt::result::stopped<result_type>(); })
                      .build(0)
                      .sync_wait();

    CHECK_EQ(fnfelt::result::get_error(failed), "7");
    CHECK_EQ(fnfelt::result::get_success(successful), 8);
    CHECK(fnfelt::result::is_stopped(stopped));
    CHECK(fnfelt::result::is_success(nested));
    CHECK(fnfelt::result::is_stopped(fnfelt::result::get_success(nested)));
    CHECK_EQ(calls, 1);
}

TEST_CASE("[pure] ReaderIO handles void successes and preserves tuple dispatch precedence")
{
    auto void_reader = value_reader(fnfelt::result::success<Result<void, int>>());
    auto continued = void_reader.and_then([] { return value_reader(5); }).build(0).sync_wait();
    auto mapped = void_reader.transform([] { return 6; }).build(0).sync_wait();
    auto tapped = void_reader.tap(value_reader(7)).build(0).sync_wait();
    auto void_recovery =
        value_reader(fnfelt::result::error<Result<void, int>>(3))
            .or_else([](int) { return value_reader(fnfelt::result::success<Result<void>>()); })
            .build(0)
            .sync_wait();
    using tuple_type = std::tuple<int, int>;
    auto success = value_reader(fnfelt::result::success<Result<tuple_type>>(tuple_type{2, 3}));
    auto failure = value_reader(fnfelt::result::error<Result<int, tuple_type>>(tuple_type{2, 3}));
    auto spread_success =
        success.and_then([](int first, int second) { return value_reader(first + second); })
            .build(0)
            .sync_wait();
    auto direct_success = success.and_then(TupleHandler{}).build(0).sync_wait();
    auto spread_error =
        failure.or_else([](int first, int second) { return value_reader(first + second); })
            .build(0)
            .sync_wait();
    auto direct_error = failure.or_else(TupleHandler{}).build(0).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(continued), 5);
    CHECK_EQ(fnfelt::result::get_success(mapped), 6);
    CHECK_EQ(fnfelt::result::get_success(tapped), 7);
    CHECK(fnfelt::result::is_success(void_recovery));
    CHECK_EQ(fnfelt::result::get_success(spread_success), 5);
    CHECK_EQ(fnfelt::result::get_success(direct_success), 5);
    CHECK_EQ(fnfelt::result::get_success(spread_error), 5);
    CHECK_EQ(fnfelt::result::get_success(direct_error), 5);
}

TEST_CASE("[pure] ReaderIO leaves impossible handlers unqueried with a move-only environment")
{
    auto source =
        reader::lift<std::unique_ptr<int>>(io::just(fnfelt::result::success<Result<int>>(4)));
    auto computation = std::move(source).or_else(poison_handler).transform_error(poison_handler);

    auto output = std::move(computation).build(std::make_unique<int>(1)).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(output), 4);
}

TEST_CASE("[pure] ReaderIO moves successful payloads through Result continuations")
{
    auto computation =
        value_reader(
            fnfelt::result::success<Result<std::unique_ptr<int>, int>>(std::make_unique<int>(9)))
            .and_then([](std::unique_ptr<int> value) { return value_reader(*value); });

    auto output = std::move(computation).build(0).sync_wait();

    CHECK_EQ(fnfelt::result::get_success(output), 9);
}

TEST_CASE("[pure] ReaderIO keeps collected Results as data")
{
    auto output = reader::sequence<int>(std::tuple{
                                            value_reader(fnfelt::result::error<result_type>(2)),
                                            value_reader(fnfelt::result::stopped<result_type>())})
                      .build(0)
                      .sync_wait();

    CHECK_EQ(fnfelt::result::get_error(std::get<0>(output)), 2);
    CHECK(fnfelt::result::is_stopped(std::get<1>(output)));

    auto element = value_reader(fnfelt::result::error<result_type>(3));
    auto array_output = reader::sequence<int>(immer::array{element, element}).build(0).sync_wait();

    CHECK_EQ(array_output.size(), 2);
    CHECK_EQ(fnfelt::result::get_error(array_output[0]), 3);
    CHECK_EQ(fnfelt::result::get_error(array_output[1]), 3);
}

TEST_CASE("[pure] ReaderIO exposes named and custom-traits recovery free functions")
{
    auto source = value_reader(fnfelt::result::error<result_type>(3));
    auto recovery = [](int error) { return value_reader(error + 1); };
    auto mapper = [](int error) { return error * 2; };
    auto custom = reader::or_else<CustomRecoveryTraits>(source, recovery);
    auto named = reader::or_else<"NamedRecovery"_ss>(source, recovery);
    auto custom_map = reader::transform_error<CustomErrorMappingTraits>(source, mapper);
    auto named_map = reader::transform_error<"NamedMapping"_ss>(source, mapper);

    CHECK_EQ(fnfelt::result::get_success(custom.build(0).sync_wait()), 4);
    CHECK_EQ(fnfelt::result::get_success(named.build(0).sync_wait()), 4);
    CHECK_EQ(fnfelt::result::get_error(custom_map.build(0).sync_wait()), 6);
    CHECK_EQ(fnfelt::result::get_error(named_map.build(0).sync_wait()), 6);
}

TEST_CASE("[pure] ReaderIO treats standard wrappers and mapper-returned Results as payload data")
{
    auto optional_value =
        value_reader(std::optional<int>{})
            .and_then([](std::optional<int> value) { return value_reader(value.has_value()); })
            .build(0)
            .sync_wait();
    auto expected_value =
        value_reader(std::expected<int, int>{std::unexpect, 5})
            .transform([](std::expected<int, int> value) { return value.error(); })
            .build(0)
            .sync_wait();
    auto error_value =
        value_reader(fnfelt::result::error<result_type>(3))
            .transform_error([](int) { return fnfelt::result::stopped<result_type>(); })
            .build(0)
            .sync_wait();

    CHECK_FALSE(optional_value);
    CHECK_EQ(expected_value, 5);
    CHECK(fnfelt::result::is_error(error_value));
    CHECK(fnfelt::result::is_stopped(fnfelt::result::get_error(error_value)));
}

TEST_CASE("[pure] ReaderIO routes Result outcomes through mixed asynchronous effects")
{
    lf::lazy_pool pool{2};
    auto computation =
        async_value_reader(fnfelt::result::error<result_type>(4))
            .transform_error([](int error) { return static_cast<std::int64_t>(error); })
            .or_else([](std::int64_t error)
                     { return async_value_reader(static_cast<int>(error) + 1); })
            .and_then([](int value) { return value_reader(value * 2); });

    auto output = std::move(computation).build(3).sync_wait(pool);

    CHECK_EQ(fnfelt::result::get_success(output), 10);
}

TEST_CASE("[pure] ReaderIO Result validators retain named context and stop before callbacks")
{
    constexpr auto reason =
        std::string_view{"Result success payload must be void or a complete non-array object type"};
    using source_type = decltype(value_reader(fnfelt::result::success<result_type>(1)));
    using plain_source = decltype(value_reader(1));
    using different_error = DifferentErrorContinuation;
    using different_success = decltype([](int) { return value_reader(std::string{"value"}); });
    using invalid_continuation = invalid_reader (*)(int);
    namespace detail = reader::detail;

    static_assert(
        detail::CreateReaderIOTraits<>::validate_action(^^invalid_reader_action, ^^int) == reason);
    static_assert(detail::LiftReaderIOTraits<>::validate_lift(^^invalid_io) == reason);
    static_assert(
        detail::AndThenReaderIOTraits<>::validate_and_then(
            ^^invalid_reader, ^^PoisonContinuation) == reason);
    static_assert(
        detail::OrElseReaderIOTraits<>::validate_or_else(^^invalid_reader, ^^PoisonContinuation) ==
        reason);
    static_assert(
        detail::TransformErrorReaderIOTraits<>::validate_transform_error(
            ^^invalid_reader, ^^PoisonContinuation) == reason);
    static_assert(
        reader::SequenceReaderIOTraits<>::validate_element(^^invalid_reader, ^^int) == reason);
    static_assert(
        detail::AndThenReaderIOTraits<>::validate_and_then(^^source_type, ^^invalid_continuation) ==
        reason);
    static_assert(
        detail::OrElseReaderIOTraits<>::validate_or_else(^^source_type, ^^invalid_continuation) ==
        reason);
    static_assert(
        !detail::AndThenReaderIOTraits<>::validate_and_then(^^source_type, ^^different_error)
             .empty());
    static_assert(
        !detail::OrElseReaderIOTraits<>::validate_or_else(^^source_type, ^^different_success)
             .empty());
    static_assert(
        detail::OrElseReaderIOTraits<>::validate_or_else(^^plain_source, ^^PoisonContinuation) ==
        "source ReaderIO does not produce a Result");
    constexpr auto has_context = [=] consteval
    {
        auto const message = detail::or_else_error_msg<^^invalid_reader, ^^PoisonContinuation>(
            "RecoverConfig", reason);
        return message.contains(
                   "RecoverConfig or_else error: RecoverConfig{InvalidReader() => "
                   "PoisonContinuation}") &&
            message.contains(reason);
    }();
    constexpr auto preserves_names = [=] consteval
    {
        auto const collision = detail::or_else_error_msg<^^invalid_reader, ^^PoisonContinuation>(
            "and_then error", reason);
        auto const map_collision =
            detail::transform_error_error_msg<^^invalid_reader, ^^PoisonContinuation>(
                "transform error", reason);
        return collision.starts_with("fnfelt: and_then error or_else error: and_then error{") &&
            map_collision.starts_with(
                "fnfelt: transform error transform_error error: transform error{");
    }();

    CHECK(has_context);
    CHECK(preserves_names);
}

#include <fnfelt/macros_pop.hpp>

#endif  // DOCTEST_CONFIG_DISABLE
