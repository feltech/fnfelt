// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/result.hpp>

#include <doctest/doctest.h>

#include <meta>

#include <any>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
using fnfelt::Error;
using fnfelt::Infallible;
using fnfelt::Result;
using result_type = Result<int, int>;

struct Incomplete;
struct AbstractValue
{
    virtual ~AbstractValue() = default;
    virtual void action() = 0;
};

/// Payload whose conversion cannot occur implicitly through an Error aggregate.
struct ExplicitPayload
{
    int value;

    /**
     * Own an explicitly converted value.
     *
     * @param input Value to own.
     */
    explicit constexpr ExplicitPayload(int input) : value(input) {}
};

/// Payload proving successful construction does not require an intermediate move.
struct ImmovablePayload
{
    int value;

    /**
     * Construct the final payload directly.
     *
     * @param input Value to own.
     */
    explicit constexpr ImmovablePayload(int input) : value(input) {}
    ImmovablePayload(ImmovablePayload const &) = delete;
    ImmovablePayload(ImmovablePayload &&) = delete;
};

/// Payload tracking copying and moving during optional extraction.
struct AccessPayload
{
    int value;
    int * copy_count;
    int * move_count;

    /**
     * Construct a payload connected to operation counters.
     *
     * @param input Initial payload value.
     * @param copies Copy operation counter.
     * @param moves Move operation counter.
     */
    constexpr AccessPayload(int input, int & copies, int & moves)
        : value(input), copy_count(&copies), move_count(&moves)
    {
    }

    /**
     * Copy a payload while recording the operation.
     *
     * @param other Payload to copy.
     */
    constexpr AccessPayload(AccessPayload const & other)
        : value(other.value), copy_count(other.copy_count), move_count(other.move_count)
    {
        ++*copy_count;
    }

    /**
     * Transfer a payload while recording its moved-from state.
     *
     * @param other Payload to move.
     */
    constexpr AccessPayload(AccessPayload && other) noexcept
        : value(std::exchange(other.value, -1)),
          copy_count(other.copy_count),
          move_count(other.move_count)
    {
        ++*move_count;
    }
};

/// Payload proving optional extraction propagates construction failures.
struct ThrowingAccessPayload
{
    ThrowingAccessPayload() = default;

    /**
     * Reject a payload transfer with a recognizable exception.
     *
     * @param other Payload to move, unused by the rejecting operation.
     */
    ThrowingAccessPayload(ThrowingAccessPayload && other)
    {
        static_cast<void>(other);
        throw std::runtime_error("payload move rejected");
    }
};

/// Distinguish direct tuple invocation from spreading its elements.
struct PayloadChoice
{
    /**
     * Accept the tuple as one opaque value.
     *
     * @param value Tuple payload.
     * @return First element.
     */
    constexpr int operator()(std::tuple<int, int> value) const
    {
        return std::get<0>(value);
    }

    /**
     * Accept the tuple's individual elements.
     *
     * @param left First element.
     * @param right Second element.
     * @return Sum with a distinct result type.
     */
    constexpr double operator()(int left, int right) const
    {
        return left + right;
    }
};

static_assert(std::is_same_v<result_type, std::optional<std::expected<int, Error<int>>>>);
static_assert(std::is_same_v<Result<void>, std::optional<std::expected<void, Error<Infallible>>>>);
static_assert(!std::is_default_constructible_v<Infallible>);
static_assert(!std::is_default_constructible_v<Error<Infallible>>);
static_assert(std::is_copy_constructible_v<Infallible>);
static_assert(std::is_move_constructible_v<Infallible>);
static_assert(!std::is_constructible_v<std::expected<int, Error<Infallible>>, std::unexpect_t>);
static_assert(Result<int>{std::in_place, std::in_place, 3}->value() == 3);
static_assert(result_type{std::in_place, std::unexpect, Error{4}}->error().value == 4);
static_assert(!Result<int>{});
static_assert(Result<void>{std::in_place}->has_value());
static_assert(!Result<void>{std::nullopt});
static_assert(Result<std::nullopt_t, int>{std::in_place, std::in_place, std::nullopt}->has_value());
static_assert(
    Result<int, std::unexpected<int>>{std::in_place, std::unexpect, std::unexpected{8}}
        ->error()
        .value.error() == 8);

static_assert(std::is_same_v<decltype(**std::declval<result_type &>()), int &>);
static_assert(std::is_same_v<decltype(**std::declval<result_type const &>()), int const &>);
static_assert(std::is_same_v<decltype(**std::declval<result_type &&>()), int &&>);
static_assert(std::is_same_v<decltype(**std::declval<result_type const &&>()), int const &&>);
static_assert(std::is_same_v<decltype((std::declval<result_type &>()->error().value)), int &>);
static_assert(
    std::is_same_v<decltype((std::declval<result_type const &>()->error().value)), int const &>);
}  // namespace

TEST_CASE("[pure] Result stores success failure and stop with standard discriminators")
{
    result_type success{std::in_place, std::in_place, 12};
    result_type failure{std::in_place, std::unexpect, Error{13}};
    result_type stopped{std::nullopt};
    REQUIRE(success);
    CHECK(success->has_value());
    CHECK_EQ(**success, 12);
    REQUIRE(failure);
    CHECK_FALSE(failure->has_value());
    CHECK_EQ(failure->error().value, 13);
    CHECK_FALSE(stopped);
    CHECK_THROWS_AS(stopped.value(), std::bad_optional_access);
    CHECK_THROWS_AS(failure->value(), std::bad_expected_access<Error<int>>);
}

TEST_CASE("[pure] Result factories construct all states in constant expressions")
{
    constexpr auto success = fnfelt::result::success<result_type>(3);
    constexpr auto error = fnfelt::result::error<result_type>(4);
    constexpr auto stopped = fnfelt::result::stopped<result_type>();
    static_assert(success && success->has_value() && **success == 3);
    static_assert(error && !error->has_value() && error->error().value == 4);
    static_assert(!stopped);
    static_assert(std::is_same_v<decltype(fnfelt::result::success<result_type>(3)), result_type>);
    static_assert(std::is_same_v<decltype(fnfelt::result::error<result_type>(4)), result_type>);
    static_assert(std::is_same_v<decltype(fnfelt::result::stopped<result_type>()), result_type>);

    constexpr auto void_success = fnfelt::result::success<Result<void, int>>();
    constexpr auto void_error = fnfelt::result::error<Result<void, int>>(5);
    static_assert(void_success && void_success->has_value());
    static_assert(void_error && void_error->error().value == 5);
    static_assert(!fnfelt::result::stopped<Result<void>>());
    CHECK(true);
}

