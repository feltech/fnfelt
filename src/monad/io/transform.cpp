// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Move-only transformer that reads through a unique_ptr as a const lvalue - passes validation.
struct MoveOnlyTransformer
{
    std::unique_ptr<int> ptr{std::make_unique<int>(2)};

    int operator()(int value) const
    {
        return value + *ptr;
    }
};

/// Move-only action incrementing a move counter on every move - pins that rvalue chains move.
struct MoveTrackingAction
{
    /// Number of move constructions observed since the last reset.
    static inline int moves = 0;
    std::unique_ptr<int> ptr{std::make_unique<int>(1)};

    MoveTrackingAction() = default;
    MoveTrackingAction(MoveTrackingAction && other) noexcept : ptr{std::move(other.ptr)}
    {
        ++moves;
    }
    MoveTrackingAction & operator=(MoveTrackingAction &&) = delete;
    int operator()() const
    {
        return *ptr;
    }
};

/// Overloaded transformer: the direct (pair-taking) overload must win over the spread one.
struct OverloadedTransformer
{
    int operator()(std::pair<int, int> pair_value) const
    {
        return pair_value.first * 10 + pair_value.second;
    }

    int operator()(int lhs, int rhs) const
    {
        return lhs * 100 + rhs;
    }
};

/// Incomplete type - has no meaningful invocation result.
struct Incomplete;

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::detail::TransformIOTraits<custom_io_name>;

/// Custom traits that accept valid actions and delegate transform validation to the default.
struct DelegatingTransformTraits : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "DelegatingTransformIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_transform(
        std::meta::info io_meta, std::meta::info transformer_meta)
    {
        return fnfelt::monad::io::detail::TransformIOTraits<>::validate_transform(
            io_meta, transformer_meta);
    }
};

