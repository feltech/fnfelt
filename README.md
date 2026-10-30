# fnfelt

C++26 monadic programming framework without type erasure.

Provides monadic template types

- `IO` for encapsulating a side-effecting action with automatic (applicative) concurrency;
- `ReaderIO` adding a read-only environment for the IO effect whilst maintaining concurrency;
- `StateIO` allowing the environment to be (sequentially) modified between IO effects.

Each support standard monadic operations

- `lift` promotes `IO` -> `ReaderIO` or `IO` / `ReaderIO` -> `StateIO`;
- `tranform` mutates the wrapped (result of side-effect) value (Functor);
- `ap` / `sequence` provides concurrent continuation (Applicative);
- `select` adds a hook for fallback/caching (Selective);
- `and_then` provides sequential continuation (Monad).
- `repeat_until` adds loop support in lieu of C++ tail-call recursion guarantees.
- ... and more.

Describe independent work, combine its results, then perform the next effect. This small report
reads two files concurrently with `sequence`, shares an immutable `immer::box` state through
`ReaderIO`, and uses `StateIO` to accumulate their line counts. Each read uses `transform` to
resolve its path and `and_then` to compose the dependent file IO:

```cpp
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

struct StateData {
    std::filesystem::path root;
    int total_lines = 0;
};

using state_type = immer::box<StateData>;

int main()
{
    // Describe a file's line count as a reusable ReaderIO computation.
    auto lines = [](char const* name) {
        // ReaderIO reads the configuration from the current state.
        return reader_io::create<state_type>(
            [](state_type const& state) { return io::just(state->root); })
            // transform computes the file path without performing an effect.
            .transform([name](std::filesystem::path root) { return root / name; })
            // and_then builds the next effect from the computed path.
            .and_then([](std::filesystem::path path) {
                // lift promotes the deferred file IO into ReaderIO.
                return reader_io::lift<state_type>(io::create([path = std::move(path)] {
                    std::ifstream file(path);
                    if (!file) throw std::runtime_error{"Cannot open input"};
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
        .store([](int readme, int license, state_type state) {
            return std::move(state).update([&](StateData next) {
                next.total_lines += readme + license;
                return next;
            });
        });

    // build supplies the initial state; sync_wait runs the IO and waits for its result.
    auto [counts, state] = report.build(state_type{StateData{.root = "."}})
                            .sync_wait(lf::lazy_pool{2});
                            
    // counts is the final value emitted by the pipeline, state is the accumulated state.                        
    auto [readme, license] = counts;
    assert(readme + license == state->total_lines);
    
    std::println("Docs: {} README lines + {} license lines = {} total",
                 readme, license, state->total_lines);
    return state->total_lines;
}
```

Run from the repository root. The pipeline stays lazy until `sync_wait`: `sequence` runs the two
file-reading actions on a pool of two workers, then `store` adds their results to the state. The box
provides immutable access to both configuration and statistics.

## stdexec interoperability

`await_sender` bridges [stdexec](https://github.com/NVIDIA/stdexec) (`std::execution` whilst we are
waiting for compilers to catch up) senders into libfork coroutines, including asynchronous actions
created with `create_async`:

```cpp
#include <utility>

#include <libfork/core/task.hpp>
#include <libfork/schedule/lazy_pool.hpp>
#include <stdexec/execution.hpp>

#include <fnfelt/interop/stdexec.hpp>
#include <fnfelt/monad/io.hpp>

auto action = [](auto self, auto sender)
    -> lf::task<fnfelt::interop::sender_outcome_t<decltype(sender)>> {
    co_return co_await fnfelt::interop::await_sender(self.context(), std::move(sender));
};

auto program = fnfelt::monad::io::create_async(action, stdexec::just(42));
auto outcome = std::move(program).build().sync_wait(lf::lazy_pool{2});
int value = outcome.value().value(); // 42; throws if stopped or failed.
```

The [TCP echo demo](tests/demo/stdexec_tcp_echo.cpp) uses standalone Asio senders with concurrent
ReaderIO client and server actions. Application I/O contexts can be passed through ordinary
arguments or a ReaderIO environment, and must outlive the operations using them.

`sender_outcome_t` derives its types from the sender's completion signatures:

```cpp
std::optional<std::expected<Value, std::expected<Error, std::exception_ptr>>>
```

An empty optional means stopped. Otherwise, the outer expected holds either a successful `Value` or
an inner expected: its value holds a sender-provided `Error`, while its error holds an exception
from storing the completion result.

Success arguments are stored as owned, decayed values. Their shape depends on the signatures:

| Value completion signatures                  | `Value`                          |
| -------------------------------------------- | -------------------------------- |
| `set_value()`                                | `std::monostate`                 |
| `set_value(int)`                             | `int`                            |
| `set_value(int, std::string)`                | `std::tuple<int, std::string>`   |
| `set_value(int)` or `set_value(std::string)` | `std::variant<int, std::string>` |

Error signatures similarly give a direct `Error` type or a variant of alternatives. Duplicate types
after decay are merged; a channel with no signatures uses an unreachable `std::monostate`
placeholder. For example, `stdexec::just(42)` gives
`std::optional<std::expected<int, std::expected<std::monostate, std::exception_ptr>>>`.

## Compile-time validation

Readable compile-time errors are a core goal: composing callables should not require deciphering
nested template types and lambda signatures to find a mistake. The library validates composition
with `static_assert` before building the next action, making use of C++26 reflection. For example,
this deliberately invalid pipeline produces an `int`, but its transformer expects a `std::string`.
The named template overloads identify both the source and the failing step in the error message:

```cpp
#include <string>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/static_string.hpp>

namespace io = fnfelt::monad::io;
using namespace fnfelt::literals;

int main()
{
    auto invalid = io::create<"Read count"_ss>([] { return 42; })
        .transform<"Count text length"_ss>([](std::string text) { return text.size(); });
}
```

Saved as `example.cpp`, GCC 16.2 reports the following complete output (line-wrapped for
readability) with the diagnostic flags below:

```text
In file included from include/fnfelt/monad/io.hpp:25,
                 from example.cpp:3:
include/fnfelt/monad/io/transform.hpp: In instantiation of
  ‘constexpr auto fnfelt::monad::io::transform(TSource&&, TTransformer&&)
   [with TTraits = detail::TransformIOTraits<(&"Count text length")>;
    TSource = IO<main()::<lambda()>, detail::CreateIOTraits<(&"Read count")> >;
    TTransformer = main()::<lambda(std::string)>]’:
include/fnfelt/monad/io/transform.hpp:115:63:
  [ skipping 2 instantiation contexts, use -ftemplate-backtrace-limit=0 to disable ]
example.cpp:12:43:   required from here
include/fnfelt/monad/io/transform.hpp:75:21: error: static assertion failed:
  fnfelt: IO transform error: Count text length{(Read count()) => unsigned long}:
  transformer function does not accept the source IO's value:
  transformer_type {aka main()::<lambda(std::string)>}
  • ‘reason.std::basic_string_view<char>::empty()’ evaluates to false
```

Here `unsigned long` is the compiler's underlying type for `std::string::size_type` (`std::size_t`).

> Compiler flags: this output uses `-ftemplate-backtrace-limit=1` to limit template instantiation
> notes, `-fno-diagnostics-show-caret` to omit source excerpts and caret lines, and
> `-fdiagnostics-color=never` for plain text. Locations and the remaining include and instantiation
> context are retained; no output was manually removed.

## Runtime dependencies

- [libfork](https://conorwilliams.github.io/libfork/) for fork-join coroutine concurrency.
- [immer](https://sinusoid.es/immer/) for immutable containers.
- [stdexec](https://nvidia.github.io/stdexec/) for interoperability with other concurrent IO libraries.

## Building

Build dependencies are managed via [Conan](https://conan.io/), which also creates CMake presets for
the build:

```bash
conan install . -of build/Release -s "&:build_type=Release" --build=missing
cmake --preset conan-release
cmake --build --parallel --preset conan-release
```

`conan install` generates a `CMakePresets.json` under the `-of` directory and a
`CMakeUserPresets.json` at the project root that includes it, so `cmake --preset` resolves after
each workflow's install. Both files are generated and gitignored: do not commit them.

### Requirements

`fnfelt` requires a compiler with C++26 (P2996) reflection support. GCC 16+ with libstdc++ is
currently the only supported toolchain. The required `-freflection` flag is applied automatically
through the installed target's usage requirements. Clang support is not yet available.

## Development

A Nix flake dev env is available with all tooling required.

```shell
nix develop ./build_env
```

Commit message linting hook

```shell
gitlint install-hook
```

Dev builds (`-Dfnfelt_ENABLE_DEV=ON`) additionally enable multiple linters (where available) and are
recommended for development (especially useful for AI coding assistants!).

The following uses `build/Debug` as the development build directory.

Configure:

```bash
conan install . -of build/Debug -s "&:build_type=Debug" --build=missing
cmake --preset conan-debug -Dfnfelt_ENABLE_DEV=ON
```

Build:

```bash
cmake --build --parallel --preset conan-debug
```

Test:

```bash
ctest --test-dir build/Debug --output-on-failure --timeout 300
```
