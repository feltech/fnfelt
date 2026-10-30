// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <cassert>
#include <filesystem>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>

#include <immer/box.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>
#include <fnfelt/monad/state_io.hpp>

namespace io = fnfelt::monad::io;
namespace reader_io = fnfelt::monad::reader_io;
namespace state_io = fnfelt::monad::state_io;

struct StateData
{
    std::filesystem::path root;
    int total_lines = 0;
};

using state_type = immer::box<StateData>;

int main()
{
    // Describe a file's line count as a reusable ReaderIO computation.
    auto lines = [](char const * name)
    {
        // ReaderIO reads the configuration from the current state.
        return reader_io::create<state_type>([](state_type const & state)
                                             { return io::just(state->root); })
            // transform computes the file path without performing an effect.
            .transform([name](std::filesystem::path root) { return root / name; })
            // and_then builds the next effect from the computed path.
            .and_then(
                [](std::filesystem::path path)
                {
                    // lift promotes the deferred file IO into ReaderIO.
                    return reader_io::lift<state_type>(io::create(
                        [path = std::move(path)]
                        {
                            std::ifstream file(path);
                            if (!file)
                                throw std::runtime_error{"Cannot open input"};
                            int count = 0;
                            for (std::string line; std::getline(file, line);) ++count;
                            return count;
                        }));
                });
    };

    // sequence combines independent ReaderIO actions for concurrent execution.
    auto reads = reader_io::sequence<state_type>(lines("README.md"), lines("LICENSE"));

    // lift supplies ReaderIO with the current StateIO state.
    auto report = state_io::lift<state_type>(reads)
                      // store updates the state while preserving both line counts.
                      .store(
                          [](int readme, int license, state_type state)
                          {
                              return std::move(state).update(
                                  [&](StateData next)
                                  {
                                      next.total_lines += readme + license;
                                      return next;
                                  });
                          });

    // build supplies the initial state; sync_wait runs the IO and waits for its result.
    auto [counts, state] = report.build({StateData{.root = "."}}).sync_wait(lf::lazy_pool{2});

    auto [readme, license] = counts;
    assert(readme + license == state->total_lines);
    std::println(
        "Docs: {} README lines + {} license lines = {} total", readme, license, state->total_lines);
    return state->total_lines;
}