/// Constant-evaluated IO pipeline transforming via the member overload.
consteval int constexpr_pipeline_transform()
{
    constexpr auto transformed =
        fnfelt::monad::io::create([] { return 1; }).transform([](int x) { return x + 1; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the named member overload.
consteval int constexpr_pipeline_transform_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto transformed =
        fnfelt::monad::io::create([] { return 1; })
            .template transform<"Named transform"_ss>([](int x) { return x + 1; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the explicit-traits member overload.
consteval int constexpr_pipeline_transform_traits()
{
    constexpr auto transformed =
        fnfelt::monad::io::create([] { return 1; })
            .template transform<CustomIOTraits>([](int x) { return x + 1; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the free transform function.
consteval int constexpr_pipeline_transform_free()
{
    constexpr auto transformed = fnfelt::monad::io::transform(
        fnfelt::monad::io::create([] { return 1; }), [](int x) { return x + 1; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the free named transform function.
consteval int constexpr_pipeline_transform_free_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto transformed = fnfelt::monad::io::transform<"Named free transform"_ss>(
        fnfelt::monad::io::create([] { return 1; }), [](int x) { return x + 1; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the free explicit-traits transform function.
consteval int constexpr_pipeline_transform_free_traits()
{
    constexpr auto transformed = fnfelt::monad::io::transform<CustomIOTraits>(
        fnfelt::monad::io::create([] { return 1; }), [](int x) { return x + 1; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline spreading a pair value into the transformer.
consteval int constexpr_pipeline_spread()
{
    constexpr auto transformed = fnfelt::monad::io::create([] { return std::pair{1, 2}; })
                                     .transform([](int x, int y) { return x + y; });
    return transformed.build().sync_wait();
}

/// Constant-evaluated IO pipeline composing transform with a subsequent and_then.
consteval int constexpr_pipeline_transform_and_then()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .transform([](int x) { return x + 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    return bound.build().sync_wait();
}

/// Constant-evaluated IO pipeline composing and_then with a subsequent transform.
consteval int constexpr_pipeline_and_then_transform()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); })
            .transform([](int x) { return x * 10; });
    return bound.build().sync_wait();
}

/// Constant-evaluated IO pipeline composing transform with a subsequent ap.
consteval int constexpr_pipeline_transform_ap()
{
    constexpr auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x * 10; }; }),
        fnfelt::monad::io::create([] { return 2; }).transform([](int x) { return x + 1; }));
    return applied.build().sync_wait();
}

/// Constant-evaluated IO pipeline composing ap with a subsequent transform.
consteval int constexpr_pipeline_ap_transform()
{
    constexpr auto applied =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create([] { return [](int x) { return x * 10; }; }),
            fnfelt::monad::io::create([] { return 2; }))
            .transform([](int x) { return x + 1; });
    return applied.build().sync_wait();
}

/// Stateless async function doubling its argument.
inline constexpr auto async_double = [](auto, int value) -> lf::task<int> { co_return value * 2; };

/// Stateless async function producing a pair, for spread dispatch.
inline constexpr auto async_pair = [](auto, int value) -> lf::task<std::pair<int, int>>
{ co_return std::pair{value, value + 1}; };

/// Stateless async function consuming and producing a move-only value.
inline constexpr auto async_unique_from =
    [](auto, std::unique_ptr<int> ptr) -> lf::task<std::unique_ptr<int>>
{ co_return std::make_unique<int>(*ptr * 3); };

/// Stateless async function yielding its argument unchanged.
inline constexpr auto async_identity = [](auto, int value) -> lf::task<int> { co_return value; };

/// Stateless functor incrementing its argument, produced by an async function.
struct Increment
{
    constexpr int operator()(int value) const
    {
        return value + 1;
    }
};

/// Stateless async function producing an incrementing callable.
inline constexpr auto async_fn_increment = [](auto, int) -> lf::task<Increment>
{ co_return Increment{}; };

/// Movable stateful functor scaling then adding, produced by an async function.
struct ScaleAndAdd
{
    int scale = 0;

    constexpr int operator()(int value) const
    {
        return scale * 100 + value;
    }
};

/// Stateless async function producing a scaling callable.
inline constexpr auto async_fn_scale = [](auto, int scale) -> lf::task<ScaleAndAdd>
{ co_return ScaleAndAdd{scale}; };
}  // namespace

TEST_CASE("IO transform applies a direct transformer to the source's value")
{
    using fnfelt::monad::io::IO;

    auto const transformed =
        fnfelt::monad::io::create([] { return 1; }).transform([](int x) { return x + 1; });
    CHECK_EQ(transformed.build().sync_wait(), 2);
}

TEST_CASE("IO transform spreads pair and tuple values into the transformer")
{
    using fnfelt::monad::io::IO;

    auto const transformed_pair = fnfelt::monad::io::create([] { return std::pair{1, 2}; })
                                      .transform([](int x, int y) { return x + y; });
    CHECK_EQ(transformed_pair.build().sync_wait(), 3);

    auto const transformed_tuple = fnfelt::monad::io::create([] { return std::tuple{3, 4}; })
                                       .transform([](int x, int y) { return x * y; });
    CHECK_EQ(transformed_tuple.build().sync_wait(), 12);
}

TEST_CASE("IO transform prefers direct invocation over spreading")
{
    using fnfelt::monad::io::IO;

    // The transformer accepts the whole pair, so it must be invoked directly even though the value
    // could also be spread.
    auto const transformed = fnfelt::monad::io::create([] { return std::pair{1, 2}; })
                                 .transform([](std::pair<int, int> pair_value)
                                            { return pair_value.first * 10 + pair_value.second; });
    CHECK_EQ(transformed.build().sync_wait(), 12);

    // An overloaded functor pins precedence at runtime: the pair-taking overload must win.
    auto const overloaded = fnfelt::monad::io::create([] { return std::pair{1, 2}; })
                                .transform(OverloadedTransformer{});
    CHECK_EQ(overloaded.build().sync_wait(), 12);
}

TEST_CASE("IO transform invokes the transformer as an rvalue")
{
    using fnfelt::monad::io::IO;

    // Ref-qualified transformer: the `&&` overload is the one validation, inference and the driver
    // must all select, so the composition's value is long (2) rather than int (1).
    struct RefQualifiedTransformer
    {
        int operator()([[maybe_unused]] int value) const &
        {
            return 1;
        }

        // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
        long operator()([[maybe_unused]] int value) &&
        {
            return 2;
        }
    };

    auto transformed =
        fnfelt::monad::io::create([] { return 1; }).transform(RefQualifiedTransformer{});
    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    static_assert(std::is_same_v<decltype(transformed)::value_type, long>);
    CHECK_EQ(transformed.build().sync_wait(), 2L);
}

TEST_CASE("IO transform rejects a transformer callable only as a const lvalue")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::TransformIOTraits;

    // The stored transformer is invoked as an rvalue, so a deleted `&&` overload is not accepted.
    struct ConstLvalueOnlyTransformer
    {
        int operator()(int value) const &
        {
            return value;
        }

        int operator()(int value) && = delete;
    };

    static_assert(
        fnfelt::monad::io::detail::TransformIOTraits<>::validate_transform(
            ^^IO<int (*)()>, ^^ConstLvalueOnlyTransformer) ==
        "transformer function does not accept the source IO's value");
}

TEST_CASE("IO transform chains through successive transformers")
{
    using fnfelt::monad::io::IO;

    auto const transformed = fnfelt::monad::io::create([] { return 1; })
                                 .transform([](int x) { return x + 1; })
                                 .transform([](int x) { return x * 10; });
    CHECK_EQ(transformed.build().sync_wait(), 20);
}

TEST_CASE("IO transform accepts an lvalue transformer and copies it")
{
    using fnfelt::monad::io::IO;

    // A named copyable transformer must be accepted (copied by value), not deduced as a reference
    // type and rejected.
    struct IdentityTransformer
    {
        int offset;

        int operator()(int value) const
        {
            return value + offset;
        }
    };

    IdentityTransformer transformer_lvalue{10};
    auto const transformed =
        fnfelt::monad::io::create([] { return 1; }).transform(transformer_lvalue);
    CHECK_EQ(transformed.build().sync_wait(), 11);

    // The lvalue is untouched, so it remains usable.
    CHECK_EQ(transformer_lvalue(2), 12);
}

TEST_CASE("IO transform works on a const IO and with a move-only transformer")
{
    using fnfelt::monad::io::IO;

    auto const source = fnfelt::monad::io::create([] { return 5; });
    auto transformed = source.transform(MoveOnlyTransformer{});
    CHECK_EQ(std::move(transformed).build().sync_wait(), 7);
}

TEST_CASE("IO with custom traits transforms and runs end-to-end")
{
    using fnfelt::monad::io::IO;

    auto const source = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    auto const transformed =
        source.template transform<DelegatingTransformTraits>([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, DelegatingTransformTraits>);
    CHECK_EQ(transformed.build().sync_wait(), 2);
}

TEST_CASE("transform member defaults to TransformIOTraits for the result")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;

    // Source IO with custom traits: the result takes the default TransformIOTraits, not the
    // source's.
    auto const source = fnfelt::monad::io::create([] { return 1; });
    auto const transformed = source.transform([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, TransformIOTraits>);
    CHECK_EQ(transformed.build().sync_wait(), 2);
}

TEST_CASE("transform member with explicit traits uses those traits for the result")
{
    using fnfelt::monad::io::IO;

    auto const source = fnfelt::monad::io::create([] { return 1; });
    auto const transformed = source.template transform<CustomIOTraits>([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, CustomIOTraits>);
    CHECK_EQ(transformed.build().sync_wait(), 2);
}

TEST_CASE("transform member with a name names the result")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    auto const source = fnfelt::monad::io::create([] { return 1; });
    auto const plus_one = [](int x) { return x + 1; };
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    auto const transformed = source.template transform<"Named transform"_ss>(plus_one);
    static_assert(decltype(transformed)::traits::name == "Named transform");
    CHECK_EQ(transformed.build().sync_wait(), 2);
}

TEST_CASE("free transform uses the default, named and explicit-traits forms")
{
    using fnfelt::monad::io::IO;
    using TransformIOTraits = fnfelt::monad::io::detail::TransformIOTraits<>;
    using namespace fnfelt::literals;  // NOLINT

    auto const source = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    auto const transformed = fnfelt::monad::io::transform(source, [](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, TransformIOTraits>);
    CHECK_EQ(transformed.build().sync_wait(), 2);

    auto const named = fnfelt::monad::io::transform<"Named free transform"_ss>(
        source, [](int x) { return x + 1; });
    static_assert(decltype(named)::traits::name == "Named free transform");
    CHECK_EQ(named.build().sync_wait(), 2);

    auto const trait_bound =
        fnfelt::monad::io::transform<CustomIOTraits>(source, [](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(trait_bound)::traits, CustomIOTraits>);
    CHECK_EQ(trait_bound.build().sync_wait(), 2);
}

TEST_CASE("transform composes with and_then and ap in both directions")
{
    using fnfelt::monad::io::ap;
    using fnfelt::monad::io::create;

    // transform then and_then: (1 + 1) * 10.
    auto const transform_then_and_then =
        create([] { return 1; })
            .transform([](int x) { return x + 1; })
            .and_then([](int x) { return create([x] { return x * 10; }); });
    CHECK_EQ(transform_then_and_then.build().sync_wait(), 20);

    // and_then then transform: (1 + 1) * 10.
    auto const and_then_then_transform =
        create([] { return 1; })
            .and_then([](int x) { return create([x] { return x + 1; }); })
            .transform([](int x) { return x * 10; });
    CHECK_EQ(and_then_then_transform.build().sync_wait(), 20);

    // transform then ap: (2 + 1) * 10.
    auto const transform_then_ap =
        ap(create([] { return [](int x) { return x * 10; }; }),
           create([] { return 2; }).transform([](int x) { return x + 1; }));
    CHECK_EQ(transform_then_ap.build().sync_wait(), 30);

    // ap then transform: (2 * 10) + 1.
    auto const ap_then_transform =
        ap(create([] { return [](int x) { return x * 10; }; }), create([] { return 2; }))
            .transform([](int x) { return x + 1; });
    CHECK_EQ(ap_then_transform.build().sync_wait(), 21);
}

TEST_CASE("transform produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_transform() == 2);
    static_assert(constexpr_pipeline_transform_named() == 2);
    static_assert(constexpr_pipeline_transform_traits() == 2);
    static_assert(constexpr_pipeline_transform_free() == 2);
    static_assert(constexpr_pipeline_transform_free_named() == 2);
    static_assert(constexpr_pipeline_transform_free_traits() == 2);
    static_assert(constexpr_pipeline_spread() == 3);
    static_assert(constexpr_pipeline_transform_and_then() == 20);
    static_assert(constexpr_pipeline_and_then_transform() == 20);
    static_assert(constexpr_pipeline_transform_ap() == 30);
    static_assert(constexpr_pipeline_ap_transform() == 21);
}

TEST_CASE("transform moves a move-only rvalue source through a chain without copying")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::TransformIOTraits;

    // A move-only source cannot be copied, so it must be moved into the TransformAction member. The
    // action is move-only, hence so is the resulting IO.
    MoveTrackingAction::moves = 0;
    auto bound =
        IO<MoveTrackingAction, fnfelt::monad::io::detail::TransformIOTraits<>>{MoveTrackingAction{}}
            .transform([](int x) { return x + 1; });
    CHECK_EQ(std::move(bound).build().sync_wait(), 2);
    static_assert(!std::is_copy_constructible_v<decltype(bound)>);
    // Five moves. Construction: the temporary action into the source IO's action member; the source
    // IO into the TransformAction's source member; and the TransformAction into the returned IO's
    // action member (three). Running: the IO's action into the IORunProxy by value; and the nested
    // source IO into its own IORunProxy when the TransformAction runs (two). The rvalue temporary
    // is elided into the IO constructor's by-value parameter, so it costs no extra move.
    CHECK_EQ(MoveTrackingAction::moves, 5);

    // Each further hop moves the whole nested source IO once into the new TransformAction member
    // and the new TransformAction once into the returned IO's action member, so the two-hop chain
    // costs two more moves than the one-hop chain to construct. Running adds one more nested
    // source-IO move for the extra hop, so eight in total.
    MoveTrackingAction::moves = 0;
    auto chained =
        IO<MoveTrackingAction, fnfelt::monad::io::detail::TransformIOTraits<>>{MoveTrackingAction{}}
            .transform([](int x) { return x + 1; })
            .transform([](int x) { return x * 10; });
    CHECK_EQ(std::move(chained).build().sync_wait(), 20);
    static_assert(!std::is_copy_constructible_v<decltype(chained)>);
    CHECK_EQ(MoveTrackingAction::moves, 8);

    // The free function is a forwarding reference too, so a directly-passed prvalue rvalue source
    // costs the same as the member form (the temporary is direct-initialised into the stored
    // action). Pinned here so a regression to a by-value source parameter would show up for the
    // move-only case.
    MoveTrackingAction::moves = 0;
    auto free_bound = fnfelt::monad::io::transform(
        IO<MoveTrackingAction, fnfelt::monad::io::detail::TransformIOTraits<>>{
            MoveTrackingAction{}},
        [](int x) { return x + 1; });
    CHECK_EQ(std::move(free_bound).build().sync_wait(), 2);
    static_assert(!std::is_copy_constructible_v<decltype(free_bound)>);
    CHECK_EQ(MoveTrackingAction::moves, 5);
}

TEST_CASE("transform copies an lvalue IO source, leaving the caller's IO usable")
{
    using fnfelt::monad::io::IO;

    // A copyable action so the lvalue source can be copied; the caller's IO must be untouched.
    auto const source = fnfelt::monad::io::create([] { return 1; });
    auto const transformed = fnfelt::monad::io::transform(source, [](int x) { return x + 1; });
    CHECK_EQ(transformed.build().sync_wait(), 2);
    CHECK_EQ(source.build().sync_wait(), 1);
}

namespace transform_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using fnfelt::monad::io::IO;
using fnfelt::monad::io::detail::TransformIOTraits;

using SourceIO = IO<int (*)(), fnfelt::monad::io::detail::TransformIOTraits<"SourceIO"_ss>>;

/// Transformer returning a plain int; its display string is stable.
struct IntTransformer
{
    int operator()(int) const;
};

/// Transformer that is not callable with the source value, so no result is determinable.
struct UncallableTransformer
{
};

// The source is an async leaf, so the diagnostic must splice the proxy's unwrapped value type
// without blowing up; the transformer is a valid callable but returns an IO.
inline constexpr auto async_diag_source = fnfelt::monad::io::create_async<"AsyncSource"_ss>(
    [](auto, int x) -> lf::task<int> { co_return x * 2; }, 21);

/// Transformer returning a named IO; its display string is stable.
struct IoReturningTransformer
{
    IO<int (*)(), fnfelt::monad::io::detail::TransformIOTraits<"ResultIO"_ss>> operator()(
        int) const;
};
}  // namespace transform_error_msg_test

TEST_CASE("detail::transform_error_msg composes a readable diagnostic")
{
    using fnfelt::monad::io::detail::transform_error_msg;

    // Valid source IO and transformer returning a plain int: the source IO name is shown, then the
    // result name, then the reason, and the offending type is the transformer.
    static_assert(
        transform_error_msg<
            ^^transform_error_msg_test::SourceIO,
            ^^transform_error_msg_test::IntTransformer>(
            "TransformIO", "rejected by custom transformer traits") ==
        std::string{
            "fnfelt: IO transform error: TransformIO{(SourceIO()) => int}: rejected by custom "
            "transformer traits: "} +
            std::string{display_string_of(^^transform_error_msg_test::IntTransformer)});

    // Non-IO source: neither IO name is determinable, and the offending type is the source.
    static_assert(
        transform_error_msg<^^int, ^^transform_error_msg_test::IntTransformer>(
            "TransformIO", "custom reason") ==
        "fnfelt: IO transform error: TransformIO{(<unknown>()) => <unknown>}: custom reason: int");

    // Source IO whose transformer result cannot be determined: the result name is "<unknown>".
    static_assert(
        transform_error_msg<
            ^^transform_error_msg_test::SourceIO,
            ^^transform_error_msg_test::UncallableTransformer>("TransformIO", "custom reason") ==
        std::string{
            "fnfelt: IO transform error: TransformIO{(SourceIO()) => <unknown>}: custom reason: "} +
            std::string{display_string_of(^^transform_error_msg_test::UncallableTransformer)});
    // Async source: the diagnostic splices the proxy's unwrapped value type ("AsyncSource") and
    // the offending type is the transformer.
    static_assert(
        transform_error_msg<
            ^^decltype(transform_error_msg_test::async_diag_source),
            ^^transform_error_msg_test::IoReturningTransformer>(
            "IO", "transformer function should not return an IO") ==
        std::string{"fnfelt: IO transform error: IO{(AsyncSource()) => ResultIO}: transformer "
                    "function should "
                    "not return an IO: "} +
            std::string{display_string_of(^^transform_error_msg_test::IoReturningTransformer)});
}

TEST_CASE("detail::maybe_transformer_result_name reflects a transform application's result")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::maybe_transformer_result_name;

    using value_io_type = IO<int (*)()>;
    // Plain-value result: shown by display string.
    using transformer_type = decltype([](int x) { return x + 1; });
    static_assert(maybe_transformer_result_name<^^value_io_type, ^^transformer_type>() == "int");
    // Void result is still determinable.
    using void_transformer_type = decltype([](int) {});
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^void_transformer_type>() == "void");
    // IO result: shown by the result IO's name rather than a display string.
    using io_transformer_type = decltype([](int) { return IO<int (*)()>{[] { return 1; }}; });
    static_assert(maybe_transformer_result_name<^^value_io_type, ^^io_transformer_type>() == "IO");
    // Lambda result: shown by kind as "lambda" rather than a structural display string.
    using lambda_transformer_type = decltype([](int) { return [] { return 1; }; });
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^lambda_transformer_type>() == "lambda");
    // Spread: two-argument transformer with a pair value resolves through the spread branch.
    using spread_transformer_type = decltype([](int, int) { return 1; });
    using pair_value_io_type = IO<std::pair<int, int> (*)()>;
    static_assert(
        maybe_transformer_result_name<^^pair_value_io_type, ^^spread_transformer_type>() == "int");
    // An incompatible argument still leaves a concrete signature's return type available.
    using string_transformer_type = decltype([](std::string) { return 1; });
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^string_transformer_type>() == "int");
    using size_transformer_type = decltype([](std::string text) { return text.size(); });
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^size_transformer_type>() ==
        fnfelt::detail::short_type_name(dealias(^^std::string::size_type)));
    using generic_transformer_type = decltype([](auto * value) { return *value; });
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^generic_transformer_type>() ==
        "<unknown>");
    struct OverloadedTransformer
    {
        int operator()(std::string) const;
        double operator()(std::string, int) const;
    };
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^OverloadedTransformer>() == "<unknown>");
    using pointer_transformer_type = int (*)(std::string) noexcept;
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^pointer_transformer_type>() == "int");
    using ordinary_pointer_transformer_type = int (*)(std::string);
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^ordinary_pointer_transformer_type>() ==
        "int");
    auto capturing_transformer = [count = std::string::size_type{3}](std::string) { return count; };
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^decltype(capturing_transformer)>() ==
        fnfelt::detail::short_type_name(dealias(^^std::string::size_type)));
    struct PrivateTransformer
    {
    private:
        int operator()(std::string) const;
    };
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^PrivateTransformer>() == "<unknown>");
    struct UndeducedTransformer
    {
        auto operator()(std::string) const;
    };
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^UndeducedTransformer>() == "<unknown>");
    struct DeletedTransformer
    {
        int operator()(std::string) const = delete;
    };
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^DeletedTransformer>() == "<unknown>");
    // Incomplete transformer: no result determinable.
    static_assert(maybe_transformer_result_name<^^value_io_type, ^^Incomplete>() == "<unknown>");
    // Non-IO source: no result determinable.
    static_assert(maybe_transformer_result_name<^^int, ^^transformer_type>() == "<unknown>");
}

