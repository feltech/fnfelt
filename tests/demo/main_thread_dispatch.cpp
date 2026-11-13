// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

/**
 * @file main_thread_dispatch.cpp
 *
 * Demonstrates ReaderIO dispatch from a worker pool to the main thread.
 */

#include <atomic>
#include <cassert>
#include <future>
#include <memory>
#include <print>
#include <stop_token>
#include <thread>
#include <utility>

#include <immer/box.hpp>
#include <libfork/core/ext/context.hpp>
#include <libfork/core/ext/resume.hpp>
#include <libfork/core/ext/tls.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>

namespace reader_io = fnfelt::monad::reader_io;
namespace io = fnfelt::monad::io;

struct Runtime
{
    std::unique_ptr<lf::ext::worker_context, decltype(&lf::finalize)> main_context;
    std::thread::id main_thread_id;
    std::stop_source main_loop_stop{};
};

std::future<std::thread::id> start_program_on_worker(immer::box<Runtime> runtime);

int main() noexcept
{
    std::atomic<bool> should_main_thread_awake{false};

    // Context holding task queue for main thread.
    auto main_context = std::unique_ptr<lf::ext::worker_context, decltype(&lf::finalize)>{
        lf::worker_init(
            // Called on the submitting thread after queueing work..
            lf::nullary_function_t{[&] noexcept
                                   {
                                       // Wake main thread polling loop.
                                       should_main_thread_awake.exchange(
                                           true, std::memory_order_release);
                                       should_main_thread_awake.notify_one();
                                   }}),
        lf::finalize};

    // ReaderIO environment.
    auto runtime = immer::box<Runtime>{Runtime{
        .main_context = std::move(main_context), .main_thread_id = std::this_thread::get_id()}};

    auto worker = start_program_on_worker(runtime);

    // Main thread event loop.
    for (;;)
    {
        // Reset before checking the queue, so a racing notification is not erased.
        should_main_thread_awake.exchange(false, std::memory_order_acquire);

        // Drain and execute all pending coroutine jobs.
        if (auto * jobs = runtime->main_context->try_pop_all())
            lf::resume(jobs);

        // Break out if one of the jobs requested a stop.
        if (runtime->main_loop_stop.stop_requested())
            break;

        // Wait for new work to be posted.
        should_main_thread_awake.wait(false, std::memory_order_relaxed);
    }

    // Wait for worker thread to finish with final result from IO chain.
    auto const final_thread_id = worker.get();
    return final_thread_id == runtime->main_thread_id ? 0 : 1;
}

std::future<std::thread::id> start_program_on_worker(immer::box<Runtime> runtime)
{
    // Hop to main thread, get the thread ID, then hop back to worker.
    auto get_main_thread_id = reader_io::create<immer::box<Runtime>>(
        [](immer::box<Runtime> runtime)
        {
            assert(std::this_thread::get_id() != runtime->main_thread_id);
            auto * context = runtime->main_context.get();
            // Schedule IO on the main thread.
            return io::on(
                context,
                // Create IO that produces the thread ID that it runs in.
                io::create(
                    [runtime = std::move(runtime)]
                    {
                        auto const main_thread_id = std::this_thread::get_id();
                        assert(main_thread_id == runtime->main_thread_id);
                        std::println("Main thread: {}", main_thread_id);
                        return main_thread_id;
                    }));
        });

    // Signal completion on main so its polling loop needs no further notification.
    auto terminal_io = reader_io::create<immer::box<Runtime>>(
        [](immer::box<Runtime> runtime)
        {
            auto * context = runtime->main_context.get();
            return io::on(
                context,
                io::create(
                    [runtime = std::move(runtime)]
                    {
                        assert(std::this_thread::get_id() == runtime->main_thread_id);
                        runtime->main_loop_stop.request_stop();
                        return std::this_thread::get_id();
                    }));
        });

    // Lazy monadic IO chain "program".
    auto program = std::move(get_main_thread_id)
                       .and_then(
                           [terminal_io = std::move(terminal_io)](
                               std::thread::id const main_thread_id) noexcept
                           {
                               // This continuation is back on a worker thread.
                               assert(std::this_thread::get_id() != main_thread_id);
                               // Hop back to main thread and signal exit.
                               return terminal_io;
                           });

    // Execute and the program in a worker thread and block until the program completes. Within the
    // worker thread, another thread (pool) is created to actually execute the async tasks.
    return std::async(
        std::launch::async,
        [program = std::move(program), runtime = std::move(runtime)] noexcept
        { return program.build(runtime).sync_wait(lf::lazy_pool{1}); });
}