TEST_CASE("[pure] Result factories keep optional expected and Result payloads opaque")
{
    constexpr auto nullopt_payload =
        fnfelt::result::success<Result<std::nullopt_t, int>>(std::nullopt);
    constexpr auto empty_optional =
        fnfelt::result::success<Result<std::optional<int>, int>>(std::nullopt);
    constexpr auto failed_expected =
        fnfelt::result::success<Result<std::expected<int, int>, int>>(std::unexpect, 7);
    constexpr auto stopped_payload = fnfelt::result::success<Result<Result<int>, int>>();
    static_assert(nullopt_payload && nullopt_payload->has_value());
    static_assert(empty_optional && empty_optional->has_value() && !**empty_optional);
    static_assert(failed_expected && failed_expected->has_value());
    static_assert(!(**failed_expected).has_value() && (**failed_expected).error() == 7);
    static_assert(stopped_payload && stopped_payload->has_value() && !**stopped_payload);

    auto broad_payload = fnfelt::result::success<Result<std::any, int>>(8);
    REQUIRE(broad_payload);
    REQUIRE(broad_payload->has_value());
    CHECK_EQ(std::any_cast<int>(**broad_payload), 8);
}

TEST_CASE("[pure] Result factories construct and convert the destination payload types")
{
    constexpr auto explicit_success = fnfelt::result::success<Result<ExplicitPayload, int>>(9);
    constexpr auto explicit_error = fnfelt::result::error<Result<int, ExplicitPayload>>(10);
    constexpr auto converted_error =
        fnfelt::result::error<Result<int, std::int64_t>>(std::int16_t{11});
    constexpr auto tuple_success = fnfelt::result::success<Result<std::tuple<int, int>>>(12, 13);
    static_assert((**explicit_success).value == 9);
    static_assert(explicit_error->error().value.value == 10);
    static_assert(converted_error->error().value == 11);
    static_assert(std::get<0>(**tuple_success) == 12 && std::get<1>(**tuple_success) == 13);

    // An Error supplied as opaque data must not be unwrapped before converting to std::any.
    auto broad_error = fnfelt::result::error<Result<int, std::any>>(Error<std::any>{std::any{14}});
    REQUIRE(broad_error);
    REQUIRE_FALSE(broad_error->has_value());
    auto wrapped_payload = std::any_cast<Error<std::any>>(&broad_error->error().value);
    REQUIRE(wrapped_payload);
    CHECK_EQ(std::any_cast<int>(wrapped_payload->value), 14);
}

TEST_CASE("[pure] Result factories own lvalues and transfer move-only values")
{
    int value = 15;
    auto copied_value = fnfelt::result::success<result_type>(value);
    auto copied_error = fnfelt::result::error<result_type>(value);
    value = 16;
    CHECK_EQ(**copied_value, 15);
    CHECK_EQ(copied_error->error().value, 15);

    using owned = Result<std::unique_ptr<int>, std::unique_ptr<int>>;
    auto payload = std::make_unique<int>(17);
    auto error_payload = std::make_unique<int>(18);
    auto success = fnfelt::result::success<owned>(std::move(payload));
    auto error = fnfelt::result::error<owned>(std::move(error_payload));
    CHECK_FALSE(payload);
    CHECK_FALSE(error_payload);
    CHECK_EQ(***success, 17);
    CHECK_EQ(*error->error().value, 18);

    auto immovable = fnfelt::result::success<Result<ImmovablePayload, int>>(19);
    CHECK_EQ((**immovable).value, 19);

    // Construct const ownership directly; the completed Error cannot itself be moved.
    auto constant_error =
        fnfelt::result::error<Result<int, std::unique_ptr<int> const>>(std::make_unique<int>(20));
    CHECK_EQ(*constant_error->error().value, 20);
}

TEST_CASE("[pure] Untyped Result tokens use the declared destination")
{
    constexpr auto make_error = []() -> result_type { return fnfelt::result::error(20); };
    constexpr auto make_stopped = []() -> result_type { return fnfelt::result::stopped(); };
    static_assert(make_error()->error().value == 20);
    static_assert(!make_stopped());
    static_assert(std::is_same_v<decltype(fnfelt::result::stopped()), std::nullopt_t>);
    static_assert(std::is_same_v<decltype(fnfelt::result::error(20)), std::unexpected<Error<int>>>);

    int payload = 21;
    auto copied_error = fnfelt::result::error(payload);
    payload = 22;
    CHECK_EQ(copied_error.error().value, 21);

    auto owned_payload = std::make_unique<int>(23);
    auto owned_error = fnfelt::result::error(std::move(owned_payload));
    CHECK_FALSE(owned_payload);
    CHECK_EQ(*owned_error.error().value, 23);
}

TEST_CASE("[pure] Result factory destinations must be unqualified owned Results")
{
    using fnfelt::detail::ResultTraits;
    using ordinary_optional = std::optional<int>;
    static_assert(ResultTraits::validate_construction(^^result_type).empty());
    static_assert(ResultTraits::validate_construction(^^Result<void>).empty());
    static_assert(!ResultTraits::validate_construction(^^int).empty());
    static_assert(!ResultTraits::validate_construction(^^void).empty());
    static_assert(!ResultTraits::validate_construction(^^ordinary_optional).empty());
    static_assert(!ResultTraits::validate_construction(^^result_type &).empty());
    static_assert(!ResultTraits::validate_construction(^^result_type &&).empty());
    static_assert(!ResultTraits::validate_construction(^^result_type const).empty());
    static_assert(!ResultTraits::validate_construction(^^result_type volatile).empty());
    CHECK(true);
}

