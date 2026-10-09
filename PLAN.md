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
    named via a `char const *` NTTP with static storage duration, defaulted in `fwd.hpp` to
    `"IO"_ss`) with `static consteval std::string_view validate_action(std::meta::info)`; it returns
    an empty view when valid, else a short reason (e.g. `"does not return a value"`). `IO`'s traits
    also expose `validate_and_then(std::meta::info io_meta, std::meta::info continuation_meta)`
    (overridable by custom traits; the default implementation holds the full validation logic),
    while the reflection helper predicates it uses (`is_io`, `is_directly_invocable`, ...) remain
    free `detail` functions.
  - `<X><TAction, TTraits = <X>Traits>` runs the check in a `consteval {}` block with a
    `static_assert` whose message is built by `construct_type_error_msg`
    (`fnfelt/detail/errors.hpp`).
  - `fwd.hpp` forward-declares the class **with** the default traits argument (the default cannot be
    repeated in the definition), the traits struct (with its default `name_cstr`), and the free
    `and_then` functions (the workhorse's `TTraits` default lives here too).
  - Tests: doctest `static_assert`s on the traits validators covering every rejection reason (valid
    and invalid types); a test that the default traits are `<X>Traits`; a single compile-fail probe
    per traits injection point, using a stub validator that rejects everything (per AGENTS.md
    "single compile_fail test with a stub validator"). Validation runs in a class-scope
    `consteval {}` block, so it triggers on ANY instantiation of the type — compile-fail probes need
    only instantiate the type (constructing also works), not just use `sizeof`.
- **Namespace**: `fnfelt::` for shared utilities; monads live in `fnfelt::monad::<name>` (e.g.
  `fnfelt::monad::io`), implementation details in a nested `detail`.
- **Naming** (std-aligned; `.clang-tidy` is authoritative but clang-tidy is currently disabled for
  C++26 reflection, so apply by hand — see AGENTS.md "Naming"): concepts `lower_case`; classes,
  structs and enums `CamelCase`; namespace-scope computed aliases `lower_case_t`; member aliases
  `lower_case` with no `_t`; type template params `CamelCase` prefixed `T`; value template params
  `lower_case`; functions, variables and parameters `lower_case`; macros `UPPER_CASE`.
- **Operation naming is std-aligned**: `bind` → `and_then` (continuation → IO), `fmap` → `transform`
  (fn → plain value, re-wrapped), `ap` unchanged; error-message vocabulary uses "continuation", not
  "kleisli".
- **No Hana, no immer, no range-v3, no Boost.** Use `std::ranges`. libfork is **kept** (async
  runtime) and is introduced only when the async IO stage lands.
- **Test layout** (per AGENTS.md): tests live in `src/`, mirror the `include/` path, and are wrapped
  in `#ifndef DOCTEST_CONFIG_DISABLE` / `#endif // DOCTEST_CONFIG_DISABLE`. Compile-fail tests live
  under `tests/compile_fail/` with a subdirectory per source file (e.g. `monad/io/IO/`), registered
  in that directory's `CMakeLists.txt` with `.`-delimited ctest names (e.g.
  `monad.io.IO.custom_traits`). Per-TU split for the IO monad, mirroring `include/`: `detail.hpp`
  (generic reflection helpers) is tested in `src/monad/io/detail.cpp`, `traits.hpp` (`IOTraits`) in
  `src/monad/io/traits.cpp`, `IO.hpp` (construction, run) in `src/monad/io/IO.cpp`, `create.hpp` in
  `src/monad/io/create.cpp`, `and_then.hpp` in `src/monad/io/and_then.cpp`, `ap.hpp` in
  `src/monad/io/ap.cpp`, and the umbrella `io.hpp` in `src/monad/io.cpp`.
- **Compile-fail harness**: `fnfelt_test_compile_fail(<name> SOURCE <f> ERROR_REGEX <re>)` in
  `tests/compile_fail/CMakeLists.txt` (gated by `PASS_REGULAR_EXPRESSION` only — `WILL_FAIL` inverts
  regex-match passes; probes are `RESOURCE_LOCK`-serialized to avoid build-dir races under
  `ctest -j`).
