// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include <libfork/core/ext/context.hpp>
#include <libfork/core/ext/resume.hpp>
#include <libfork/core/ext/tls.hpp>
#include <libfork/core/task.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/result.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
namespace io = fnfelt::monad::io;
using fnfelt::Result;
using fnfelt::literals::operator""_ss;

// A dedicated thread owns and services the target independently of the awaiting worker pool.
class TargetContext
{
public:
    TargetContext()
    {
        std::promise<lf::worker_context *> ready;
        auto published = ready.get_future();
        worker = std::jthread(
            [this, ready = std::move(ready)](std::stop_token stop) mutable
            {
                auto owner = std::unique_ptr<lf::ext::worker_context, decltype(&lf::finalize)>{
                    lf::worker_init(lf::nullary_function_t{[this] noexcept { wake(); }}),
                    lf::finalize};
                ready.set_value(owner.get());
                for (;;)
                {
                    should_awake.exchange(false, std::memory_order_acquire);
                    if (auto * jobs = owner->try_pop_all())
                    {
                        lf::resume(jobs);
                    }
                    if (stop.stop_requested())
                    {
                        break;
                    }
                    should_awake.wait(false, std::memory_order_relaxed);
                }
            });
        context = published.get();
    }

    ~TargetContext()
    {
        worker.request_stop();
        wake();
    }

    TargetContext(TargetContext const &) = delete;
    TargetContext & operator=(TargetContext const &) = delete;
    TargetContext(TargetContext &&) = delete;
    TargetContext & operator=(TargetContext &&) = delete;

    lf::worker_context * get() const
    {
        return context;
    }

    std::thread::id thread_id() const
    {
        return worker.get_id();
    }

private:
    void wake() noexcept
    {
        should_awake.exchange(true, std::memory_order_release);
        should_awake.notify_one();
    }

    std::atomic<bool> should_awake{false};
    lf::worker_context * context{};
    std::jthread worker;
};

// Observe the caller's exact worker context, rather than relying on pool thread identity.
template <class TSource>
auto observe(TSource source)
{
    return io::create_async(
        []<class TChild>(
            auto self, TChild child) -> lf::task<std::pair<typename TChild::value_type, bool>>
        {
            auto * origin = self.context();
            auto value = co_await io::detail::just_child(std::move(child));
            co_return std::pair{
                io::detail::unpack_result(std::move(value)), self.context() == origin};
        },
        std::move(source));
}

struct MoveOnlyAction
{
    std::unique_ptr<int> value;

    auto operator()() &&
    {
        return [value = std::move(value)] { return *value; };
    }
};
}  // namespace

TEST_CASE("on validates its source and supports diagnostic names")
{
    using traits = io::detail::OnIOTraits<>;
    using source_type = decltype(io::just(42));
    static_assert(traits::validate_on(^^source_type).empty());
    static_assert(traits::validate_on(^^int) == "source is not an IO");
    static_assert(traits::validate_on(^^void) == "source is not an IO");
    constexpr auto named = io::on<"Scoped"_ss>(nullptr, io::just(42));
    static_assert(decltype(named)::traits::name == std::string_view{"Scoped"});
    static_assert(std::is_same_v<typename decltype(named)::value_type, int>);
}

TEST_CASE("on executes synchronous and asynchronous sources on target and restores origin")
{
    TargetContext target;
    auto const target_thread = target.thread_id();
    SUBCASE("synchronous source remains reusable")
    {
        auto scoped = io::on(target.get(), io::create([] { return std::this_thread::get_id(); }));
        for (int idx = 0; idx < 2; ++idx)
        {
            auto [thread_id, is_origin] = observe(scoped).build().sync_wait(lf::lazy_pool{1});
            CHECK_EQ(thread_id, target_thread);
            CHECK(is_origin);
        }
    }
    SUBCASE("asynchronous source")
    {
        auto source = io::create_async(
            [](auto) -> lf::task<std::thread::id> { co_return std::this_thread::get_id(); });
        auto [thread_id, is_origin] =
            observe(io::on(target.get(), std::move(source))).build().sync_wait(lf::lazy_pool{1});
        CHECK_EQ(thread_id, target_thread);
        CHECK(is_origin);
    }
    SUBCASE("same context and nested scopes")
    {
        auto source = io::create_async(
            [](auto self) -> lf::task<bool>
            {
                auto * origin = self.context();
                auto child = io::on(origin, io::on(origin, io::just(42)));
                auto value = co_await io::detail::just_child(std::move(child));
                co_return value == 42 && self.context() == origin;
            });
        CHECK(source.build().sync_wait(lf::lazy_pool{1}));
    }
}

TEST_CASE("on preserves Result outcomes and restores origin before recovery")
{
    TargetContext target;
    using result_type = Result<int, int>;
    for (auto outcome :
         {fnfelt::result::success<result_type>(42),
          fnfelt::result::error<result_type>(7),
          fnfelt::result::stopped<result_type>()})
    {
        auto [value, is_origin] =
            observe(io::on(target.get(), io::just(outcome))).build().sync_wait(lf::lazy_pool{1});
        CHECK_EQ(fnfelt::result::is_stopped(value), fnfelt::result::is_stopped(outcome));
        CHECK_EQ(fnfelt::result::is_error(value), fnfelt::result::is_error(outcome));
        CHECK_EQ(fnfelt::result::is_success(value), fnfelt::result::is_success(outcome));
        if (fnfelt::result::is_success(outcome))
        {
            REQUIRE(fnfelt::result::is_success(value));
            CHECK_EQ(fnfelt::result::get_success(value), 42);
        }
        if (fnfelt::result::is_error(outcome))
        {
            REQUIRE(fnfelt::result::is_error(value));
            CHECK_EQ(fnfelt::result::get_error(value), 7);
        }
        CHECK(is_origin);
    }
}

TEST_CASE("on restores origin before propagating synchronous and asynchronous exceptions")
{
    TargetContext target;
    auto check_source = [&target](auto source)
    {
        auto checked = io::create_async(
            [](auto self, auto child) -> lf::task<bool>
            {
                auto * origin = self.context();
                try
                {
                    static_cast<void>(co_await io::detail::just_child(std::move(child)));
                }
                catch (std::runtime_error const & error)
                {
                    co_return self.context() == origin && std::string_view{error.what()} ==
                        "scoped failure";
                }
                co_return false;
            },
            io::on(target.get(), std::move(source)));
        CHECK(std::move(checked).build().sync_wait(lf::lazy_pool{1}));
    };
    check_source(io::create([]() -> int { throw std::runtime_error{"scoped failure"}; }));
    check_source(
        io::create_async(
            [](auto) -> lf::task<int>
            {
                throw std::runtime_error{"scoped failure"};
                co_return 0;
            }));
}

TEST_CASE("on consumes move-only sources and transports nonassignable move-only results")
{
    TargetContext target;
    auto source = io::create(MoveOnlyAction{std::make_unique<int>(42)});
    auto result = io::on(target.get(), std::move(source)).build().sync_wait(lf::lazy_pool{1});
    static_assert(!std::is_copy_constructible_v<decltype(result)>);
    static_assert(!std::is_move_assignable_v<decltype(result)>);
    CHECK_EQ(result(), 42);
}

#endif  // DOCTEST_CONFIG_DISABLE