TEST_CASE("[pure] Result state queries distinguish success error and stop in constant expressions")
{
    constexpr auto success = fnfelt::result::success<result_type>(3);
    constexpr auto error = fnfelt::result::error<result_type>(4);
    constexpr auto stopped = fnfelt::result::stopped<result_type>();
    static_assert(fnfelt::result::is_success(success));
    static_assert(!fnfelt::result::is_error(success) && !fnfelt::result::is_stopped(success));
    static_assert(fnfelt::result::is_error(error));
    static_assert(!fnfelt::result::is_success(error) && !fnfelt::result::is_stopped(error));
    static_assert(fnfelt::result::is_stopped(stopped));
    static_assert(!fnfelt::result::is_success(stopped) && !fnfelt::result::is_error(stopped));
    static_assert(fnfelt::result::get_success(success) == 3);
    static_assert(fnfelt::result::get_error(error) == 4);
    static_assert(noexcept(fnfelt::result::is_stopped(stopped)));
    static_assert(noexcept(fnfelt::result::is_error(error)));
    static_assert(noexcept(fnfelt::result::is_success(success)));
    static_assert(noexcept(fnfelt::result::get_error(error)));
    static_assert(noexcept(fnfelt::result::get_success(success)));

    constexpr auto void_success = fnfelt::result::success<Result<void, int>>();
    constexpr auto void_error = fnfelt::result::error<Result<void, int>>(5);
    constexpr auto void_stopped = fnfelt::result::stopped<Result<void, int>>();
    static_assert(fnfelt::result::is_success(void_success));
    static_assert(fnfelt::result::is_error(void_error));
    static_assert(fnfelt::result::is_stopped(void_stopped));
    static_assert(std::is_void_v<decltype(fnfelt::result::get_success(void_success))>);
    fnfelt::result::get_success(void_success);

    constexpr auto infallible = fnfelt::result::success<Result<int>>(6);
    static_assert(fnfelt::result::is_success(infallible));
    static_assert(!fnfelt::result::is_error(infallible));
    static_assert(fnfelt::result::is_stopped(fnfelt::result::stopped<Result<int>>()));
    CHECK(true);
}

TEST_CASE(
    "[pure] Result inspection treats nested optionals expected values and tags as opaque data")
{
    auto empty_payload = fnfelt::result::success<Result<std::optional<int>, int>>(std::nullopt);
    CHECK(fnfelt::result::is_success(empty_payload));
    CHECK_FALSE(fnfelt::result::get_success(empty_payload));

    auto error_payload =
        fnfelt::result::success<Result<std::expected<int, int>, int>>(std::unexpect, 7);
    CHECK(fnfelt::result::is_success(error_payload));
    CHECK_EQ(fnfelt::result::get_success(error_payload).error(), 7);

    auto nullopt_payload = fnfelt::result::success<Result<std::nullopt_t, int>>(std::nullopt);
    CHECK(fnfelt::result::is_success(nullopt_payload));
    static_assert(
        std::is_same_v<decltype(fnfelt::result::get_success(nullopt_payload)), std::nullopt_t &>);

    auto empty_error = fnfelt::result::error<Result<int, std::optional<int>>>(std::nullopt);
    CHECK(fnfelt::result::is_error(empty_error));
    CHECK_FALSE(fnfelt::result::get_error(empty_error));

    auto tag_error = fnfelt::result::error<Result<int, std::in_place_t>>(std::in_place);
    CHECK(fnfelt::result::is_error(tag_error));
    static_assert(
        std::is_same_v<decltype(fnfelt::result::get_error(tag_error)), std::in_place_t &>);

    auto stopped_payload = fnfelt::result::success<Result<Result<int>, int>>();
    CHECK(fnfelt::result::is_success(stopped_payload));
    CHECK(fnfelt::result::is_stopped(fnfelt::result::get_success(stopped_payload)));
}

TEST_CASE("[pure] Result unsafe access preserves cvref and borrows or moves only on request")
{
    static_assert(
        std::
            is_same_v<decltype(fnfelt::result::get_success(std::declval<result_type &>())), int &>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_success(std::declval<result_type const &>())),
                  int const &>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_success(std::declval<result_type &&>())),
                  int &&>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_success(std::declval<result_type const &&>())),
                  int const &&>);
    static_assert(
        std::is_same_v<decltype(fnfelt::result::get_error(std::declval<result_type &>())), int &>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_error(std::declval<result_type const &>())),
                  int const &>);
    static_assert(
        std::
            is_same_v<decltype(fnfelt::result::get_error(std::declval<result_type &&>())), int &&>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_error(std::declval<result_type const &&>())),
                  int const &&>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_error(std::declval<Result<int> &>())),
                  Infallible &>);
    static_assert(std::is_same_v<
                  decltype(fnfelt::result::get_error(std::declval<Result<int> const &&>())),
                  Infallible const &&>);

    auto immovable = fnfelt::result::success<Result<ImmovablePayload, int>>(8);
    auto & borrowed = fnfelt::result::get_success(immovable);
    borrowed.value = 9;
    CHECK_EQ((**immovable).value, 9);
    CHECK_EQ(std::addressof(borrowed), std::addressof(**immovable));

    using owned = Result<std::unique_ptr<int>, std::unique_ptr<int>>;
    auto success = fnfelt::result::success<owned>(std::make_unique<int>(10));
    auto error = fnfelt::result::error<owned>(std::make_unique<int>(11));
    auto && value_reference = fnfelt::result::get_success(std::move(success));
    auto && error_reference = fnfelt::result::get_error(std::move(error));
    CHECK_EQ(std::addressof(value_reference), std::addressof(**success));
    CHECK_EQ(std::addressof(error_reference), std::addressof(error->error().value));
    REQUIRE(value_reference);
    REQUIRE(error_reference);
    auto taken_value = std::move(value_reference);
    auto taken_error = std::move(error_reference);
    CHECK_EQ(*taken_value, 10);
    CHECK_EQ(*taken_error, 11);
    CHECK_FALSE(fnfelt::result::get_success(success));
    CHECK_FALSE(fnfelt::result::get_error(error));
}

TEST_CASE("[pure] Result error access removes only the outer Error wrapper")
{
    auto error = fnfelt::result::error<Result<int, Error<int>>>(Error{12});
    auto & payload = fnfelt::result::get_error(error);
    static_assert(std::is_same_v<decltype(payload), Error<int> &>);
    CHECK_EQ(std::addressof(payload), std::addressof(error->error().value));
    payload.value = 13;
    CHECK_EQ(error->error().value.value, 13);
}