- **Conventions on every file**: copyright header; `@file` doxygen; `#pragma once`; javadoc-style
  docs without `@brief` (doxygen warnings are errors over `include/`); clang-format, cmake-lint,
  cmake-format, doxygen and mdformat all green (they gate the build with `fnfelt_ENABLE_DEV=ON`).
  Public headers are listed in the CMake FILE_SET.

## What's done

### Stage R: IO from reflection

- `include/fnfelt/monad/io/IO.hpp` — `IO<TAction, TTraits = IOTraits>` holding an action; exposes
  `action`, `traits`, `value_type` and `name` aliases. Validation runs in a class-scope
  `consteval {}` block, i.e. on ANY instantiation of the IO type (including use as a return type),
  which is why compile-fail probes need only instantiate (constructing is not required).
- `include/fnfelt/monad/io/detail.hpp` — IO implementation details (mirroring the reference repo's
  `src/monad/detail.hpp` layout): the reflection helper predicates (`is_io`,
  `all_template_arguments_are_types`, `action_value_meta`, `io_action_meta`, `io_value_meta`,
  `is_directly_invocable`, `is_spread_invocable_as`, `is_spread_invocable`, `fundamental_type_name`,
  `short_type_name`, `maybe_io_name`). The and_then and ap operations live in their own
  `and_then.hpp`/`ap.hpp` (with `detail::AndThenError`/`AndThenAction` and
  `detail::ApError`/`ApAction` respectively), tested in `src/monad/io/and_then.cpp` and
  `src/monad/io/ap.cpp`; `detail.hpp` holds only the generic reflection helpers. `IO.hpp` holds only
  the `IO` class; the `create` factory lives in `include/fnfelt/monad/io/create.hpp` (tested in
  `src/monad/io/create.cpp`).
- `include/fnfelt/monad/io/traits.hpp` — the public default traits `IOTraits` in `fnfelt::monad::io`
  (named via its `name_cstr` NTTP, defaulted in `fwd.hpp` to `"IO"_ss`), a user extension point:
  name an IO (e.g. `IOTraits<"FileIO"_ss>`) or override with custom traits. Holds the action +
  continuation validators.