TEST_CASE("transform runs directly over an async source")
{
    // The transformer is applied to the async source's unwrapped value: 10 -> async(*2)=20 ->
    // +1=21.
    auto transformed =
        fnfelt::monad::io::create_async(async_double, 10).transform([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::value_type, int>);
    CHECK_EQ(transformed.build().sync_wait(), 21);
}

TEST_CASE("transform runs over an async source nested in an async chain")
{
    // and_then over an async source whose continuation returns a transform over another async
    // source. The nested transform must join the surrounding async tree; running it inline spins up
    // its own scheduler from a worker thread. 10 -> async(*2)=20 -> transform(async(*2) + 1)=41.
    auto nested = fnfelt::monad::io::create_async(async_double, 10)
                      .and_then(
                          [](int x)
                          {
                              return fnfelt::monad::io::create_async(async_double, x)
                                  .transform([](int y) { return y + 1; });
                          });
    CHECK_EQ(nested.build().sync_wait(lf::lazy_pool{2}), 41);
}

TEST_CASE("transform spreads a pair produced by an async source")
{
    // The pair produced by the async leaf is spread through the dealiased async value type.
    auto spread = fnfelt::monad::io::create_async(async_pair, 3)
                      .transform([](int lhs, int rhs) { return lhs * 10 + rhs; });
    static_assert(std::is_same_v<decltype(spread)::value_type, int>);
    CHECK_EQ(spread.build().sync_wait(), 34);
}

TEST_CASE("transform invokes an async transformer as an rvalue")
{
    // Ref-qualified transformer: the `&&` overload is the one validation, inference and the driver
    // must all select, so the composition's value is long (2) rather than int (1).
    struct RefQualifiedTransformer
    {
        int operator()([[maybe_unused]] int value) const &
        {
            return 1;
        }

        // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
        long operator()([[maybe_unused]] int value) &&
        {
            return 2;
        }
    };

    auto transformed =
        fnfelt::monad::io::create_async(async_double, 10).transform(RefQualifiedTransformer{});
    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    static_assert(std::is_same_v<decltype(transformed)::value_type, long>);
    CHECK_EQ(transformed.build().sync_wait(lf::lazy_pool{2}), 2L);
}

TEST_CASE("transform carries an async move-only value into a transformer")
{
    auto transformed = fnfelt::monad::io::create_async(async_unique_from, std::make_unique<int>(4))
                           .transform([](std::unique_ptr<int> ptr) { return *ptr + 1; });
    static_assert(std::is_same_v<decltype(transformed)::value_type, int>);
    // The leaf stores a move-only argument, so the composed action and its IO are move-only.
    static_assert(!std::is_copy_constructible_v<decltype(transformed)>);
    CHECK_EQ(std::move(transformed).build().sync_wait(), 13);
}

TEST_CASE("transform composes with and_then and ap across alternating sync and async levels")
{
    // Outer async ap: Increment(async 1) = 2. Nested transform over an async source: *2 + 1 = 5.
    // Middle sync and_then: *10 = 50. Inner async ap: ScaleAndAdd{50}(async 7) = 5007.
    auto pipeline =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create_async(async_fn_increment, 0),
            fnfelt::monad::io::create_async(async_identity, 1))
            .and_then(
                [](int x)
                {
                    return fnfelt::monad::io::create_async(async_double, x)
                        .transform([](int y) { return y + 1; });
                })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); })
            .and_then(
                [](int x)
                {
                    return fnfelt::monad::io::ap(
                        fnfelt::monad::io::create_async(async_fn_scale, x),
                        fnfelt::monad::io::create_async(async_identity, 7));
                });
    CHECK_EQ(pipeline.build().sync_wait(lf::lazy_pool{2}), 5007);
}