TEST_CASE("[pure] Result inspection accepts qualified expressions and rejects volatile sources")
{
    using fnfelt::detail::ResultTraits;
    using qualified_payloads = Result<int volatile, int const>;
    using ordinary_optional = std::optional<int>;
    static_assert(ResultTraits::validate_access(^^result_type).empty());
    static_assert(ResultTraits::validate_access(^^result_type &).empty());
    static_assert(ResultTraits::validate_access(^^result_type const &).empty());
    static_assert(ResultTraits::validate_access(^^result_type &&).empty());
    static_assert(ResultTraits::validate_access(^^result_type const &&).empty());
    static_assert(ResultTraits::validate_access(^^qualified_payloads const &).empty());
    static_assert(!ResultTraits::validate_access(^^result_type volatile).empty());
    static_assert(!ResultTraits::validate_access(^^result_type volatile &).empty());
    static_assert(!ResultTraits::validate_access(^^result_type const volatile &&).empty());
    static_assert(!ResultTraits::validate_access(^^int).empty());
    static_assert(!ResultTraits::validate_access(^^void).empty());
    static_assert(!ResultTraits::validate_access(^^ordinary_optional).empty());
    CHECK(true);
}

TEST_CASE("[pure] Result type queries recognize only the marked outer representation")
{
    using ordinary_nested = std::optional<std::expected<int, int>>;
    using direct_result = std::optional<std::expected<int, Error<int>>>;
    static_assert(fnfelt::result::is_result<result_type>());
    static_assert(fnfelt::result::is_result<result_type const &>());
    static_assert(fnfelt::result::is_result<result_type const volatile &&>());
    static_assert(fnfelt::result::is_result<direct_result>());
    static_assert(fnfelt::result::is_result<Result<void>>());
    static_assert(fnfelt::result::is_result<Result<Result<int>, std::optional<int>>>());
    static_assert(!fnfelt::result::is_result<int>());
    static_assert(!fnfelt::result::is_result<void>());
    static_assert(!fnfelt::result::is_result<Incomplete>());
    static_assert(!fnfelt::result::is_result<std::optional<int>>());
    static_assert(!fnfelt::result::is_result<ordinary_nested>());
    static_assert(!fnfelt::result::is_result<std::expected<int, Error<int>>>());
    static_assert(!fnfelt::result::is_result<Error<int>>());
    static_assert(noexcept(fnfelt::result::is_result<result_type>()));
    CHECK(true);
}

TEST_CASE("[pure] Result value queries inspect runtime references without copying or moving")
{
    auto query = []<class T>(T && value)
    {
        static_assert(fnfelt::result::is_result(value) == fnfelt::result::is_result<T>());
        static_assert(
            fnfelt::result::is_result(std::forward<T>(value)) == fnfelt::result::is_result<T>());
        return fnfelt::result::is_result(value);
    };
    auto runtime_result = fnfelt::result::success<Result<ImmovablePayload>>(7);
    result_type volatile qualified_result;
    int ordinary = 9;

    static_assert(fnfelt::result::is_result(runtime_result));
    static_assert(fnfelt::result::is_result(qualified_result));
    static_assert(!fnfelt::result::is_result(ordinary));
    static_assert(noexcept(fnfelt::result::is_result(runtime_result)));

    CHECK(query(runtime_result));
    CHECK(query(std::as_const(runtime_result)));
    CHECK(query(std::move(runtime_result)));
    CHECK(query(std::move(std::as_const(runtime_result))));
    CHECK(query(qualified_result));
    CHECK_FALSE(query(ordinary));
    CHECK_EQ(fnfelt::result::get_success(runtime_result).value, 7);
}

TEST_CASE(
    "[pure] Result optional access constructs owned matching payloads in constant expressions")
{
    constexpr auto owned_value =
        fnfelt::result::maybe_get_success(fnfelt::result::success<result_type>(3));
    constexpr auto owned_error =
        fnfelt::result::maybe_get_error(fnfelt::result::error<result_type>(4));
    static_assert(std::is_same_v<std::remove_cv_t<decltype(owned_value)>, std::optional<int>>);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(owned_error)>, std::optional<int>>);
    static_assert(owned_value && *owned_value == 3);
    static_assert(owned_error && *owned_error == 4);
    static_assert(!fnfelt::result::maybe_get_error(fnfelt::result::success<result_type>(5)));
    static_assert(!fnfelt::result::maybe_get_success(fnfelt::result::error<result_type>(6)));
    static_assert(!fnfelt::result::maybe_get_success(fnfelt::result::stopped<result_type>()));
    static_assert(!fnfelt::result::maybe_get_error(fnfelt::result::stopped<result_type>()));

    constexpr auto owned_void =
        fnfelt::result::maybe_get_success(fnfelt::result::success<Result<void, int>>());
    static_assert(
        std::is_same_v<std::remove_cv_t<decltype(owned_void)>, std::optional<std::monostate>>);
    static_assert(owned_void.has_value());
    static_assert(!fnfelt::result::maybe_get_success(fnfelt::result::error<Result<void, int>>(7)));
    static_assert(!fnfelt::result::maybe_get_success(fnfelt::result::stopped<Result<void>>()));
    static_assert(!fnfelt::result::maybe_get_error(fnfelt::result::success<Result<void>>()));
    static_assert(
        std::is_same_v<
            decltype(fnfelt::result::maybe_get_error(fnfelt::result::success<Result<int>>(8))),
            std::optional<Infallible>>);
    CHECK(true);
}

TEST_CASE("[pure] Result optional access moves only the selected payload and retains source state")
{
    using observed = Result<AccessPayload, AccessPayload>;
    int copies = 0;
    int moves = 0;
    auto success = fnfelt::result::success<observed>(9, copies, moves);

    auto absent_error = fnfelt::result::maybe_get_error(std::move(success));
    CHECK_FALSE(absent_error);
    CHECK_EQ(copies, 0);
    CHECK_EQ(moves, 0);
    CHECK_EQ(fnfelt::result::get_success(success).value, 9);

    auto value = fnfelt::result::maybe_get_success(std::move(success));
    REQUIRE(value);
    CHECK_EQ(value->value, 9);
    CHECK_EQ(copies, 0);
    CHECK_EQ(moves, 1);
    CHECK(fnfelt::result::is_success(success));
    CHECK_EQ(fnfelt::result::get_success(success).value, -1);

    auto error = fnfelt::result::error<observed>(AccessPayload{10, copies, moves});
    copies = 0;
    moves = 0;
    auto absent_value = fnfelt::result::maybe_get_success(std::move(error));
    CHECK_FALSE(absent_value);
    CHECK_EQ(copies, 0);
    CHECK_EQ(moves, 0);
    CHECK_EQ(fnfelt::result::get_error(error).value, 10);

    auto error_payload = fnfelt::result::maybe_get_error(std::move(error));
    REQUIRE(error_payload);
    CHECK_EQ(error_payload->value, 10);
    CHECK_EQ(copies, 0);
    CHECK_EQ(moves, 1);
    CHECK(fnfelt::result::is_error(error));
    CHECK_EQ(fnfelt::result::get_error(error).value, -1);

    auto stopped = fnfelt::result::stopped<observed>();
    copies = 0;
    moves = 0;
    CHECK_FALSE(fnfelt::result::maybe_get_success(std::move(stopped)));
    CHECK_FALSE(fnfelt::result::maybe_get_error(std::move(stopped)));
    CHECK_EQ(copies, 0);
    CHECK_EQ(moves, 0);
    CHECK(fnfelt::result::is_stopped(stopped));
}