- `IOTraits::validate_action(std::meta::info)` — rejects, with these reasons (all prefixed "provided
  action ..."):
  - `is not a complete type`
  - `must not be a reference type`
  - `is already an IO, pass its action instead`
  - `is not a class or function pointer type`
  - `must be move constructible`
  - `is not callable with no arguments`
  - `is only callable when non-const`
  - `does not return a value` (void result)
  - `returns a reference, return by value instead`
  - `should not return an IO` (checked with `detail::is_io`)
  - `returns a non-movable type`
- `include/fnfelt/monad/io/fwd.hpp` — forward declaration with the default traits argument.
- `include/fnfelt/detail/errors.hpp` — `construct_type_error_msg`.
- Tests: `src/monad/io/IO.cpp` holds the IO-class tests (construction, run, `value_type`,
  default-traits assertion); `src/monad/io/traits.cpp` holds the `IOTraits` tests (validators,
  naming); `src/monad/io/detail.cpp` holds the `detail.hpp` tests (reflection helpers) - all
  mirroring the `include/` path. Compile-fail is a single probe, `monad.io.IO.custom_traits` (stub
  validator; the default validator's reasons are covered by doctest `static_assert`s).
- IO has **no dependency** on any shared utility beyond `fnfelt/detail/errors.hpp`,
  `fnfelt/static_string.hpp` and the macro guards.
- IO now has **`operator()` (run)**, the **`value_type`** alias and **`and_then`**:
  - `operator()` runs the wrapped action and returns its value (run exposed as the call operator,
    not a named `run` member).
  - `value_type` splices the action's invocation result (`detail::action_value_meta`, `void` for
    incomplete/non-callable actions).
  - `and_then` runs the source, applies the continuation to its value, then runs the continuation's
    IO. It exists as free functions (`and_then(io, continuation)`, with named and explicit-traits
    overloads) declared in `fwd.hpp` (so they precede the class) and defined in `and_then.hpp`, plus
    `IO::and_then` member overloads delegating to them. Dispatch is
    direct-invocation-else-tuple-spread: a continuation taking the whole value wins; otherwise
    values that are specialisations with all-type template arguments (e.g. `std::pair`,
    `std::tuple`) are spread with `std::apply`.
  - Continuation validation is an `IOTraits::validate_and_then(io_meta, continuation_meta)` method —
    the single injection point used by `and_then` (default implementation holds the full logic; the
    reflection helper predicates remain free `detail` functions). It rejects, with these reasons:
    `source IO is not an IO`, `source IO produces no value`,
    `continuation function is not a complete type`,
    `continuation function must not be a reference type` (validator-only),
    `continuation function is not a class or function pointer type`,
    `continuation function must be move constructible`,
    `continuation function does not accept the source IO's value`,
    `continuation function must return an IO`. The continuation parameter is taken by forwarding
    reference and decayed before validation/storage (lvalues are copied, rvalues moved), so the
    `must not be a reference type` reason is validator-only — it is unreachable through `and_then`.
    The source IO is likewise taken by forwarding reference: rvalue chains move one whole IO per
    hop, while an lvalue source is copied. The result IO's traits default to `IOTraits<>` (the
    source IO's traits are not inherited). The default validator's return values are covered by
    doctest `static_assert`s; the traits injection is covered by compile-fail probes
    `monad.io.and_then.custom_traits` and `monad.io.and_then.not_io_custom_traits` (a non-IO source
    through the workhorse), both using stub validators returning a fixed custom reason (per
    AGENTS.md "single compile_fail test with a stub validator"). AndThen tests live in
    `src/monad/io/and_then.cpp`; probes under `tests/compile_fail/monad/io/and_then/`.
  - Design decisions: `IOTraits::is_io` delegates to a free `detail::is_io` helper so any-traits IOs
    are recognised (one source of truth; `template_of(spec) == ^^IO` compares template, not full
    type); an invalid and_then is a `static_assert` plus a `detail::AndThenError` tombstone return
    (the `if constexpr` guard prevents cascade errors and yields exactly one friendly message).
  - The free `and_then` has exactly two overloads: the unconstrained workhorse
    (`and_then<TTraits>(source, continuation)`, `TTraits` defaulting to `IOTraits<>` via the
    `fwd.hpp` declaration) and the named form (`and_then<"name"_ss>(source, continuation)`, using
    `IOTraits<name_cstr>`). There are no non-IO catch-alls: a non-IO source resolves to the
    workhorse, where `validate_and_then` rejects it (`"source IO is not an IO"`) with the friendly
    static_assert and a `detail::AndThenError` tombstone, so the return type stays well-formed (e.g.
    when deduced by `auto`). Non-IO diagnostics render the offending type via the workhorse's alias
    (`source_io_type {aka int}`), so the non-IO probe pins the error regex only up to the stable
    prefix ending after the reason.
  - The **`create`** free function factory wraps a callable action in an IO, perfect-forwarding it
    (lvalues are copied, rvalues moved, function lvalues decay to function pointers). It has two
    overloads, keyed on the leading template argument: a name (`create<"My IO"_ss>(action)`,
    yielding `IOTraits<name>`), or a traits type (`create<MyTraits>(action)`, defaulting to
    `IOTraits`). A free function template is used because class CTAD cannot take an explicit leading
    `IOTraits` argument while deducing the rest — the function template is the workaround. It lives
    in `include/fnfelt/monad/io/create.hpp`, tested in `src/monad/io/create.cpp`. It has no
    compile-fail probe of its own: `create` forwards to `IO`, whose validation the
    `monad.io.IO.custom_traits` probe already covers.
  - The IO pipeline is fully **`constexpr`**: `IO`'s constructor, run (`operator()`) and `and_then`,
    plus `detail::AndThenAction`'s and `detail::ApAction`'s call operators, are all `constexpr`, so
    IOs (including and_then chains) run in constant-evaluated contexts.
- IO now has **`ap`** — the free function `fnfelt::monad::io::ap(fn_io, value_io)` (sync for now;
  the concurrent libfork version is deferred to stage b):
  - Both IOs are taken by forwarding reference (each forwarded independently), so rvalue IOs move
    (rvalue chains move one whole IO per hop) and lvalue IOs are copied; it runs the function IO
    then the value IO and applies the callable. Like `and_then`, the callable is invoked with the
    value directly when possible, otherwise the value is spread with `std::apply` (direct invocation
    wins over spreading).
  - Result traits default to `IOTraits<>` (not inherited from the argument IOs); named and
    explicit-traits overloads exist, as for `and_then`.
  - Invalid arguments yield a friendly `static_assert` whose message mirrors `and_then`'s structured
    form: `fnfelt: IO ap error: <name>{(FnIO()(ValueIO()) => Result)}: <reason>: <offending type>`,
    built by `detail::ap_error_msg`, plus a `detail::ApError` tombstone return. The general
    `maybe_io_name` helper lives in `detail.hpp`; `maybe_ap_result_name` (the application's result,
    or `"<unknown>"`) and `ap_result_name` live in `ap.hpp`. Result types render via
    `detail::short_type_name` — kind-based short names (identifier / outer template name / "lambda"
    / canonical fundamental spelling / "value"), no string truncation.
  - `ApError`/`ApAction<TFnIO, TValueIO>` live in `include/fnfelt/monad/io/ap.hpp` (the reflection
    helpers `io_action_meta`/`io_value_meta` remain in `detail.hpp`; the latter via
    `action_value_meta`; both return `^^void` for non-IO types to avoid consteval throws).
  - `IOTraits` validator: `validate_ap(fn_io_meta, value_io_meta)` — kind checks
    (`"IO-wrapped function is not an IO"`, `"IO-wrapped value is not an IO"`), then compatibility
    (`"IO-wrapped function's callable does not accept the IO-wrapped value's value"`), then void
    result (`"IO-wrapped function's callable does not return a value"`).
  - The free `ap` has exactly two overloads (the unconstrained workhorse with `TTraits` defaulting
    to `IOTraits<>`, and the named `ap<"name"_ss>(...)` form), mirroring `and_then`. There are no
    non-IO catch-alls: a non-IO argument resolves to the workhorse, where `validate_ap` rejects it
    with the same structured static_assert, naming whichever argument is not an IO as the offending
    type, rendered via the workhorse's alias (e.g. `fn_io_type {aka int}` — so the non-IO probe pins
    the error regex only up to the stable prefix ending after the reason), and a `detail::ApError`
    tombstone return.
  - Tests: doctest `static_assert`s in `src/monad/io/traits.cpp`; end-to-end/constexpr/spread/
    traits-propagation/move-tracking tests and the `ap_error_msg` composition doctest in
    `src/monad/io/ap.cpp`; compile-fail probes `monad.io.ap.custom_traits` and
    `monad.io.ap.fn_not_io_custom_traits` under `tests/compile_fail/monad/io/ap/`.
