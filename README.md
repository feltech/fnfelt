# fnfelt

C++ monadic programming framework without type erasure.

## Building

Build dependencies are managed via [Conan](https://conan.io/), which also creates CMake presets for
the build:

```bash
conan install . -of build --build=missing
cmake --preset conan-release
cmake --build --parallel --preset conan-release
```

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

The following assumes a chosen build directory of `build`.

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
ctest --test-dir build --output-on-failure --timeout 300
```
