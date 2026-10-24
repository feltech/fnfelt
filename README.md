# fnfelt

C++ monadic programming framework without type erasure.

Describe independent work, combine its results, then perform the next effect. This small report
reads two files concurrently with `sequence`, shares configuration through `ReaderIO`, and lifts an
`IO` action to print the summary:

```cpp
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>

namespace io = fnfelt::monad::io;
namespace reader = fnfelt::monad::reader_io;

struct Config { std::filesystem::path root; };

int main()
{
    auto lines = [](char const* name) {
        return reader::create<Config>([name](Config const& config) {
            return io::create([path = config.root / name] {
                std::ifstream file(path);
                if (!file) throw std::runtime_error{"Cannot open input"};
                int count = 0;
                for (std::string line; std::getline(file, line);) ++count;
                return count;
            });
        });
    };

    auto report = reader::sequence<Config>(lines("README.md"), lines("LICENSE"))
        .transform([](int readme, int license) {
            return std::format("Docs: {} README lines + {} license lines = {} total",
                               readme, license, readme + license);
        })
        .and_then([](std::string summary) {
            return reader::lift<Config>(io::create([summary] {
                std::println("{}", summary);
                return 0;
            }));
        });

    return report.build(Config{"."}).sync_wait(lf::lazy_pool{2});
}
```

Run from the repository root. The pipeline stays lazy until `sync_wait`: `sequence` runs the two
file-reading actions on a pool of two workers, `transform` combines their results, and `and_then`
runs the dependent output action. Change the root passed to `build` to reuse the same report for
another checkout.

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
