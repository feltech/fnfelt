// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/result.hpp>
#include <fnfelt/static_string.hpp>

#include <fnfelt/macros_push.hpp>

namespace
{
namespace io = fnfelt::monad::io;
using fnfelt::Infallible;
using fnfelt::Result;
using combined = Result<int, int>;

template <class TValue>
struct ErrorMapValueAction
{
    TValue value;
    constexpr auto operator()(this auto && self)
    {
        return FW(self).value;
    }
};

template <class TValue>
constexpr auto source_io(TValue value)
{
    return io::create(ErrorMapValueAction<TValue>{std::move(value)});
}

template <class TValue>
struct ErrorMapAsyncFn
{
    auto operator()(auto, TValue value) const -> lf::task<TValue>
    {
        co_return std::move(value);
    }
};

template <class TValue>
constexpr auto async_source_io(TValue value)
{
    return io::create_async(ErrorMapAsyncFn<TValue>{}, std::move(value));
}

constexpr auto invalid_mapper = [](auto error) { return error.no_such_member(); };
constexpr auto increment_error = [](int error) { return error + 1; };
struct CustomResultTraits : fnfelt::detail::ResultTraits
{
};
using custom_infallible = fnfelt::detail::ResultType<int, Infallible, CustomResultTraits>::type;
using custom_noop = decltype(source_io(custom_infallible{std::in_place, std::in_place, 3})
                                 .transform_error(invalid_mapper));
static_assert(std::is_same_v<typename custom_noop::value_type, custom_infallible>);
static_assert(
    source_io(combined{std::in_place, std::unexpect, fnfelt::Error{3}})
        .transform_error(increment_error)
        .build()
        .sync_wait()
        .value()
        .error()
        .value == 4);
static_assert(
    source_io(Result<int>{std::in_place, std::in_place, 3})
        .transform_error(invalid_mapper)
        .build()
        .sync_wait()
        .value()
        .value() == 3);
// Exercise the transport's constexpr driver with a synchronous leaf; coroutine leaves cannot
// execute during constant evaluation.
using constexpr_source =
    decltype(source_io(combined{std::in_place, std::unexpect, fnfelt::Error{3}}));
static_assert(
    io::TransformErrorProxy<constexpr_source, decltype(increment_error)>::run_inline(
        source_io(combined{std::in_place, std::unexpect, fnfelt::Error{3}}), increment_error)
        .value()
        .error()
        .value == 4);

struct OverloadedErrorMapper
{
    constexpr int operator()(std::pair<int, int> error) const
    {
        return error.first * 10 + error.second;
    }
    constexpr int operator()(int left, int right) const
    {
        return left + right;
    }
};

struct OwnedErrorMapper
{
    std::unique_ptr<int> offset;
    auto operator()(std::unique_ptr<int> error) &&
    {
        return std::make_unique<int>(*error + *offset);
    }
};
}  // namespace

TEST_CASE("[pure] IO transform_error maps only errors and preserves success and skip")
{
    int calls = 0;
    auto mapper = [&](int error)
    {
        ++calls;
        return std::to_string(error);
    };
    auto failed =
        source_io(combined{std::in_place, std::unexpect, fnfelt::Error{7}}).transform_error(mapper);
    static_assert(std::is_same_v<typename decltype(failed)::value_type, Result<int, std::string>>);
    CHECK_EQ(calls, 0);
    CHECK_EQ(failed.build().sync_wait().value().error().value, "7");
    CHECK_EQ(calls, 1);
    CHECK_EQ(
        source_io(combined{std::in_place, std::in_place, 5})
            .transform_error(mapper)
            .build()
            .sync_wait()
            .value()
            .value(),
        5);
    CHECK(
        source_io(combined{std::nullopt}).transform_error(mapper).build().sync_wait() ==
        std::nullopt);
    CHECK_EQ(calls, 1);
    using void_source = Result<void, int>;
    CHECK(source_io(void_source{std::in_place})
              .transform_error(mapper)
              .build()
              .sync_wait()
              .has_value());
    CHECK_EQ(
        source_io(void_source{std::in_place, std::unexpect, fnfelt::Error{8}})
            .transform_error(mapper)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        "8");
}