- IO now has **`transform`** (fmap) — the free function
  `fnfelt::monad::io::transform(source_io, transformer)` (sync, mirroring `and_then`/`ap`):
  - The returned IO runs the source IO, applies the transformer to its value, and produces the
    transformer's PLAIN value (unlike `and_then`, the result is not run as an IO). The transformer
    is invoked with the value directly when possible, otherwise the value is spread with
    `std::apply` (direct invocation wins over spreading), exactly like `and_then`.
  - The source IO and transformer are taken by forwarding reference (each forwarded independently),
    so rvalue sources move (rvalue chains move one whole IO per hop) and lvalue sources are copied;
    the transformer is decayed (lvalues copied, rvalues moved). Result traits default to
    `IOTraits<>` (not inherited from the source IO); named and explicit-traits overloads exist, as
    for `and_then`.
  - `TransformError`/`TransformAction<TSourceIO, TTransformer>` live in
    `include/fnfelt/monad/io/transform.hpp` (the action returns the transformer's result directly;
    `transform_result_name` names the result, favouring an IO's name, and
    `maybe_transformer_result_name` dispatches direct-then-spread, yielding `"<unknown>"` for
    undeterminable parts).
  - `IOTraits::validate_transform(io_meta, transformer_meta)` is the single injection point used by
    `transform` (default implementation holds the full logic). It rejects, with these reasons:
    `source IO is not an IO`, `source IO produces no value`,
    `transformer function is not a complete type`,
    `transformer function must not be a reference type` (validator-only),
    `transformer function is not a class or function pointer type`,
    `transformer function must be move constructible`,
    `transformer function does not accept the source IO's value`,
    `transformer function does not return a value`,
    `transformer function returns a reference, return by value instead`,
    `transformer function should not return an IO`,
    `transformer function returns a non-movable type`. The transformer parameter is taken by
    forwarding reference and decayed before validation/storage, so the
    `must not be a reference type` reason is validator-only — it is unreachable through `transform`.
  - Invalid arguments yield a friendly `static_assert` whose message mirrors `and_then`'s structured
    form: `fnfelt: IO transform error: <name>{(SourceIO()) => Result}: <reason>: <offending type>`,
    built by `detail::transform_error_msg`, plus a `detail::TransformError` tombstone return. The
    free `transform` has exactly two overloads (the workhorse with `TTraits` defaulting to
    `IOTraits<>`, declared in `fwd.hpp`, and the named `transform<"name"_ss>(...)` form); non-IO
    sources resolve to the workhorse, where `validate_transform` rejects them.
  - Tests: doctest `static_assert`s in `src/monad/io/traits.cpp`; end-to-end/direct-vs-spread/
    precedence/constexpr/composition/move-tracking tests, the `transform_error_msg` and
    `maybe_transformer_result_name` doctests in `src/monad/io/transform.cpp`; compile-fail probes
    `monad.io.transform.custom_traits` and `monad.io.transform.not_io_custom_traits` under
    `tests/compile_fail/monad/io/transform/`.
