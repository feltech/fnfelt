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

#include <fnfelt/monad/io.hpp>
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
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;

/// Custom traits that accept valid actions and delegate transform validation to the default.
struct DelegatingIOTraits
{
    static constexpr std::string_view name = "DelegatingIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_transform(
        std::meta::info io_meta, std::meta::info transformer_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_transform(io_meta, transformer_meta);
    }
};

/// Constant-evaluated IO pipeline transforming via the member overload.
consteval int constexpr_pipeline_transform()
{
    constexpr auto transformed =
        fnfelt::monad::io::create([] { return 1; }).transform([](int x) { return x + 1; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the named member overload.
consteval int constexpr_pipeline_transform_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto transformed =
        fnfelt::monad::io::create([] { return 1; })
            .template transform<"Named transform"_ss>([](int x) { return x + 1; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the explicit-traits member overload.
consteval int constexpr_pipeline_transform_traits()
{
    constexpr auto transformed =
        fnfelt::monad::io::create([] { return 1; })
            .template transform<CustomIOTraits>([](int x) { return x + 1; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the free transform function.
consteval int constexpr_pipeline_transform_free()
{
    constexpr auto transformed = fnfelt::monad::io::transform(
        fnfelt::monad::io::create([] { return 1; }), [](int x) { return x + 1; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the free named transform function.
consteval int constexpr_pipeline_transform_free_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto transformed = fnfelt::monad::io::transform<"Named free transform"_ss>(
        fnfelt::monad::io::create([] { return 1; }), [](int x) { return x + 1; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline transforming via the free explicit-traits transform function.
consteval int constexpr_pipeline_transform_free_traits()
{
    constexpr auto transformed = fnfelt::monad::io::transform<CustomIOTraits>(
        fnfelt::monad::io::create([] { return 1; }), [](int x) { return x + 1; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline spreading a pair value into the transformer.
consteval int constexpr_pipeline_spread()
{
    constexpr auto transformed = fnfelt::monad::io::create([] { return std::pair{1, 2}; })
                                     .transform([](int x, int y) { return x + y; });
    return transformed().sync_wait();
}

/// Constant-evaluated IO pipeline composing transform with a subsequent and_then.
consteval int constexpr_pipeline_transform_and_then()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .transform([](int x) { return x + 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline composing and_then with a subsequent transform.
consteval int constexpr_pipeline_and_then_transform()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); })
            .transform([](int x) { return x * 10; });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline composing transform with a subsequent ap.
consteval int constexpr_pipeline_transform_ap()
{
    constexpr auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x * 10; }; }),
        fnfelt::monad::io::create([] { return 2; }).transform([](int x) { return x + 1; }));
    return applied().sync_wait();
}

/// Constant-evaluated IO pipeline composing ap with a subsequent transform.
consteval int constexpr_pipeline_ap_transform()
{
    constexpr auto applied =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create([] { return [](int x) { return x * 10; }; }),
            fnfelt::monad::io::create([] { return 2; }))
            .transform([](int x) { return x + 1; });
    return applied().sync_wait();
}
}  // namespace

TEST_CASE("IO transform applies a direct transformer to the source's value")
{
    using fnfelt::monad::io::IO;

    auto const transformed = IO{[] { return 1; }}.transform([](int x) { return x + 1; });
    CHECK_EQ(transformed().sync_wait(), 2);
}

TEST_CASE("IO transform spreads pair and tuple values into the transformer")
{
    using fnfelt::monad::io::IO;

    auto const transformed_pair =
        IO{[] { return std::pair{1, 2}; }}.transform([](int x, int y) { return x + y; });
    CHECK_EQ(transformed_pair().sync_wait(), 3);

    auto const transformed_tuple =
        IO{[] { return std::tuple{3, 4}; }}.transform([](int x, int y) { return x * y; });
    CHECK_EQ(transformed_tuple().sync_wait(), 12);
}

TEST_CASE("IO transform prefers direct invocation over spreading")
{
    using fnfelt::monad::io::IO;

    // The transformer accepts the whole pair, so it must be invoked directly even though the value
    // could also be spread.
    auto const transformed = IO{[] { return std::pair{1, 2}; }}.transform(
        [](std::pair<int, int> pair_value) { return pair_value.first * 10 + pair_value.second; });
    CHECK_EQ(transformed().sync_wait(), 12);

    // An overloaded functor pins precedence at runtime: the pair-taking overload must win.
    auto const overloaded = IO{[] { return std::pair{1, 2}; }}.transform(OverloadedTransformer{});
    CHECK_EQ(overloaded().sync_wait(), 12);
}

TEST_CASE("IO transform chains through successive transformers")
{
    using fnfelt::monad::io::IO;

    auto const transformed = IO{[] { return 1; }}
                                 .transform([](int x) { return x + 1; })
                                 .transform([](int x) { return x * 10; });
    CHECK_EQ(transformed().sync_wait(), 20);
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
    auto const transformed = IO{[] { return 1; }}.transform(transformer_lvalue);
    CHECK_EQ(transformed().sync_wait(), 11);

    // The lvalue is untouched, so it remains usable.
    CHECK_EQ(transformer_lvalue(2), 12);
}

TEST_CASE("IO transform works on a const IO and with a move-only transformer")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 5; }};
    auto transformed = source.transform(MoveOnlyTransformer{});
    CHECK_EQ(std::move(transformed)().sync_wait(), 7);
}

TEST_CASE("IO with custom traits transforms and runs end-to-end")
{
    using fnfelt::monad::io::IO;

    auto const source = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    auto const transformed =
        source.template transform<DelegatingIOTraits>([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, DelegatingIOTraits>);
    CHECK_EQ(transformed().sync_wait(), 2);
}

TEST_CASE("transform member defaults to IOTraits for the result")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Source IO with custom traits: the result takes the default IOTraits, not the source's.
    IO const source{[] { return 1; }};
    auto const transformed = source.transform([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, IOTraits>);
    CHECK_EQ(transformed().sync_wait(), 2);
}

TEST_CASE("transform member with explicit traits uses those traits for the result")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 1; }};
    auto const transformed = source.template transform<CustomIOTraits>([](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, CustomIOTraits>);
    CHECK_EQ(transformed().sync_wait(), 2);
}

TEST_CASE("transform member with a name names the result")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    IO const source{[] { return 1; }};
    auto const plus_one = [](int x) { return x + 1; };
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    auto const transformed = source.template transform<"Named transform"_ss>(plus_one);
    static_assert(decltype(transformed)::traits::name == "Named transform");
    CHECK_EQ(transformed().sync_wait(), 2);
}

TEST_CASE("free transform uses the default, named and explicit-traits forms")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;
    using namespace fnfelt::literals;  // NOLINT

    auto const source = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    auto const transformed = fnfelt::monad::io::transform(source, [](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(transformed)::traits, IOTraits>);
    CHECK_EQ(transformed().sync_wait(), 2);

    auto const named = fnfelt::monad::io::transform<"Named free transform"_ss>(
        source, [](int x) { return x + 1; });
    static_assert(decltype(named)::traits::name == "Named free transform");
    CHECK_EQ(named().sync_wait(), 2);

    auto const trait_bound =
        fnfelt::monad::io::transform<CustomIOTraits>(source, [](int x) { return x + 1; });
    static_assert(std::is_same_v<decltype(trait_bound)::traits, CustomIOTraits>);
    CHECK_EQ(trait_bound().sync_wait(), 2);
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
    CHECK_EQ(transform_then_and_then().sync_wait(), 20);

    // and_then then transform: (1 + 1) * 10.
    auto const and_then_then_transform =
        create([] { return 1; })
            .and_then([](int x) { return create([x] { return x + 1; }); })
            .transform([](int x) { return x * 10; });
    CHECK_EQ(and_then_then_transform().sync_wait(), 20);

    // transform then ap: (2 + 1) * 10.
    auto const transform_then_ap =
        ap(create([] { return [](int x) { return x * 10; }; }),
           create([] { return 2; }).transform([](int x) { return x + 1; }));
    CHECK_EQ(transform_then_ap().sync_wait(), 30);

    // ap then transform: (2 * 10) + 1.
    auto const ap_then_transform =
        ap(create([] { return [](int x) { return x * 10; }; }), create([] { return 2; }))
            .transform([](int x) { return x + 1; });
    CHECK_EQ(ap_then_transform().sync_wait(), 21);
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
    using fnfelt::monad::io::IOTraits;

    // A move-only source cannot be copied, so it must be moved into the TransformAction member. The
    // action is move-only, hence so is the resulting IO.
    MoveTrackingAction::moves = 0;
    auto bound = IO<MoveTrackingAction, IOTraits<>>{MoveTrackingAction{}}.transform(
        [](int x) { return x + 1; });
    CHECK_EQ(std::move(bound)().sync_wait(), 2);
    static_assert(!std::is_copy_constructible_v<decltype(bound)>);
    // Five moves. Construction: the temporary action into the source IO's action member; the source
    // IO into the TransformAction's source member; and the TransformAction into the returned IO's
    // action member (three). Running: the IO's action into the RunProxy by value; and the nested
    // source IO into its own RunProxy when the TransformAction runs (two). The rvalue temporary is
    // elided into the IO constructor's by-value parameter, so it costs no extra move.
    CHECK_EQ(MoveTrackingAction::moves, 5);

    // Each further hop moves the whole nested source IO once into the new TransformAction member
    // and the new TransformAction once into the returned IO's action member, so the two-hop chain
    // costs two more moves than the one-hop chain to construct. Running adds one more nested
    // source-IO move for the extra hop, so eight in total.
    MoveTrackingAction::moves = 0;
    auto chained = IO<MoveTrackingAction, IOTraits<>>{MoveTrackingAction{}}
                       .transform([](int x) { return x + 1; })
                       .transform([](int x) { return x * 10; });
    CHECK_EQ(std::move(chained)().sync_wait(), 20);
    static_assert(!std::is_copy_constructible_v<decltype(chained)>);
    CHECK_EQ(MoveTrackingAction::moves, 8);

    // The free function is a forwarding reference too, so a directly-passed prvalue rvalue source
    // costs the same as the member form (the temporary is direct-initialised into the stored
    // action). Pinned here so a regression to a by-value source parameter would show up for the
    // move-only case.
    MoveTrackingAction::moves = 0;
    auto free_bound = fnfelt::monad::io::transform(
        IO<MoveTrackingAction, IOTraits<>>{MoveTrackingAction{}}, [](int x) { return x + 1; });
    CHECK_EQ(std::move(free_bound)().sync_wait(), 2);
    static_assert(!std::is_copy_constructible_v<decltype(free_bound)>);
    CHECK_EQ(MoveTrackingAction::moves, 5);
}

TEST_CASE("transform copies an lvalue IO source, leaving the caller's IO usable")
{
    using fnfelt::monad::io::IO;

    // A copyable action so the lvalue source can be copied; the caller's IO must be untouched.
    auto const source = IO{[] { return 1; }};
    auto const transformed = fnfelt::monad::io::transform(source, [](int x) { return x + 1; });
    CHECK_EQ(transformed().sync_wait(), 2);
    CHECK_EQ(source().sync_wait(), 1);
}

namespace transform_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using fnfelt::monad::io::IO;
using fnfelt::monad::io::IOTraits;

using SourceIO = IO<int (*)(), IOTraits<"SourceIO"_ss>>;

/// Transformer returning a plain int; its display string is stable.
struct IntTransformer
{
    int operator()(int) const;
};

/// Transformer that is not callable with the source value, so no result is determinable.
struct UncallableTransformer
{
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
    // Incompatible transformer value: no result determinable.
    using string_transformer_type = decltype([](std::string) { return 1; });
    static_assert(
        maybe_transformer_result_name<^^value_io_type, ^^string_transformer_type>() == "<unknown>");
    // Incomplete transformer: no result determinable.
    static_assert(maybe_transformer_result_name<^^value_io_type, ^^Incomplete>() == "<unknown>");
    // Non-IO source: no result determinable.
    static_assert(maybe_transformer_result_name<^^int, ^^transformer_type>() == "<unknown>");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