TEST_CASE("[pure] IO transform_error retains opaque returned error data")
{
    auto mapped =
        source_io(combined{std::in_place, std::unexpect, fnfelt::Error{4}})
            .transform_error(
                [](int error)
                {
                    return Result<int, int>{std::in_place, std::unexpect, fnfelt::Error{error + 1}};
                });
    using expected_type = Result<int, combined>;
    static_assert(std::is_same_v<typename decltype(mapped)::value_type, expected_type>);
    CHECK_EQ(mapped.build().sync_wait().value().error().value.value().error().value, 5);
    auto optional_error = source_io(combined{std::in_place, std::unexpect, fnfelt::Error{4}})
                              .transform_error([](int) { return std::optional<int>{}; });
    auto result = optional_error.build().sync_wait();
    CHECK((result && !result->has_value()));
    CHECK_FALSE(result.value().error().value.has_value());
}

TEST_CASE("[pure] IO transform_error supports spreading with direct invocation priority")
{
    using source_type = Result<int, std::pair<int, int>>;
    CHECK_EQ(
        source_io(source_type{std::in_place, std::unexpect, fnfelt::Error{std::pair{2, 3}}})
            .transform_error(OverloadedErrorMapper{})
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        23);
    CHECK_EQ(
        source_io(source_type{std::in_place, std::unexpect, fnfelt::Error{std::pair{2, 3}}})
            .transform_error([](int left, int right) { return left + right; })
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        5);
}

TEST_CASE("[pure] IO transform_error supports named custom traits and unreachable mappers")
{
    using fnfelt::literals::operator""_ss;
    auto source = source_io(Result<int>{std::in_place, std::in_place, 5});
    auto named = io::transform_error<"mapped"_ss>(source, invalid_mapper);
    CHECK_EQ(named.build().sync_wait().value().value(), 5);
    auto member_named = source.template transform_error<"member"_ss>(invalid_mapper);
    CHECK_EQ(member_named.build().sync_wait().value().value(), 5);
    auto custom = io::transform_error<io::detail::TransformErrorIOTraits<>>(
        source_io(combined{std::in_place, std::unexpect, fnfelt::Error{6}}), increment_error);
    CHECK_EQ(custom.build().sync_wait().value().error().value, 7);
    auto member_custom =
        source_io(combined{std::in_place, std::unexpect, fnfelt::Error{6}})
            .template transform_error<io::detail::TransformErrorIOTraits<>>(increment_error);
    CHECK_EQ(member_custom.build().sync_wait().value().error().value, 7);
    auto async_noop =
        async_source_io(Result<int, Infallible>{std::nullopt}).transform_error(invalid_mapper);
    CHECK(async_noop.build().sync_wait() == std::nullopt);

    auto mutable_source = source_io(combined{std::in_place, std::unexpect, fnfelt::Error{3}});
    auto const constant_source = mutable_source;
    CHECK_EQ(
        mutable_source.transform_error(increment_error).build().sync_wait().value().error().value,
        4);
    CHECK_EQ(
        constant_source.transform_error(increment_error).build().sync_wait().value().error().value,
        4);
    CHECK_EQ(
        std::move(constant_source)
            .transform_error(increment_error)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        4);
    CHECK_EQ(
        std::move(mutable_source)
            .transform_error(increment_error)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        4);
}

TEST_CASE("[pure] IO transform_error moves actions errors and rvalue-only mappers")
{
    using owned = Result<std::unique_ptr<int>, std::unique_ptr<int>>;
    auto mapped =
        source_io(owned{std::in_place, std::unexpect, fnfelt::Error{std::make_unique<int>(3)}})
            .transform_error(OwnedErrorMapper{std::make_unique<int>(2)});
    CHECK_EQ(*std::move(mapped).build().sync_wait().value().error().value, 5);
    auto successful = source_io(owned{std::in_place, std::in_place, std::make_unique<int>(7)})
                          .transform_error(OwnedErrorMapper{std::make_unique<int>(2)});
    auto success_result = std::move(successful).build().sync_wait();
    REQUIRE(success_result);
    REQUIRE(success_result->has_value());
    CHECK_EQ(***success_result, 7);
    auto async_mapped =
        async_source_io(
            owned{std::in_place, std::unexpect, fnfelt::Error{std::make_unique<int>(4)}})
            .transform_error(OwnedErrorMapper{std::make_unique<int>(2)});
    CHECK_EQ(*std::move(async_mapped).build().sync_wait().value().error().value, 6);
}