- **Umbrella header**: `include/fnfelt/monad/io.hpp` is the public interface. It includes the eight
  `io/` sub-headers (`IO`, `and_then`, `ap`, `create`, `detail`, `fwd`, `traits`, `transform`), each
  marked `// IWYU pragma: export`; each sub-header carries
  `// IWYU pragma: private, include "../io.hpp"`. The sub-headers remain individually
  includable/standalone (`and_then.hpp`/`ap.hpp`/`create.hpp`/`transform.hpp` include `IO.hpp`). The
  umbrella has its own doctest TU, `src/monad/io.cpp`, locking its completeness
  (create/member-and_then/free-and_then/member-transform/free-transform/ap end-to-end and in a
  `constexpr` pipeline).

## Reference behaviour (from vulkanisedfelt, to preserve — not to port)

Terminology below is the reference repo's own (the reference's `bind` is fnfelt's `and_then`; its
`transform` will be fnfelt's `transform`).

- **IO**: actions are zero-arg callables. `chain`/`transform`/`ap` produce a new IO. Results that
  are tuples are spread into the continuation with `std::apply`. `ap` forks the function IO and
  calls the value IO, then joins (concurrent, libfork); `sequence` is a fold of `ap` (the reference
  sync `ap` applies directly, but fnfelt's sync `ap` deliberately deviates and spreads like `bind`:
  direct invocation when possible, otherwise `std::apply`, direct wins). An IO must not return an
  IO. Async results are detected by `unwrap_async`; sync-vs-async is chosen per step.
- **sequence / traverse / filter**: variadic `sequence` folds `ap`; range `sequence` forks each IO
  into a vector and rebuilds the container; `traverse` maps a continuation then sequences (with an
  `std::optional` overload); `filter` likewise.
- **ReaderIO**: action is `state -> IO`. `bind` copies state, runs the source, then feeds the same
  state to the continuation's IO. `transform` is IO `fmap`; `ap` runs both with copies of state.
- **StateIO**: action is `state -> IO<pair<value, state>>`. `bind` runs the continuation on `.first`
  with `.second`. `ap` is sequential (state threading), unlike IO's concurrent `ap`.
