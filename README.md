# fnfelt

C++ monadic programming framework without type erasure.

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
