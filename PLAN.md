# Plan: reflection-first monadic framework

Goal: build a monadic framework (IO, ReaderIO, StateIO) in fnfelt from the ground up, using C++26
reflection (gcc-16 `-freflection`) as the core tool for compile-time logic and readable type
validation errors. Everything is fully tested (doctest + compile-fail) and fully documented
(doxygen), with consistent naming.

`~/workspace/vulkanisedfelt` (branch `monads`, `src/monad/`) is a **reference for behaviour and
idioms only** — it is a Hana-tag-dispatched stack and is **not a port source**. Its semantics are
kept (see "Reference behaviour"); its machinery (Hana tags, `*_impl` specialisations,
`Unspecialise`/`rebind_t`-style helpers) is not.

## Locked decisions

- **Reflection-first**: type inspection and validation use `std::meta::info` and `consteval`
  functions, not concepts-as-constraints or Hana-style dispatch. Concepts are for overload
  resolution and boolean utilities only. Use ADL for reflection calls (`is_complete_type(info)`, not
  `std::meta::is_complete_type`).
- **Traits pattern** (established by `IO`):
  - `<X>Traits` struct (public, in `fnfelt::monad::<name>`, e.g. `fnfelt::monad::io::IOTraits`;
    default name via `default_io_name`-style constants in `fwd.hpp`) with
    `static consteval std::string_view validate_action(std::meta::info)`; it returns an empty view
    when valid, else a short reason (e.g. `"does not return a value"`). `IO`'s traits also expose
    `validate_bind(std::meta::info io_meta, std::meta::info kleisli_meta)` (overridable by custom
    traits; the default implementation holds the full validation logic), while the reflection helper
    predicates it uses (`is_io`, `is_directly_invocable`, ...) remain free `detail` functions.
  - `<X><TAction, TTraits = <X>Traits>` runs the check in a `consteval {}` block with a
    `static_assert` whose message is built by `construct_type_error_msg`
    (`fnfelt/detail/errors.hpp`).
  - `fwd.hpp` forward-declares the class **with** the default traits argument (the default cannot be
    repeated in the definition) and forward-declares the traits struct.
  - Tests: doctest `static_assert`s on `validate_action` for valid and invalid types; a test that
    the default traits are `<X>Traits`; one compile-fail test per rejection reason; one compile-fail
    test with custom traits that reject everything. Compile-fail probes must construct the object
    (validation runs in the constructor), not just use `sizeof`.
- **Namespace**: `fnfelt::` for shared utilities; monads live in `fnfelt::monad::<name>` (e.g.
  `fnfelt::monad::io`), implementation details in a nested `detail`.
- **Naming** (std-aligned; `.clang-tidy` is authoritative but clang-tidy is currently disabled for
  C++26 reflection, so apply by hand — see AGENTS.md "Naming"): concepts `lower_case`; classes,
  structs and enums `CamelCase`; namespace-scope computed aliases `lower_case_t`; member aliases
  `lower_case` with no `_t`; type template params `CamelCase` prefixed `T`; value template params
  `lower_case`; functions, variables and parameters `lower_case`; macros `UPPER_CASE`.
- **No Hana, no immer, no range-v3, no Boost.** Use `std::ranges`. libfork is **kept** (async
  runtime) and is introduced only when the async IO stage lands.
- **Test layout** (per AGENTS.md): tests live in `src/`, mirror the `include/` path, and are wrapped
  in `#ifndef DOCTEST_CONFIG_DISABLE` / `#endif // DOCTEST_CONFIG_DISABLE`. Compile-fail tests live
  under `tests/compile_fail/` with a subdirectory per source file (e.g. `monad/io/IO/`), registered
  in that directory's `CMakeLists.txt` with `.`-delimited ctest names (e.g.
  `monad.io.IO.custom_traits`). Per-TU split for the IO monad, mirroring `include/`: `detail.hpp`
  (generic reflection helpers) is tested in `src/monad/io/detail.cpp`, `traits.hpp` (`IOTraits`) in
  `src/monad/io/traits.cpp`, `IO.hpp` (construction, run) in `src/monad/io/IO.cpp`, `create.hpp` in
  `src/monad/io/create.cpp`, `bind.hpp` in `src/monad/io/bind.cpp`, `ap.hpp` in
  `src/monad/io/ap.cpp`, and the umbrella `io.hpp` in `src/monad/io.cpp`.