TEST_CASE("[pure] Result optional access owns move-only payloads without moving the whole Result")
{
    using owned = Result<std::unique_ptr<int>, std::unique_ptr<int>>;
    auto success = fnfelt::result::success<owned>(std::make_unique<int>(11));
    auto error = fnfelt::result::error<owned>(std::make_unique<int>(12));
    auto value = fnfelt::result::maybe_get_success(std::move(success));
    auto error_payload = fnfelt::result::maybe_get_error(std::move(error));
    REQUIRE(value);
    REQUIRE(error_payload);
    CHECK_EQ(**value, 11);
    CHECK_EQ(**error_payload, 12);
    CHECK(fnfelt::result::is_success(success));
    CHECK(fnfelt::result::is_error(error));
    CHECK_FALSE(fnfelt::result::get_success(success));
    CHECK_FALSE(fnfelt::result::get_error(error));

    using fixed_success = Result<ImmovablePayload, int>;
    using fixed_error = Result<int, ImmovablePayload>;
    static_assert(!std::is_move_constructible_v<fixed_success>);
    static_assert(!std::is_move_constructible_v<fixed_error>);
    auto inactive_success = fnfelt::result::error<fixed_success>(13);
    auto inactive_error = fnfelt::result::success<fixed_error>(14);
    auto movable_error = fnfelt::result::maybe_get_error(std::move(inactive_success));
    auto movable_success = fnfelt::result::maybe_get_success(std::move(inactive_error));
    REQUIRE(movable_error);
    REQUIRE(movable_success);
    CHECK_EQ(*movable_error, 13);
    CHECK_EQ(*movable_success, 14);
}

TEST_CASE(
    "[pure] Result optional access copies const rvalues and preserves declared payload constness")
{
    using observed = Result<AccessPayload, AccessPayload>;
    int copies = 0;
    int moves = 0;
    auto const success = fnfelt::result::success<observed>(15, copies, moves);
    auto copied = fnfelt::result::maybe_get_success(std::move(success));
    REQUIRE(copied);
    CHECK_EQ(copied->value, 15);
    CHECK_EQ(copies, 1);
    CHECK_EQ(moves, 0);
    CHECK_EQ(fnfelt::result::get_success(success).value, 15);

    auto const error = fnfelt::result::error<observed>(AccessPayload{16, copies, moves});
    copies = 0;
    moves = 0;
    auto copied_error = fnfelt::result::maybe_get_error(std::move(error));
    REQUIRE(copied_error);
    CHECK_EQ(copied_error->value, 16);
    CHECK_EQ(copies, 1);
    CHECK_EQ(moves, 0);
    CHECK_EQ(fnfelt::result::get_error(error).value, 16);

    auto constant_payload =
        fnfelt::result::success<Result<AccessPayload const, int>>(17, copies, moves);
    copies = 0;
    moves = 0;
    auto owned_constant = fnfelt::result::maybe_get_success(std::move(constant_payload));
    static_assert(std::is_same_v<decltype(owned_constant), std::optional<AccessPayload const>>);
    REQUIRE(owned_constant);
    CHECK_EQ(owned_constant->value, 17);
    CHECK_EQ(copies, 1);
    CHECK_EQ(moves, 0);
}

TEST_CASE("[pure] Result optional access keeps nested payloads and error wrappers opaque")
{
    auto empty_payload = fnfelt::result::success<Result<std::optional<int>, int>>(std::nullopt);
    auto empty_value = fnfelt::result::maybe_get_success(std::move(empty_payload));
    REQUIRE(empty_value);
    CHECK_FALSE(*empty_value);

    auto failed_payload =
        fnfelt::result::success<Result<std::expected<int, int>, int>>(std::unexpect, 18);
    auto failed_value = fnfelt::result::maybe_get_success(std::move(failed_payload));
    REQUIRE(failed_value);
    REQUIRE_FALSE(failed_value->has_value());
    CHECK_EQ(failed_value->error(), 18);

    auto nested_payload = fnfelt::result::success<Result<Result<int>, int>>();
    auto nested_value = fnfelt::result::maybe_get_success(std::move(nested_payload));
    REQUIRE(nested_value);
    CHECK(fnfelt::result::is_stopped(*nested_value));

    auto wrapped_error = fnfelt::result::error<Result<int, Error<int>>>(Error{19});
    auto wrapped_payload = fnfelt::result::maybe_get_error(std::move(wrapped_error));
    REQUIRE(wrapped_payload);
    CHECK_EQ(wrapped_payload->value, 19);

    auto empty_error = fnfelt::result::error<Result<int, std::optional<int>>>(std::nullopt);
    auto empty_error_payload = fnfelt::result::maybe_get_error(std::move(empty_error));
    REQUIRE(empty_error_payload);
    CHECK_FALSE(*empty_error_payload);
}

TEST_CASE("[pure] Result optional access propagates throwing moves")
{
    auto success = fnfelt::result::success<Result<ThrowingAccessPayload, int>>();
    Result<int, ThrowingAccessPayload> error{std::in_place, std::unexpect};
    CHECK_THROWS_AS(fnfelt::result::maybe_get_success(std::move(success)), std::runtime_error);
    CHECK_THROWS_AS(fnfelt::result::maybe_get_error(std::move(error)), std::runtime_error);
    CHECK(fnfelt::result::is_success(success));
    CHECK(fnfelt::result::is_error(error));
}

