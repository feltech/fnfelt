# fnfelt — Agent Orientation

## Environment

A Nix flake dev env is available in build_env and ensures all tools are installed. In an interactive
shell, the dev env is activated automatically via `direnv`.

Detect if you're working in a dev shell by checking the existence of `IN_NIX_SHELL` environment
variable.

If not in the dev shell, then prefix all commands below with `nix develop ./build_env --command `.

Seek explicit permission from the user before editing `build_env/flake.nix`.

Some non-standard command line tools are available in PATH that may be helpful:

- JSON parser `jq` - always use first for JSON.
- html parser `pup`.
- YAML, JSON, INI and XML processor `yq`.
- GitHub CLI `gh` (avoid `gh api` unless there is no alternative).
- Image manipulation `imagemagick`.

You have permission to use these tools in bash (and several others), but require approval before you
can use scripts in Python or bash/shell. Use targeted tools before resorting to the Bash tool with
Python or shell scripts to avoid awaiting approval.

In particular, use the Write or Edit tools before using the Bash tool with Python, `cat` or shell
scripts, including when writing to `/tmp/opencode`.

Strongly prefer the read/write/edit tools over bash tool. Avoid patterns like
`cat > some_file.txt << 'EOF'` - use the write tool instead.

Use the read and grep tools before resorting to reading and parsing files with a Python script.

NEVER do `find /` nor `find /nix/store` - it will take an intractable amount of time.

When probing/testing and need a compiler, use `g++`, `cmake`, etc available in `PATH` - do not use
absolute paths to the tool. Write probe test executables under `/tmp/opencode`, where you have write
and execute permissions.

## Build and test

The full build and test cycle is (not all steps required depending what was edited)

```bash
conan install . -of build/Debug --build=missing -s "&:build_type=Debug" \
  -pr:a build_env/nixos.linux.conan.profile
cmake --preset conan-debug -Dfnfelt_ENABLE_DEV=ON
cmake --build --parallel --preset conan-debug
ctest --test-dir build/Debug --output-on-failure --timeout 300
```

## Linting, formatting and testing

`clang-format`, `cmake-lint`, `cmake-format`, `doxygen` and `mdformat` linters are integrated into
the build when `fnfelt_ENABLE_DEV=ON` such that the build fails on linter warning.

`clang-tidy` is currently **disabled** (see the TODO in `CMakeLists.txt`) because it does not
support C++26 reflection, so none of its checks gate the build. In particular, the naming rules
below are not machine-enforced for now: apply them by hand, and keep `.clang-tidy` and the Naming
section in sync.

Only suppress linter issues as a last resort.

Each linter is available on PATH.

Commit messages are linted automatically via commit-msg hook using `gitlint`.

Testing uses C++ doctest - very similar to Catch2 syntax.

### Auto-format

If formatting is required

- CMake files: `cmake-format -i CMakeLists.txt` - where `CMakeLists.txt` is the file to format.
- C++ files: `clang-format -i main.cpp` - where `main.cpp` is the file to format.
- Markdown files: `mdformat README.md` - where `README.md` is the file to format.

## Creating new files

### Copyright header

All files that can support inline comments should have a copyright header

```shell
# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell
```

Where `#` is the comment prefix for the file type.

### Docstrings

Docstrings for C++ use javadoc style with no @brief necessary (JAVADOC_AUTOBRIEF=YES).

All C++ headers should start with an `@file` doxygen description after the copyright

```c++
/**
 * @file public_header.hpp
 *
 * Short description.
 *
 * Longer description if useful.
 */
```

Docstrings do not need `@brief` (i.e. JAVADOC_AUTOBRIEF is enabled).

Keep docstrings concise. Do not explain implementation details. Focus on why and what rather than
how.

### Header order

Headers should be ordered as follows:

- Associated header (if applicable)
- Standard library headers
- Third-party library headers
- fnfelt public headers (`<>` surround)
- fnfelt private headers (`""` surround)

With a space in-between each section, e.g.

```c++
#include <fnfelt/feature.hpp>

#include <optional>
#include <string>

#include <libfork>

#include <fnfelt/util.hpp>

#include "./private.hpp"
```

cpplint will partially enforce header order but can get confused, e.g. not recognizing `<meta>`. To
fix, place the header in its own section, e.g.

```c++
#include <meta>

#include <optional>
#include <string>
```

## Naming

std-aligned. `.clang-tidy` is the authoritative source; the rules below mirror it. `clang-tidy` is
currently disabled (C++26 reflection), so apply these rules by hand - do not rely on a diagnostic.
When `.clang-tidy` changes, update this section in the same commit.