- **Conventions on every file**: copyright header; `@file` doxygen; `#pragma once`; javadoc-style
  docs without `@brief` (doxygen warnings are errors over `include/`); clang-format, cmake-lint,
  cmake-format, doxygen and mdformat all green (they gate the build with `fnfelt_ENABLE_DEV=ON`).
  Public headers are listed in the CMake FILE_SET.

## What's done

### Stage R: IO from reflection

- `include/fnfelt/monad/io/IO.hpp` — `IO<TAction, TTraits = IOTraits>` holding an action; exposes
  `action` and `traits` aliases. Validation runs in a class-scope `consteval {}` block, i.e. on ANY
  instantiation of the IO type (including use as a return type), which is why compile-fail probes
  need only instantiate (constructing is not required).
- `include/fnfelt/monad/io/detail.hpp` — IO implementation details (mirroring the reference repo's
  `src/monad/detail.hpp` layout): the reflection helper predicates (`is_io`,
  `all_template_arguments_are_types`, `action_value_meta`, `is_directly_invocable`,
  `is_spread_invocable`). The bind and ap operations live in their own `bind.hpp`/`ap.hpp` (with
  `detail::BindError`/`BindAction` and `detail::ApError`/`ApAction` respectively), tested in
  `src/monad/io/bind.cpp` and `src/monad/io/ap.cpp`; `detail.hpp` holds only the generic reflection
  helpers. `IO.hpp` holds only the `IO` class; the `create` factory lives in
  `include/fnfelt/monad/io/create.hpp` (tested in `src/monad/io/create.cpp`).
- `include/fnfelt/monad/io/traits.hpp` — the public default traits `IOTraits` in `fnfelt::monad::io`
  (named via the `default_io_name` constant declared in `fwd.hpp`), a user extension point: name an
  IO (e.g. `IOTraits<"FileIO"_ss>`) or override with custom traits. Holds the action + kleisli
  validators.
- `IOTraits::validate_action(std::meta::info)` — rejects, with these messages:
  - `is not a complete type`
  - `does not return a value` (void result)
  - `is not callable with no arguments`
  - `should not return an IO` (checked with `has_template_arguments`)
- `include/fnfelt/monad/io/fwd.hpp` — forward declaration with the default traits argument.
- `include/fnfelt/detail/errors.hpp` — `construct_type_error_msg`.
- Tests: `src/monad/io/IO.cpp` holds the IO-class tests (construction, run, default-traits
  assertion); `src/monad/io/traits.cpp` holds the `IOTraits` tests (validators, naming);
  `src/monad/io/detail.cpp` holds the `detail.hpp` tests (reflection helpers) - all mirroring the
  `include/` path. Compile-fail under `tests/compile_fail/monad/io/IO/` (`not_callable`,
  `returns_void`, `custom_traits`, and others), with the `create` probe under
  `tests/compile_fail/monad/io/create/`.