TEST_CASE("transform consults custom traits that delegate to the default on the async path")
{
    auto transformed = fnfelt::monad::io::transform<DelegatingTransformTraits>(
        fnfelt::monad::io::create_async(async_double, 10), [](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, DelegatingTransformTraits>);
    CHECK_EQ(transformed.build().sync_wait(lf::lazy_pool{2}), 21);
}

TEST_CASE("transform detects an async source and keeps the transformer's result type")
{
    using fnfelt::monad::io::detail::action_is_async;

    // A transform over an async source must take the async arm, so its action materialises an async
    // proxy.
    using async_transformed_io = decltype(fnfelt::monad::io::create_async(async_double, 10)
                                              .transform([](int x) { return x + 1; }));
    static_assert(action_is_async(^^async_transformed_io::action_type));

    // The composed result's value is the transformer's result type.
    static_assert(std::is_same_v<async_transformed_io::value_type, int>);
    using string_transformed_io = decltype(fnfelt::monad::io::create_async(async_double, 10)
                                               .transform([](int x) { return std::to_string(x); }));
    static_assert(std::is_same_v<string_transformed_io::value_type, std::string>);

    // A transform over a sync source stays on the sync arm.
    using sync_transformed_io = decltype(fnfelt::monad::io::create([] { return 10; })
                                             .transform([](int x) { return x + 1; }));
    static_assert(!action_is_async(^^sync_transformed_io::action_type));
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