### Casing

These are the casings configured in `.clang-tidy` (`readability-identifier-naming`).

- Namespaces and inline namespaces: `lower_case`.
- Classes, structs, unions, abstract classes and enums: `CamelCase`. This includes trait and
  dispatch structs and data types (e.g. `Unspecialise`, `Sequence`, `IO`).
- Concepts: `lower_case` (e.g. `rebindable`, `applicable_with`).
- Type aliases (`using`) and `typedef`s: `lower_case`. Disambiguate when necessary by adding a
  `_type` suffix, e.g. `action_type`. Namespace-scope computed `::type` aliases take a `_t` suffix
  (e.g. `rebind_t`); member aliases take no `_t` (e.g. `Unspecialise<C>::specialise<TArgs...>`,
  `type`).
- Enum constants, scoped or unscoped: `lower_case`.
- Functions and methods: `lower_case`.
- Variables, constants, `constexpr` variables, non-static data members and parameters: `lower_case`.
- Type template parameters (including template-template parameters): `CamelCase` prefixed `T` (e.g.
  `TContainer`, `TArgs`); a lone `T` is allowed. The prefix distinguishes template parameters from
  types and aliases, and avoids shadowing when a member alias renames the parameter (e.g.
  `template <class TArg> struct A { using Arg = TArg; };`).
- Value (non-type) template parameters: `lower_case` (e.g. `idx`, `target_meta`, `error_preamble`).
  These are values rather than types, so they follow the functions-and-values convention.
- Macros: `UPPER_CASE` (ALL_CAPS constants emitted by macros are allowed).

### Identifier length

`readability-identifier-length` requires variables and parameters to be at least 3 characters, with
`fn` exempted. Loop counters must be at least 2 characters, but the conventional one-letter counters
`i`, `j` and `k` are exempt; caught exception variables must be at least 2, but `e` is exempt.

### Semantic conventions

clang-tidy cannot express these; follow them by hand.

- Concepts: no prefix/suffix. Capabilities of one type take `-able` (e.g. `rebindable`); predicates
  over multiple arguments take a trailing preposition (e.g. `applicable_with`, `specialisation_of`);
  abstractions are nouns (e.g. `transformer`).
- Functors: `CamelCase` struct (e.g. `Construct`, `Sequence`) with a `lower_case` `inline constexpr`
  instance where a value form helps (e.g. `construct`). Use at most one call-facing form per verb.

### Test files

Doctest encourages placing tests in the program's translation units, which are disabled by defining
`DOCTEST_CONFIG_DISABLE` for production.

This project is header-only and so translation units only contain tests. If this changes, notify the
user that AGENTS.md needs updating.

Wrap all test-only `.cpp` code for files in `src/` in `#ifndef DOCTEST_CONFIG_DISABLE` and
`#endif // DOCTEST_CONFIG_DISABLE`, including headers that are only used for tests.

Translation units are found under `src/` and largely mirror the structure of `include/`.

Compile-fail tests (code that must NOT compile) live under `tests/compile_fail/` and are driven by
CMake - see that directory's CMakeLists.txt. Use a subdirectory per source file, with test cases as
files under that subdirectory. Use corresponding `.` delimiter in CMake test names.

Compile-time validation structures should be injected into types so that the validator function can
be tested without compilation failure, and so only a single compile_fail test is needed using a stub
validator.

## Coding conventions

Use C++26 reflection extensively for compile-time logic, in particular for type validation with
clear human-readable error messages.

Reflection documentation starting point: https://cppreference.com/cpp/meta/reflection

Load the `cpp26-reflection` skill (`.opencode/skills/cpp26-reflection/SKILL.md`) before writing
reflection code. It records verified gcc 16.2 behaviour, API signatures and gotchas, so there is no
need to scan the gcc source tree or write probe executables for what it covers.

Read the online documentation in preference to performing live probes of the compiler.

If live probes are still required, report what information was missing back to the user so that the
skill can be updated.

C++ `concept`s should be used for overload resolution and as boolean utilities. Avoid using concepts
purely to constrain a type or function (the errors are difficult to read) - use reflection instead.

Use ADL (argument-dependent lookup) where possible, for cleaner code, e.g.

```c++
bool check_complete(std::meta::info info) 
{
  return is_complete_type(action_meta);  // not std::meta::is_complete_type
}
```

Maintain a FILE_SET in CMake listing public headers for the main target.

`constexpr` All The Things.