TEST_CASE("[pure] IO transform_error async paths preserve every source result")
{
    CHECK_EQ(
        async_source_io(combined{std::in_place, std::unexpect, fnfelt::Error{4}})
            .transform_error(increment_error)
            .build()
            .sync_wait()
            .value()
            .error()
            .value,
        5);
    CHECK_EQ(
        async_source_io(combined{std::in_place, std::in_place, 4})
            .transform_error(increment_error)
            .build()
            .sync_wait()
            .value()
            .value(),
        4);
    CHECK(
        async_source_io(combined{std::nullopt})
            .transform_error(increment_error)
            .build()
            .sync_wait() == std::nullopt);
}

TEST_CASE("[pure] IO transform_error validates error domains before constructing results")
{
    using traits = io::detail::TransformErrorIOTraits<>;
    using source_type = decltype(source_io(combined{std::in_place, std::in_place, 1}));
    using plain_source = decltype(source_io(1));
    using void_mapper = decltype([](int) {});
    using reference_mapper = decltype([](int) -> int & { static int value; return value; });
    using io_mapper = decltype([](int) { return source_io(1); });
    struct InfallibleMapper
    {
        Infallible operator()(int) const;
    };
    struct Incomplete;
    struct IncompleteMapper
    {
        Incomplete operator()(int) const;
    };
    struct Immovable
    {
        Immovable(Immovable &&) = delete;
    };
    struct ImmovableMapper
    {
        Immovable operator()(int) const;
    };
    using unexpected_mapper = decltype([](int) { return std::unexpected<int>{1}; });
    static_assert(!traits::validate_transform_error(^^int, ^^void_mapper).empty());
    static_assert(!traits::validate_transform_error(^^plain_source, ^^void_mapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^void_mapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^reference_mapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^io_mapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^InfallibleMapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^IncompleteMapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^ImmovableMapper).empty());
    static_assert(traits::validate_transform_error(^^source_type, ^^unexpected_mapper).empty());
    static_assert(!traits::validate_transform_error(^^source_type, ^^int).empty());
    CHECK(true);
}

TEST_CASE("[pure] IO composition diagnostics explain explicit channel alignment")
{
    using bind_traits = io::detail::AndThenIOTraits<>;
    using recovery_traits = io::detail::OrElseIOTraits<>;
    using source_type = decltype(source_io(combined{std::in_place, std::in_place, 1}));
    using conflicting =
        decltype([](int) { return source_io(Result<int, char>{std::in_place, std::in_place, 1}); });
    using wrong_value = decltype([](int) { return source_io('a'); });
    constexpr auto bind_reason = bind_traits::validate_and_then(^^source_type, ^^conflicting);
    constexpr auto value_reason = recovery_traits::validate_or_else(^^source_type, ^^wrong_value);
    constexpr std::string_view error_guidance =
        "composed error channels must have the same error type; use transform_error "
        "on conflicting computations before composition";
    CHECK_EQ(bind_reason, error_guidance);
    CHECK_EQ(
        value_reason,
        "recovery and source success must have the same payload type; use transform "
        "before or_else to align successful payloads");
    auto aligned =
        source_io(combined{std::in_place, std::unexpect, fnfelt::Error{3}})
            .transform_error([](int error) { return static_cast<char>(error); })
            .and_then(
                [](int value)
                { return source_io(Result<int, char>{std::in_place, std::in_place, value}); });
    CHECK_EQ(aligned.build().sync_wait().value().error().value, static_cast<char>(3));
}

#include <fnfelt/macros_pop.hpp>

#endif  // DOCTEST_CONFIG_DISABLE