TEST_CASE("[pure] Result optional access validates rvalues and representable selected payloads")
{
    using fnfelt::detail::ResultTraits;
    using tag_success = Result<std::nullopt_t, int>;
    using tag_error = Result<int, std::in_place_t>;
    using fixed_success = Result<ImmovablePayload, int>;
    using fixed_error = Result<int, ImmovablePayload>;
    using const_owned = Result<std::unique_ptr<int> const, std::unique_ptr<int> const>;
    static_assert(ResultTraits::validate_maybe_access(^^result_type &&, false).empty());
    static_assert(ResultTraits::validate_maybe_access(^^result_type const &&, true).empty());
    static_assert(ResultTraits::validate_maybe_access(^^Result<void> &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^result_type &, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^result_type const &, true).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^result_type volatile &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^int &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^tag_success &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^tag_error &&, true).empty());
    static_assert(ResultTraits::validate_maybe_access(^^tag_success &&, true).empty());
    static_assert(ResultTraits::validate_maybe_access(^^tag_error &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^fixed_success &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^fixed_error &&, true).empty());
    static_assert(ResultTraits::validate_maybe_access(^^fixed_success &&, true).empty());
    static_assert(ResultTraits::validate_maybe_access(^^fixed_error &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^const_owned &&, false).empty());
    static_assert(!ResultTraits::validate_maybe_access(^^const_owned &&, true).empty());
    CHECK(true);
}

TEST_CASE("[pure] Result payloads and errors remain opaque standard data")
{
    Result<std::optional<int>, int> missing{std::in_place, std::in_place, std::nullopt};
    REQUIRE(missing);
    REQUIRE(missing->has_value());
    CHECK_FALSE(**missing);
    Result<std::expected<int, int>, int> payload{std::in_place, std::in_place, std::unexpect, 9};
    REQUIRE(payload);
    REQUIRE(payload->has_value());
    CHECK_FALSE((**payload).has_value());
    CHECK_EQ((**payload).error(), 9);
    Result<int, std::optional<int>> error{
        std::in_place, std::unexpect, Error{std::optional<int>{}}};
    REQUIRE(error);
    REQUIRE_FALSE(error->has_value());
    CHECK_FALSE(error->error().value);
    using inner_error = std::expected<int, int>;
    Result<int, inner_error> nested_error{
        std::in_place, std::unexpect, Error{inner_error{std::unexpect, 7}}};
    REQUIRE(nested_error);
    REQUIRE_FALSE(nested_error->has_value());
    CHECK_EQ(nested_error->error().value.error(), 7);
    Result<int, int const> constant_error{std::in_place, std::unexpect, Error<int const>{8}};
    REQUIRE(constant_error);
    REQUIRE_FALSE(constant_error->has_value());
    CHECK_EQ(constant_error->error().value, 8);
    Result<Result<int>> nested{std::in_place, std::in_place, std::nullopt};
    REQUIRE(nested);
    REQUIRE(nested->has_value());
    CHECK_FALSE(**nested);
}

TEST_CASE("[pure] Result transports move-only payloads and owning errors")
{
    using owned = Result<std::unique_ptr<int>, std::unique_ptr<int>>;
    static_assert(!std::is_copy_constructible_v<owned>);
    static_assert(std::is_move_constructible_v<owned>);
    owned success{std::in_place, std::in_place, std::make_unique<int>(17)};
    owned error{std::in_place, std::unexpect, Error{std::make_unique<int>(18)}};
    auto payload = **std::move(success);
    auto failure = std::move(error).value().error().value;
    CHECK_EQ(*payload, 17);
    CHECK_EQ(*failure, 18);
}

TEST_CASE("[pure] Result void uses native expected success failure and stop")
{
    Result<void, int> success{std::in_place};
    Result<void, int> failure{std::in_place, std::unexpect, Error{4}};
    Result<void, int> stopped;
    REQUIRE(success);
    CHECK(success->has_value());
    CHECK_NOTHROW(success->value());
    REQUIRE(failure);
    CHECK_FALSE(failure->has_value());
    CHECK_EQ(failure->error().value, 4);
    CHECK_FALSE(stopped);
}

TEST_CASE("[pure] Result validates actual expected payload and owning error requirements")
{
    using fnfelt::detail::ErrorTraits;
    using fnfelt::detail::ResultTraits;
    using array_type = int[2];
    using unexpected_alias = std::unexpected<int>;
    using nullopt_alias = std::nullopt_t;
    static_assert(ResultTraits::validate(^^int, ^^fnfelt::Infallible).empty());
    static_assert(ResultTraits::validate(^^void, ^^int).empty());
    static_assert(ResultTraits::validate(^^std::optional<int>, ^^std::optional<int>).empty());
    static_assert(ResultTraits::validate(^^std::nullopt_t, ^^int).empty());
    static_assert(ResultTraits::validate(^^nullopt_alias, ^^int).empty());
    static_assert(ResultTraits::validate(^^int, ^^std::unexpected<int>).empty());
    static_assert(ResultTraits::validate(^^int, ^^std::in_place_t).empty());
    static_assert(ResultTraits::validate(^^int, ^^std::unexpect_t).empty());
    static_assert(!ResultTraits::validate(^^Incomplete, ^^int).empty());
    static_assert(!ResultTraits::validate(^^int &, ^^int).empty());
    static_assert(!ResultTraits::validate(^^array_type, ^^int).empty());
    static_assert(!ResultTraits::validate(^^AbstractValue, ^^int).empty());
    static_assert(!ResultTraits::validate(^^std::in_place_t, ^^int).empty());
    static_assert(!ResultTraits::validate(^^std::unexpect_t, ^^int).empty());
    static_assert(!ResultTraits::validate(^^std::unexpected<int>, ^^int).empty());
    static_assert(!ResultTraits::validate(^^unexpected_alias, ^^int).empty());
    static_assert(!ResultTraits::validate(^^int, ^^void).empty());
    static_assert(!ResultTraits::validate(^^int, ^^Incomplete).empty());
    static_assert(ResultTraits::validate(^^int, ^^int const).empty());
    static_assert(ResultTraits::validate(^^int, ^^int volatile).empty());
    static_assert(!ResultTraits::validate(^^int, ^^int &).empty());
    static_assert(!ResultTraits::validate(^^int, ^^array_type).empty());
    static_assert(ErrorTraits::validate(^^fnfelt::Infallible).empty());
    static_assert(ErrorTraits::validate(^^std::unexpected<int>).empty());
    static_assert(!ErrorTraits::validate(^^void).empty());
    static_assert(!ErrorTraits::validate(^^Incomplete).empty());
    static_assert(!ErrorTraits::validate(^^AbstractValue).empty());
    static_assert(!ErrorTraits::validate(^^int &).empty());
    static_assert(!ErrorTraits::validate(^^array_type).empty());
    static_assert(ErrorTraits::validate(^^int const).empty());
    static_assert(ErrorTraits::validate(^^int volatile).empty());
    CHECK(true);
}