- Takeaway: `bind`/`fmap`/`ap` become ordinary members or ADL functions on `IO<TAction, TTraits>`;
  sync-or-async detection, invocable-vs-apply dispatch and "is an IO" checks are done by reflection,
  with Traits validators giving readable messages instead of `static_assert(false, ...)`.

## Async IO design (stage b)

Dual-natured run proxy, finalized from the completed de-risk probes in a `/tmp` worktree
(`probe.cpp` for the sync/mock-reflection path, `probes/probe_san.cpp` for the libfork+reflection
path). The reviewer checks the implementation against this section.

### Probed foundations

- libfork 3.8.0 header-only, Conan-cached, builds/runs clean under gcc 16.2 / C++26 / `-freflection`
  / `-Wall -Wextra -Werror`; `find_package(libfork REQUIRED)` + link gives an `-isystem` include, so
  **no warning suppressions**. `-Wno-comma-subscript` no longer fires at C++26 (kept, harmless).
- **CRITICAL**: stateful/capturing async fns dangle (`stack-use-after-return` in `lf::sync_wait`,
  ASan-confirmed). Statelessness is enforced by a reflection empty-closure check
  (`nonstatic_data_members_of` walking `bases_of`).
- **`consteval` survival** verified: `io().sync_wait()` `static_assert`s on 3-deep fully-sync
  chains; mixed chains compile via a per-level `if constexpr` split; scheduler args are
  accepted-and-ignored on sync proxies (unnamed `auto&&...` pack avoids `-Werror=unused-parameter`);
  a class-scope `consteval` validation block + proxy `value_type` splice works.
- **Reflection details**: alias extraction uses `dealias(member)`, **not** `type_of` (throws for
  aliases); guard `is_complete_type` + `is_class_type` before `is_base_of_type` (incomplete derived
  type is a hard gcc error); `members_of` omits inherited members → walk `bases_of`; define
  tag/proxy machinery in `fwd.hpp` so it precedes `detail.hpp`/`IO.hpp` and the class-scope value
  splice sees the tag at instantiation (avoids `-Werror=sfinae-incomplete`).
- `lf::eventually<T>` + fork/join verified for concurrency; `lf::just` handles plain callables
  (incl. static `constexpr` stateless `lf::task` lambdas, the vulkanisedfelt pattern); `lf::lift`
  bridges plain callables; `<meta>` + libfork coexist in one TU.

### Design

- **Dual-natured `RunProxy`**: `io().sync_wait()` is the ONLY execution path. `IO::operator()`
  returns a `RunProxy` holding the action BY VALUE (moved from rvalues, copied from const lvalues;
  `[[nodiscard]]` is an intentional break for previously-meaningful `io();`).
  - sync branch accepts-and-ignores scheduler packs (unnamed `auto&&...`); async branch dispatch:
    empty pack → `lf::sync_wait(lf::lazy_pool{}, fn, arg)`; exactly one `lf::scheduler` → forward
    it; else readable `static_assert`.
  - returns the plain `value` type `T` on both paths (`lf::sync_wait` unwraps to plain `T`, not
    `optional`/`tuple`).
- **Async execution model**: ONLY the top-level `sync_wait` calls `lf::sync_wait`; composed proxies'
  coroutine fns `co_await` children via `lf::just` (and `lf::fork[&slot, fn]` + `lf::join` for `ap`)
  — never recursive sub-`sync_wait`. The scheduler enters only at the top and services the whole
  tree; one `if constexpr` per composition level. Exceptions propagate on both paths (async via
  `future.get()` rethrow).
- **`create_async<name_cstr>(fn, args...)`**: `fn` is a VALUE of an empty (stateless) callable type
  (`TFn` deduced); `AsyncLeafProxy<TFn, TArgTuple>` stores the arg tuple and has no stored `fn`
  member (`TFn{}` is instantiated on invocation). Composed (`and_then`/`ap`) proxies carry
  fnfelt-generated `static constexpr` stateless `lf::task` driver lambdas, all state in the arg
  tuple; a capturing lambda fails statelessness validation, so the dangle is unrepresentable.
