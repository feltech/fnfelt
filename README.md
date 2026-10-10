# fnfelt

C++ monadic programming framework without type erasure.

## Usage

`fnfelt` is a monadic IO framework: `create` wraps a side-effecting action, `and_then` sequences
IOs, `transform` maps a value and `ap` applies one IO's callable to another's value. Compositions
are validated at compile time through C++26 reflection, with readable diagnostics and no type
erasure.

```c++
#include <fnfelt/monad/io.hpp>

using namespace fnfelt::literals;  // "name"_ss
using namespace fnfelt::monad::io;

auto program = create<"read"_ss>([] { return 20; }).and_then<"increment"_ss>(
    [](int value) { return create([value] { return value + 1; }); });

auto const result = program.build().sync_wait();  // 21
```

Calling an IO yields a run proxy; `sync_wait()` is the only execution path. Purely synchronous
pipelines remain constant-evaluable.

### Async

`create_async` wraps a stateless callable returning `lf::task<T>` into an async leaf; the callable
takes a libfork context (`auto`) first, before the bound arguments, e.g.
`[](auto, int x) -> lf::task<int> { co_return x; }`. Synchronous and asynchronous IOs compose freely
in `and_then` and `ap` chains, running as one tree under a single top-level scheduler. On the async
path `ap` runs its two sub-IOs concurrently. `sync_wait()` optionally accepts a libfork scheduler
(e.g. `lf::lazy_pool`) to service the tree, defaulting to a freshly constructed one.

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