TEST_CASE("[pure] Result recognition inspects only its marked outer representation")
{
    using fnfelt::detail::ResultTraits;
    using ordinary_nested = std::optional<std::expected<int, int>>;
    using direct_result = std::optional<std::expected<int, Error<int>>>;
    using const_expected = std::optional<std::expected<int, Error<int>> const>;
    using const_error = std::optional<std::expected<int, Error<int> const>>;
    static_assert(!ResultTraits::is_result_type(^^ordinary_nested));
    static_assert(!ResultTraits::is_result_type(^^std::expected<int, Error<int>>));
    static_assert(ResultTraits::is_result_type(^^direct_result));
    static_assert(!ResultTraits::is_result_type(^^const_expected));
    static_assert(!ResultTraits::is_result_type(^^const_error));
    static_assert(ResultTraits::is_result_type(^^Result<Result<int>, int>));
    CHECK(true);
}

TEST_CASE("[pure] ResultTypeInfo describes plain values and opaque result payloads")
{
    using fnfelt::detail::ResultTypeInfo;
    constexpr auto plain = ResultTypeInfo::from(^^int);
    static_assert(!plain.is_result && !plain.has_error);
    static_assert(is_same_type(plain.success_payload_meta, ^^int));
    static_assert(is_same_type(plain.type_meta(), ^^int));
    static_assert(plain.validate().empty());
    constexpr auto plain_void = ResultTypeInfo::from(^^void);
    static_assert(!plain_void.is_result);
    static_assert(is_void_type(plain_void.type_meta()));
    static_assert(plain_void.validate().empty());
    constexpr auto void_result = ResultTypeInfo::from(^^Result<void>);
    static_assert(void_result.is_result && !void_result.has_error);
    static_assert(void_result.validate().empty());
    static_assert(is_same_type(void_result.type_meta(), ^^Result<void>));
    using nested_result = Result<Result<int>, int>;
    constexpr auto nested = ResultTypeInfo::from(^^nested_result);
    static_assert(nested.is_result && nested.has_error);
    static_assert(is_same_type(nested.success_payload_meta, ^^Result<int>));
    static_assert(is_same_type(nested.error_payload_meta, ^^int));
    static_assert(is_same_type(nested.type_meta(), ^^nested_result));
    constexpr auto opaque = ResultTypeInfo::from(^^nested_result, false);
    static_assert(!opaque.is_result && !opaque.has_error);
    static_assert(is_same_type(opaque.success_payload_meta, ^^nested_result));
    CHECK(true);
}

TEST_CASE("[pure] Result binding and invocation share direct spread and void selection")
{
    using fnfelt::detail::ErrorContinuationInfo;
    using fnfelt::detail::SuccessContinuationInfo;
    using tuple_result = Result<std::tuple<int, int>, std::tuple<int, int>>;
    constexpr auto value_binding = SuccessContinuationInfo::from(^^PayloadChoice, ^^tuple_result);
    constexpr auto error_binding = ErrorContinuationInfo::from(^^PayloadChoice, ^^tuple_result);
    static_assert(value_binding.is_result && value_binding.is_valid);
    static_assert(error_binding.is_reachable && error_binding.is_valid);
    static_assert(is_same_type(value_binding.argument_meta, ^^std::tuple<int, int>));
    static_assert(is_same_type(error_binding.argument_meta, ^^std::tuple<int, int>));
    static_assert(is_same_type(value_binding.result_meta, ^^int));
    static_assert(is_same_type(error_binding.result_meta, ^^int));
    tuple_result value{std::in_place, std::in_place, 3, 4};
    tuple_result error{std::in_place, std::unexpect, Error{std::tuple{5, 6}}};
    CHECK_EQ(fnfelt::detail::invoke_success_continuation<true>(PayloadChoice{}, value), 3);
    CHECK_EQ(fnfelt::detail::invoke_error_continuation(PayloadChoice{}, error), 5);

    constexpr auto spread = [](int left, int right) { return left + right; };
    static_assert(SuccessContinuationInfo::from(^^decltype(spread), ^^tuple_result).is_valid);
    static_assert(ErrorContinuationInfo::from(^^decltype(spread), ^^tuple_result).is_valid);
    CHECK_EQ(fnfelt::detail::invoke_success_continuation<true>(spread, value), 7);
    CHECK_EQ(fnfelt::detail::invoke_error_continuation(spread, error), 11);

    constexpr auto nullary = [] { return 9; };
    constexpr auto void_binding =
        SuccessContinuationInfo::from(^^decltype(nullary), ^^Result<void>);
    static_assert(void_binding.is_result && void_binding.is_valid);
    static_assert(is_void_type(void_binding.argument_meta));
    CHECK_EQ(
        fnfelt::detail::invoke_success_continuation<true>(
            nullary, fnfelt::detail::make_void_success<Result<void>>()),
        9);
}

TEST_CASE("[pure] Result success mapping preserves plain promotion and nested payloads")
{
    using fnfelt::detail::map_success_result;
    constexpr auto increment = [](int value) { return value + 1; };
    static_assert(map_success_result<int>(increment, 3) == 4);
    constexpr auto promote = [](int value)
    { return fnfelt::detail::make_success_result<Result<int>>(value + 2); };
    constexpr auto promoted = map_success_result<Result<int>>(promote, 3);
    static_assert(**promoted == 5);
    constexpr auto nested = map_success_result<Result<Result<int>, int>>(
        promote, fnfelt::detail::make_success_result<Result<int, int>>(4));
    static_assert(****nested == 6);
    constexpr auto nullary = [] { return 7; };
    constexpr auto from_void =
        map_success_result<Result<int>>(nullary, fnfelt::detail::make_void_success<Result<void>>());
    static_assert(**from_void == 7);
    constexpr auto discard = [](int) {};
    constexpr auto void_success = map_success_result<Result<void, int>>(
        discard, fnfelt::detail::make_success_result<Result<int, int>>(8));
    static_assert(void_success && void_success->has_value());
    int calls = 0;
    map_success_result<void>([&calls](int) { ++calls; }, 9);
    CHECK_EQ(calls, 1);
}