- **`co_run` factoring**: one shared "run a child IO (inline if sync, `co_await lf::just` if async)"
  helper used by both drivers, so `and_then`'s async arm is ~2 lines delegating into `async.hpp`.
- **Header layering**: async arms of `AndThenAction`/`ApAction` are defined out-of-line in
  `async.hpp`, so `and_then.hpp`/`ap.hpp` keep zero libfork includes and sync headers never mention
  coroutines (sync `and_then` stays simple/`constexpr` as today). `AsyncProxyTag` + `RunProxy`
  fwd-decl live in `io/fwd.hpp`; reflection helpers (`action_is_async`, extended
  `action_value_meta`, `is_empty_object`) in `io/detail.hpp`; `RunProxy`/`create_async`/async arms
  in new `io/async.hpp`, added to the umbrella `io.hpp` with `// IWYU pragma: export` and its
  `FILE_SET`.
- **Naming** per `.clang-tidy`: `AsyncProxyTag` (CamelCase); the proxy's nested alias is
  `value_type` (lower_case member alias disambiguated with a `_type` suffix per the repo naming
  convention, matching `IO::value_type`) — still a deliberate deviation from vulkanisedfelt's
  `async_function_tag_t`/`IOResultType`.

### Validation

- `validate_action` extended to accept tag-deriving proxies exposing a `value_type` alias; reject
  async-void with a readable reason ("async IO actions must not produce void"; the reference's
  `co_return *value` shape is void-incompatible). Leaf statelessness is enforced at `create_async`
  (an empty-closure check on the deduced `TFn`), while composed proxies' `fn` members are always
  fnfelt-generated `static constexpr` stateless lambdas, so the emptiness check only guards
  self-generated members.
- `validate_and_then` / `validate_ap` use the extended `action_value_meta`, so continuation/ap
  checks work transparently against unwrapped async values.

### Stages A-E

Each a separate green commit. `FILE_SET`/umbrella updated in the stage that creates each header.

- **A — plumbing — COMPLETE**: `conanfile.txt` += `libfork/3.8.0`; `find_package`; link
  `libfork::libfork` to `fnfelt.lib` with `INTERFACE` (**not** `PUBLIC` — configure error on the
  header-only INTERFACE lib); re-run `conan install .`. Full build + `ctest` green with the dep in
  place.
- **B — `RunProxy` core — COMPLETE**: `fwd.hpp` gains `AsyncProxyTag` + `RunProxy` fwd-decl;
  `detail.hpp` gains helpers; `IO::operator()` migrates to return `RunProxy`; ALL existing tests →
  `io().sync_wait()`; detection-only mock-proxy tests (incl. a void-`value_type` variant) and a
  copy-counting doctest; update `IO.hpp`'s doc comment. `consteval` tests still pass as `constexpr`;
  async branch stubbed.
- **C — async leaves + and_then**: `create_async`; statelessness enforcement + compile-fails
  (stateful `fn`, missing `value_type` alias) HERE; async leaf proxies (arg tuple, no stored `fn`);
  async/mixed `AndThenAction`; mixed-chain and sync-over-async / async-over-sync tests;
  `std::unique_ptr<int>` through an and_then with a not-copy-constructible `static_assert`; a
  `std::pair<int,int>` spread-on-async test; ASan/UBSan via `-Dfnfelt_ENABLE_SANITIZER_ASAN=ON`.
- **D — concurrent ap**: async `ApAction` via fork both sides + join; tests for mixed
  async-fn×sync-value / sync-fn×async-value (likeliest bug spot); `lf::lazy_pool{2}` + atomic
  rendezvous concurrency proof; move-only-callable `ap`; deep mixed `ap`-in-`and_then`-in-`ap`
  pipeline; ap-composed spread variant; ASan/UBSan.
- **E — polish**: continuation-non-IO-with-async-upstream compile-fail + stub-validator positive
  coverage; docstrings; formatters; final `FILE_SET`; README/AGENTS notes if the API changed
  (AGENTS.md only if conventions change — likely not).

### Acceptance criteria