- IO has **no dependency** on any legacy utility below.
- IO now has **`operator()` (run)**, the **`value`** alias and **`bind`**:
  - `operator()` runs the wrapped action and returns its value (run exposed as the call operator,
    not a named `run` member).
  - `value` splices the action's invocation result (`detail::action_value_meta`, `void` for
    incomplete/non-callable actions).
  - `bind` runs the source, applies the continuation (kleisli) to its value, then runs the
    continuation's IO. It exists as free functions (`bind(io, kleisli)`, with named and
    explicit-traits overloads) declared in `IO.hpp` before the class and defined in `bind.hpp`, plus
    `IO::bind` member overloads delegating to them. Dispatch is direct-invocation-else-tuple-spread:
    a kleisli taking the whole value wins; otherwise values that are specialisations with all-type
    template arguments (e.g. `std::pair`, `std::tuple`) are spread with `std::apply`.
  - Kleisli validation is a `IOTraits::validate_bind(io_meta, kleisli_meta)` method — the single
    injection point used by `bind` (default implementation holds the full logic; the reflection
    helper predicates remain free `detail` functions). It rejects, with these reasons:
    `source produces no value`, `is not a complete type`, `must not be a reference type`
    (validator-only), `is not a class or function pointer type`, `must be move constructible`,
    `is not callable with the value`, `must return an IO`. The kleisli parameter is taken by value
    (lvalues are copied, rvalues moved), so the `must not be a reference type` reason is
    validator-only — it is unreachable through `bind`. The result IO's traits default to
    `IOTraits<>` (the source IO's traits are not inherited). The default validator's return values
    are covered by doctest `static_assert`s; the traits injection is covered by ONE compile-fail
    probe, `monad.io.bind.custom_traits`, which uses a stub validator returning a fixed custom
    reason (per AGENTS.md "single compile_fail test with a stub validator"). Bind tests live in
    `src/monad/io/bind.cpp`; compile-fail probes under `tests/compile_fail/monad/io/bind/`.
  - Design decisions: `IO::is_io` delegates to a free `detail::is_io` helper so any-traits IOs are
    recognised (one source of truth; `template_of(spec) == ^^IO` compares template, not full type);
    an invalid bind is a `static_assert` plus a `detail::BindError` tombstone return (the
    `if constexpr` guard prevents cascade errors and yields exactly one friendly message).
  - Compile-fail: `monad.io.bind.custom_traits` (single stub-validator probe; the default
    validator's return values are covered by doctest `static_assert`s).
  - The free `bind` has an additional constrained overload for a non-IO source
    (`requires (!detail::is_io(^^TNotAnIO))`, with defaulted `IOTraits<>` traits), selected when the
    source is not an IO. It produces a friendly static_assert
    (`"fnfelt: IO bind error: ...: is not an IO: ..."`) and returns `detail::BindError` so the
    return type stays well-formed (e.g. when deduced by `auto`), mirroring the IO overloads. The
    `reason` is always non-empty for a non-IO, so the guard is symmetrical with them rather than
    load-bearing. Compile-fail probe `monad.io.bind.not_io` under
    `tests/compile_fail/monad/io/bind/` (16/16 ctest green).
  - The **`create`** free function factory wraps a callable action in an IO, perfect-forwarding it
    (lvalues are copied, rvalues moved, function lvalues decay to function pointers). It has two
    overloads, keyed on the leading template argument: a name (`create<"My IO"_ss>(action)`,
    yielding `IOTraits<name>`), or a traits type (`create<MyTraits>(action)`, defaulting to
    `IOTraits`). A free function template is used because class CTAD cannot take an explicit leading
    `IOTraits` argument while deducing the rest — the function template is the workaround. It lives
    in `include/fnfelt/monad/io/create.hpp`, tested in `src/monad/io/create.cpp`; compile-fail probe
    `monad.io.create.create_invalid_action` under `tests/compile_fail/monad/io/create/`.
  - The IO pipeline is fully **`constexpr`**: `IO`'s constructor, run (`operator()`) and `bind`,
    plus `detail::BindAction`'s call operator, are all `constexpr`, so IOs (including bind chains)
    run in constant-evaluated contexts.
- IO now has **`ap`** — the free function `fnfelt::monad::io::ap(fn_io, value_io)` (sync for now;
  the concurrent libfork version is deferred to stage b):
  - Both IOs are taken by value; it runs the function IO then the value IO and applies with
    `std::invoke(std::move(fn), std::move(value))`. Unlike `bind`, it does NOT spread tuple values.
  - Result traits default to `IOTraits<>` (not inherited from the argument IOs); named and
    explicit-traits overloads exist, as for `bind`.
  - Invalid arguments yield a friendly `static_assert` whose message mirrors `bind`'s structured
    form: `fnfelt: IO ap error: <name>{(FnIO(ValueIO()) => Result)}: <reason>: <offending type>`,
    built by `detail::ap_error_msg`, plus a `detail::ApError` tombstone return. The general
    `maybe_io_name` helper lives in `detail.hpp` (moved from `bind.hpp`), alongside
    `maybe_ap_result_name` (the application's result, or `"<unknown>"`).
  - `ApError`/`ApAction<TFnIO, TValueIO>` live in `include/fnfelt/monad/io/ap.hpp` (the reflection
    helpers `io_action_meta`/`io_value_meta` remain in `detail.hpp`; the latter via
    `action_value_meta`; both return `^^void` for non-IO types to avoid consteval throws).
  - `IOTraits` validator: `validate_ap(fn_io_meta, value_io_meta)` — kind check (`"is not an IO"`),
    then compatibility (`"function IO's value is not callable with the value IO's value"`), then
    void result (`"does not return a value"`).
  - The free `ap` has an additional constrained overload for a non-IO argument
    (`requires (!detail::is_io(^^TFnNotAnIO) || !detail::is_io(^^TValNotAnIO))`, with defaulted
    `IOTraits<>` traits), selected when either argument is not an IO. It produces the same
    structured static_assert, naming whichever argument is not an IO as the offending type, and
    returns `detail::ApError` so the return type stays well-formed, mirroring the IO overloads.
    Compile-fail probes `monad.io.ap.fn_not_io` and `monad.io.ap.value_not_io` under
    `tests/compile_fail/monad/io/ap/`. Known gap for parity with `bind`: the named overload
    (`ap<"name"_ss>(...)`) has no fallback, so a named call with a non-IO argument still yields a
    plain no-matching-function error; a named fallback could be added later if wanted.
  - Tests: doctest `static_assert`s in `src/monad/io/traits.cpp`; end-to-end/constexpr/no-spread/
    traits-propagation/by-value tests in `src/monad/io/ap.cpp`; compile-fail probes
    `monad.io.ap.custom_traits`, `monad.io.ap.incompatible`, `monad.io.ap.returns_void`,
    `monad.io.ap.fn_not_io`, `monad.io.ap.value_not_io` under `tests/compile_fail/monad/io/ap/`.
- **Umbrella header**: `include/fnfelt/monad/io.hpp` is the public interface. It includes the seven
  `io/` sub-headers (`IO`, `ap`, `bind`, `create`, `detail`, `fwd`, `traits`), each marked
  `// IWYU pragma: export`; each sub-header carries `// IWYU pragma: private, include "../io.hpp"`.
  The sub-headers remain individually includable/standalone (`bind.hpp`/`ap.hpp`/`create.hpp`
  include `IO.hpp`). The umbrella has its own doctest TU, `src/monad/io.cpp`, locking its
  completeness (create/member-bind/free-bind/ap end-to-end and in a `constexpr` pipeline).

### Legacy utilities (ported before the pivot) — status: under review

These were ported when the plan was a straight port. They are kept in the tree but **no new stage
may build on them**. See "Utility audit".

- `include/fnfelt/type_traits.hpp` — `Unspecialise` (with friendly `static_assert` on the primary),
  `detail::TypeSpecialisation`/`type_specialisation`, `strippable_to_specialisation`, `rebindable`,
  `detail::Rebind`, and the public `rebind_t` (replaces ALL template type args; non-type-param
  containers such as `std::array<T, N>` are rejected with a friendly error). Tests in
  `src/type_traits.cpp`; compile-fail probes in `tests/compile_fail/type_traits/`.
- `include/fnfelt/invoke.hpp` — `applicable_with` concept, `detail::InvokeOrApplyResult` dispatch
  (direct invocation wins over tuple-spreading), and `invoke_or_apply_result_t` (decays the value
  first). Tests in `src/invoke.cpp`; compile-fail probes in `tests/compile_fail/invoke/`.
- `include/fnfelt/functors.hpp` — `Construct`, `TransformConcat`, `TransformRangeToCheckNonEmpty`,
  `TransformRangeToFrontElem`, `TransformMaybesToValues`, `TransformBoolToOptional`, `PairWith`,
  `mem_fn::value_of`, `attr::{first,second,get_nth}`, `views::{value_of,cast}`. Tests in
  `src/functors.cpp`.
- Compile-fail harness: `fnfelt_test_compile_fail(<name> SOURCE <f> ERROR_REGEX <re>)` in
  `tests/compile_fail/CMakeLists.txt` (gated by `PASS_REGULAR_EXPRESSION` only — `WILL_FAIL` inverts
  regex-match passes).

## Utility audit

Rule: a legacy utility is **kept only if a reflection-first monad actually needs it**, and is
re-justified (and likely re-implemented with reflection) when first needed. Otherwise it is
**removed** in the cleanup pass (stage h). Nothing is deleted before then.

| Utility                                                                  | Likely disposition                                                                         |
| ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------ |
| `Unspecialise` / `rebind_t` / `rebindable`                               | Likely remove; reflection (`template_of`, `template_arguments_of`, `substitute`) covers it |
| `applicable_with`                                                        | Likely remove; use `is_invocable_type` / tuple inspection via reflection                   |
| `invoke_or_apply_result_t`                                               | Likely remove; reflection over `invoke_result` plus tuple spreading                        |
| `Construct`                                                              | Keep only if sequence/traverse need it                                                     |
| `Transform*` (`Concat`, `RangeToCheckNonEmpty`, `RangeToFrontElem`, ...) | Decide when filter/traverse land; likely remove                                            |
| `PairWith`                                                               | Decide when StateIO lands                                                                  |
| `mem_fn` / `attr` / `views`                                              | Keep only if used by a monad                                                               |

## Reference behaviour (from vulkanisedfelt, to preserve — not to port)

- **IO**: actions are zero-arg callables. `chain`/`transform`/`ap` produce a new IO. Results that
  are tuples are spread into the continuation with `std::apply`. `ap` forks the function IO and
  calls the value IO, then joins (concurrent, libfork); `sequence` is a fold of `ap` (the sync `ap`
  intentionally does not spread — it applies directly). An IO must not return an IO. Async results
  are detected by `unwrap_async`; sync-vs-async is chosen per step.
- **sequence / traverse / filter**: variadic `sequence` folds `ap`; range `sequence` forks each IO
  into a vector and rebuilds the container; `traverse` maps a kleisli then sequences (with an
  `std::optional` overload); `filter` likewise.
- **ReaderIO**: action is `state -> IO`. `bind` copies state, runs the source, then feeds the same
  state to the continuation's IO. `transform` is IO `fmap`; `ap` runs both with copies of state.
- **StateIO**: action is `state -> IO<pair<value, state>>`. `bind` runs the continuation on `.first`
  with `.second`. `ap` is sequential (state threading), unlike IO's concurrent `ap`.
- Takeaway: `bind`/`fmap`/`ap` become ordinary members or ADL functions on `IO<TAction, TTraits>`;
  sync-or-async detection, invocable-vs-apply dispatch and "is an IO" checks are done by reflection,
  with Traits validators giving readable messages instead of `static_assert(false, ...)`.

## Stage sequence (remaining)

Outline only — each stage's design is refined with the user when reached. Each stage is a green
increment: full build (all linters) + doctest + compile-fail suites pass before moving on.

a. **Finish IO (sync)** — only `fmap`/`transform` remain (`operator()` (run), `value`, `bind` and
sync `ap` are done). Reflection inspects action and result types; Traits validation covers
continuations (callable with the previous result or its spread tuple; must return an IO for `bind`).
Compile-fail test per rejection reason. b. **Async IO (add libfork)** — conanfile.txt += libfork;
`AsyncFunctorInterface`/`sync_wait`; concurrent `ap`. ASan/UBSan check for libfork concurrency.
(CMakeLists.txt already carries `-Wno-comma-subscript` "used by libfork".) c. **sequence / traverse
/ filter** — via reflection over pack and range types (no `Unspecialise`, no range-v3), including
the `std::optional` traverse overload. d. **ReaderIO** — `state -> IO`; reusable lift overloads for
IO/ReaderIO/plain value. e. **StateIO** — `state -> IO<pair>` with sequential `ap`;
then/store/fanout/split/traverse. f. **Reflection-derived env accessors** — generate reader/state
accessors from a struct. No longer a "showcase", as reflection is core. gcc 16.2 gotchas:
`-freflection` + `-std=c++26` required; `template for` over `members_of()` vectors is currently
BROKEN (constexpr-allocation lowering bug) — use indexed/pack access + `identifier_of`. Gate on
`__cpp_impl_reflection`. g. **Usage example + docs** — slim generic (no Vulkan) doctest-driven
example of IO → ReaderIO → StateIO pipelines; seeds doxygen examples. Draw idioms (not Vulkan code)
from vulkanisedfelt's `src/setup/monadic.hpp`. h. **Utility cleanup** — go through the audit table;
remove every legacy utility (with its tests and compile-fail probes) that no monad uses.

## Gotchas and working agreements

- Reflection docs: https://cppreference.com/cpp/meta/reflection — read these in preference to live
  compiler probes.
- Validation runs in the constructor, so compile-fail probes must construct the object.
- Default template arguments appear only on the first declaration (`fwd.hpp`).
- Source-repo issues to avoid when re-deriving behaviour: vulkanisedfelt's `readerio/sequence.hpp`
  range overload is unfinished (decide sequential vs concurrent semantics when reached and flag to
  the user); keep includes explicit rather than relying on ADL/umbrella coupling.
- Never suppress linters except as a last resort; auto-format with the configured tools.
- Verify behaviour rather than assume; confirm CMake property interactions empirically.
- Commits: conventional-commit titles ≤50 chars, body ≤72 cols, `Signed-off-by:` trailer required
  (gitlint enforces it).
- conan deps: add to conanfile.txt `[requires]`, then
  `conan install . -of build/Debug --build=missing -s "&:build_type=Debug" -pr:a build_env/nixos.linux.conan.profile`.