TEST_CASE("[pure] Result success mapping preserves terminal states without invoking the mapper")
{
    using fnfelt::detail::map_success_result;
    using owned_error = Result<int, std::unique_ptr<int>>;
    auto throwing_mapper = [](int) -> double { throw 42; };
    CHECK_FALSE(
        map_success_result<Result<double, std::unique_ptr<int>>>(throwing_mapper, owned_error{}));
    auto failed = map_success_result<Result<double, std::unique_ptr<int>>>(
        throwing_mapper, fnfelt::detail::make_error_result<owned_error>(std::make_unique<int>(12)));
    REQUIRE(failed);
    REQUIRE_FALSE(failed->has_value());
    CHECK_EQ(*failed->error().value, 12);
    CHECK_THROWS_AS(
        (map_success_result<Result<double, std::unique_ptr<int>>>(
            throwing_mapper, fnfelt::detail::make_success_result<owned_error>(13))),
        int);
}

TEST_CASE("[pure] Result success mapping forwards payload and mapper value categories")
{
    using fnfelt::detail::map_success_result;
    struct Consume
    {
        /**
         * Take ownership through an rvalue-only mapper.
         *
         * @param value Owning payload.
         * @return Transferred payload.
         */
        std::unique_ptr<int> operator()(std::unique_ptr<int> value) &&
        {
            return value;
        }
    };
    auto owned = map_success_result<Result<std::unique_ptr<int>>>(
        Consume{},
        fnfelt::detail::make_success_result<Result<std::unique_ptr<int>>>(
            std::make_unique<int>(14)));
    CHECK_EQ(***owned, 14);
    auto source = fnfelt::detail::make_success_result<Result<int>>(15);
    auto mutated = map_success_result<Result<int>>([](int & value) { return ++value; }, source);
    CHECK_EQ(**source, 16);
    CHECK_EQ(**mutated, 16);
    auto const & constant_source = source;
    auto copied = map_success_result<Result<int>>(
        [](int const & value) { return value + 1; }, constant_source);
    CHECK_EQ(**copied, 17);
}

TEST_CASE("[pure] Result error binding never queries unreachable handlers")
{
    constexpr auto invalid_handler = [](auto value) { return value.nonexistent_member(); };
    constexpr auto infallible_binding =
        fnfelt::detail::ErrorContinuationInfo::from(^^decltype(invalid_handler), ^^Result<int>);
    constexpr auto plain_binding =
        fnfelt::detail::ErrorContinuationInfo::from(^^decltype(invalid_handler), ^^int);
    static_assert(!infallible_binding.is_reachable && !infallible_binding.is_valid);
    static_assert(!plain_binding.is_reachable && !plain_binding.is_valid);
    constexpr auto input = fnfelt::detail::make_success_result<Result<int>>(7);
    constexpr auto mapped = fnfelt::detail::map_error_result<Result<int>>(invalid_handler, input);
    static_assert(**mapped == 7);
    constexpr auto rejected_binding =
        fnfelt::detail::ErrorContinuationInfo::from(^^decltype([] {}), ^^Result<int, int>);
    static_assert(rejected_binding.is_reachable && !rejected_binding.is_valid);
    static_assert(is_void_type(rejected_binding.result_meta));
    CHECK_EQ(**mapped, 7);
}

TEST_CASE("[pure] Result helpers preserve terminal states void and owning payloads")
{
    using owned = Result<std::unique_ptr<int>, std::unique_ptr<int>>;
    auto promoted = fnfelt::detail::convert_result<owned>(std::make_unique<int>(12));
    CHECK_EQ(***promoted, 12);
    auto widened = fnfelt::detail::convert_result<owned>(
        fnfelt::detail::make_success_result<Result<std::unique_ptr<int>>>(
            std::make_unique<int>(13)));
    CHECK_EQ(***widened, 13);
    auto failed = fnfelt::detail::maybe_stop_or_error<true, owned>(
        fnfelt::detail::make_error_result<owned>(std::make_unique<int>(14)));
    REQUIRE(failed);
    CHECK_EQ(*failed->value().error().value, 14);
    auto stopped = fnfelt::detail::maybe_stop_or_error<true, owned>(owned{});
    REQUIRE(stopped);
    CHECK_FALSE(*stopped);
    CHECK_FALSE((fnfelt::detail::maybe_stop_or_error<true, owned>(std::move(promoted))));

    auto recovered = fnfelt::detail::maybe_stop_or_success<false, owned>(
        fnfelt::detail::make_error_result<owned>(std::make_unique<int>(15)));
    REQUIRE(recovered);
    CHECK_EQ(*recovered->value().error().value, 15);
    CHECK_FALSE((fnfelt::detail::maybe_stop_or_success<true, owned>(
        fnfelt::detail::make_error_result<owned>(std::make_unique<int>(16)))));
    auto void_success = fnfelt::detail::convert_result<Result<void, int>>(
        fnfelt::detail::make_void_success<Result<void>>());
    REQUIRE(void_success);
    CHECK(void_success->has_value());
    auto void_passthrough =
        fnfelt::detail::maybe_stop_or_success<true, Result<void, int>>(std::move(void_success));
    REQUIRE(void_passthrough);
    CHECK(void_passthrough->value().has_value());
}

TEST_CASE("[pure] Result error mapping keeps returned Result data opaque")
{
    using nested_error = Result<int>;
    constexpr auto mapper = [](int error)
    { return fnfelt::detail::make_success_result<nested_error>(error + 1); };
    constexpr auto binding =
        fnfelt::detail::ErrorContinuationInfo::from(^^decltype(mapper), ^^Result<int, int>);
    static_assert(binding.is_reachable && binding.is_valid);
    static_assert(is_same_type(binding.result_meta, ^^nested_error));
    auto failed = fnfelt::detail::map_error_result<Result<int, nested_error>>(
        mapper, fnfelt::detail::make_error_result<Result<int, int>>(4));
    REQUIRE(failed);
    REQUIRE_FALSE(failed->has_value());
    CHECK_EQ(**failed->error().value, 5);
    auto stopped =
        fnfelt::detail::map_error_result<Result<int, nested_error>>(mapper, Result<int, int>{});
    CHECK_FALSE(stopped);
    auto success = fnfelt::detail::map_error_result<Result<int, nested_error>>(
        mapper, fnfelt::detail::make_success_result<Result<int, int>>(6));
    CHECK_EQ(**success, 6);
}

#endif  // DOCTEST_CONFIG_DISABLE