1. `io().sync_wait()` is the only execution path; `io()` alone yields a `[[nodiscard]]` `RunProxy`.
2. Existing `constexpr` pipeline tests still `static_assert` (`consteval` survival).
3. Async leaf + and_then + ap pipelines produce the correct plain `value` type `T` under
   `lf::lazy_pool` at runtime.
4. `ap` runs its two sub-IOs concurrently on the async path, witnessed with `lf::lazy_pool{2}` and
   an atomic rendezvous.
5. ASan/UBSan clean on the async tests via `-Dfnfelt_ENABLE_SANITIZER_ASAN=ON`.
6. Compile-fail tests produce the single readable reflection-built error message per fnfelt
   conventions.
7. All linters + full `ctest` green.

### Risks

- Stateful async fn dangles → stateless via reflection (empty-closure walking `bases_of`) +
  compile-fail.
- `-Wsfinae-incomplete` if detection runs on incomplete proxies → tag/proxy machinery in a low-level
  header with ordered includes; guard with `is_complete_type` (returns `false`, not an error).
- `members_of` omits inherited `value_type` → walk `bases_of` (probe-verified).
- `type_of` on an alias throws → use `dealias` (probe-verified).
- `constexpr` + coroutine lambda quirks → only stateless `static constexpr` lambdas on the async
  path (probe-verified).
- libfork umbrella include pulls deprecated `scan.hpp` warnings → use narrow includes
  (`libfork/core/{task,sync_wait,eventually,just}.hpp`, `libfork/schedule/lazy_pool.hpp`, plus
  `libfork/algorithm/lift.hpp` only if `lift` ends up needed).

## Stage sequence (remaining)

Outline only — each stage's design is refined with the user when reached. Each stage is a green
increment: full build (all linters) + doctest + compile-fail suites pass before moving on.

a. **Finish IO (sync) — COMPLETE**: `operator()` (run), `value_type`, `and_then`, `transform` (fmap)
and sync `ap` are all done. Reflection inspects action and result types; traits validation covers
continuations (callable with the previous result or its spread tuple; must return an IO for
`and_then`) and transformers (same direct-then-spread dispatch; must return a plain value for
`transform`). Rejection reasons are covered by doctest `static_assert`s on the traits validators,
with a single stub-validator compile-fail probe per injection point.

- **Future direction, not now**: the traits/named split could collapse further by riding names on a
  config type (`IOTraits<name>` already carries them; `std::meta::info` is a valid NTTP on gcc 16.2)
  — settle before `sequence`/`traverse` clone the pattern.

b. **Async IO (add libfork)** — dual-natured run proxy: `io().sync_wait()` the only execution path,
while sync pipelines stay `consteval`; libfork 3.8.0; concurrent `ap` on the async path. De-risked
via probes in a `/tmp` worktree. (design finalized — see "Async IO design (stage b)") c. **sequence
/ traverse / filter** — via reflection over pack and range types (no range-v3), including the
`std::optional` traverse overload. d. **ReaderIO** — `state -> IO`; reusable lift overloads for
IO/ReaderIO/plain value. e. **StateIO** — `state -> IO<pair>` with sequential `ap`;
then/store/fanout/split/traverse. f. **Reflection-derived env accessors** — generate reader/state
accessors from a struct. No longer a "showcase", as reflection is core. gcc 16.2 gotchas:
`-freflection` + `-std=c++26` required; `template for` over `members_of()` vectors is currently
BROKEN (constexpr-allocation lowering bug) — use indexed/pack access + `identifier_of`. Gate on
`__cpp_impl_reflection`. g. **Usage example + docs** — slim generic (no Vulkan) doctest-driven
example of IO → ReaderIO → StateIO pipelines; seeds doxygen examples. Draw idioms (not Vulkan code)
from vulkanisedfelt's `src/setup/monadic.hpp`.

## Gotchas and working agreements

- Reflection docs: https://cppreference.com/cpp/meta/reflection — read these in preference to live
  compiler probes.
- Validation runs in a class-scope `consteval {}` block, so it triggers on ANY instantiation of the
  type — compile-fail probes need only instantiate the type (constructing also works).
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
