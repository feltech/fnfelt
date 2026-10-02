---
name: cpp26-reflection
description: >-
    C++26 reflection (P2996) on gcc 16 with -freflection, the <meta> header, std::meta::info and the
    ^^ reflection operator, compile-time type validation, reflection compile errors, and the
    verified behaviour of invocability/member queries on libstdc++ 16.2. Use when working with code
    that uses C++26 reflection.
license: MIT
---

# C++26 reflection (P2996) on gcc 16

Verified against gcc 16.2.0 / libstdc++ 16.2.0. Everything below was compiled and run; corrections
to common assumptions are flagged inline.

## Toolchain facts

- Compile with `g++ -std=c++26 -freflection`. Without `-freflection` you get
  `error: reflection is only available with '-freflection'`.
- Feature macros: `__cpp_impl_reflection == 202603`, `__cpp_lib_reflection == 202603`.
- Header: `<meta>`. The opaque reflection handle is `std::meta::info`
  (`using info = decltype(^^int);`) and `^^` is the reflection operator.
- Authoritative header on this toolchain (do not hardcode; resolves via the compiler):
  `$(g++ -print-file-name=include)` gives the gcc internal include dir; the libstdc++ headers live
  at `<that>/../../include/c++/<version>/meta` — i.e.
  `$(dirname $(dirname $(g++ -print-file-name=include)))/include/c++/*/meta`.
- cppreference reflection index: <https://en.cppreference.com/w/cpp/meta/reflection> (per-function
  pages beneath it).
- clangd/LSP does not know gcc-16 `<meta>`; expect bogus editor errors. Trust `g++` only.
- Reflection expressions (`^^T`, queries) are consteval-only. Outside a constant-evaluated context
  you get `consteval-only expressions are only allowed in a constant-evaluated context` or
  `consteval-only variable ... not declared constexpr`. Do reflection work inside
  `consteval`/`constexpr` functions, `static_assert`, or template arguments.

## When to use / mental model

- A reflection is a *value* of type `std::meta::info`, produced by `^^entity`. `^^T` reflects a
  type; `^^decltype(x)` reflects the type of an expression.
- `^^var` reflects the **variable itself** (an entity reflection: `is_variable` true, `is_type`
  false), not its type. Handing it to a type-requiring query throws; use `^^decltype(var)` or
  `std::meta::type_of(^^var)`.
- Compare reflections with `std::meta::is_same_type`, not `==`. **Verified correction**:
  `is_same_type` already sees through aliases (`is_same_type(^^Alias, ^^int)` is `true`), but
  `info ==` does not (`^^Alias == ^^int` is `false`). Call `dealias` before using `==`.
- Query results that are `vector<info>` allocate at compile time; results must be consumed inside a
  constant-evaluated context (see gotchas).

## Quickstart

```bash
g++ -std=c++26 -freflection main.cpp -o prog && ./prog
```

```cpp
#include <meta>

#include <string_view>

// A consteval validator returning a reason, or empty on success.
struct is_int_validator
{
  consteval std::string_view operator() (std::meta::info type) const
  {
    if (!std::meta::is_type (type))
      {
        return "reflection is not a type";
      }
    if (!std::meta::is_same_type (std::meta::dealias (type), ^^int))
      {
        return "type is not int";
      }
    return {};
  }
};

inline constexpr is_int_validator validate_is_int{};

// Inject the validator as an `auto` non-type template parameter so tests can
// stub it out.
template <class T, auto validator = validate_is_int>
struct checked
{
  static_assert (validator (^^T).empty (), "T failed validation");
  using type = T;
};

static_assert (sizeof (checked<int>::type) == sizeof (int));
```

## API quick reference

All queries are `consteval` and live in `std::meta` unless noted. Exact signatures for the most-used
ones (from the libstdc++ 16.2 `<meta>`):

```cpp
namespace std::meta {
  using info = decltype(^^int);

  consteval bool has_identifier(info);
  consteval string_view identifier_of(info);     // throws if !has_identifier
  consteval string_view display_string_of(info); // never throws

  consteval bool is_type(info);
  consteval bool is_complete_type(info);
  consteval info dealias(info);
  consteval info type_of(info);                 // entity -> its type

  template<reflection_range Rg = initializer_list<info>>
    consteval bool is_invocable_type(info, Rg&&);   // NOTE: pass {} for zero args
  template<reflection_range Rg = initializer_list<info>>
    consteval bool is_invocable_r_type(info result, info type, Rg&&);
  template<reflection_range Rg = initializer_list<info>>
    consteval info invoke_result(info, Rg&&);      // throws if not invocable
  template<reflection_range Rg = initializer_list<info>>
    consteval bool is_nothrow_invocable_type(info, Rg&&);

  consteval bool is_same_type(info, info);
  consteval bool is_base_of_type(info base, info derived);
  consteval bool is_convertible_type(info from, info to);
  consteval info remove_cvref(info), decay(info);
  consteval info add_lvalue_reference(info);

  template<reflection_range Rg = initializer_list<info>>
    consteval bool can_substitute(info template_, Rg&& args);
  consteval bool has_template_arguments(info);
  consteval info template_of(info);
  consteval vector<info> template_arguments_of(info);

  consteval vector<info> members_of(info, access_context);
  consteval vector<info> nonstatic_data_members_of(info, access_context);
  consteval vector<info> static_data_members_of(info, access_context);
  consteval vector<info> bases_of(info, access_context);
  consteval vector<info> subobjects_of(info, access_context);
  consteval vector<info> enumerators_of(info);
}

namespace std {
  template<ranges::input_range Rg>
    consteval const ranges::range_value_t<Rg>* define_static_string(Rg&&);
  template<ranges::input_range Rg>
    consteval auto define_static_array(Rg&&);   // returns span
  template<class Tp>
    consteval const remove_cvref_t<Tp>* define_static_object(Tp&&);
}
```

### Names

`has_identifier`, `identifier_of` (throws `reflection with has_identifier false` for anonymous
entities — guard with `has_identifier`), `display_string_of` (safe fallback), `source_location_of`.

### Type category

`is_type`, `is_void_type`, `is_pointer_type`, `is_reference_type` (`is_lvalue_reference_type` /
`is_rvalue_reference_type`), `is_function_type`, `is_class_type`, `is_union_type`, `is_enum_type`,
`is_scoped_enum_type`, `is_object_type`, `is_complete_type`, `is_enumerable_type`, `is_function`,
`is_variable`, `is_nonstatic_data_member`, `is_static_member`, `is_type_alias`, `is_class_template`,
`is_function_template`, `is_value`, `is_object`, `is_namespace`. Note:
`is_type`/`is_variable`/`is_function`/`is_namespace` etc. never throw and return `false` for other
kinds; `is_complete_type` returns `false` (does not throw) for non-type reflections. Category
queries like `is_class_type` **throw** `std::meta::exception` ("reflection does not represent a
type") for non-type reflections.

### Relations / transformations

`is_same_type` (sees through aliases), `is_base_of_type`, `is_convertible_type`, `dealias`,
`type_of`, `remove_cvref`, `decay`, `add_lvalue_reference` / `add_rvalue_reference` /
`remove_reference`, `can_substitute` / `substitute`, `template_of`, `template_arguments_of`,
`has_template_arguments`.

### Member/scope queries

`members_of`, `nonstatic_data_members_of`, `static_data_members_of`, `bases_of`, `subobjects_of` all
take an `std::meta::access_context` second argument — **no default**. Use
`std::meta::access_context::unprivileged()`, `::current()`, or `unprivileged().via(^^Class)` (access
as if from `Class`; argument must be null or a complete class type reflection). `enumerators_of`
takes no access context.

### Invocation

`is_invocable_type(type, args...)`, `is_invocable_r_type(result, type, args...)`,
`invoke_result(type, args...)`, `is_nothrow_invocable_type`. The argument pack is a reflection
*range* (`initializer_list<info>` by default), where each element reflects an argument **type**.
**Verified**: the header has no default *function* argument for the pack, so one-argument calls like
`is_invocable_type(^^T)` fail to compile ("no matching function") — pass `{}` explicitly for zero
args: `is_invocable_type(^^T, {})`. The same applies to `invoke_result`, `is_invocable_r_type`,
`is_nothrow_invocable_type`.

### Static promotion (escaping consteval data)

- `std::define_static_string(range)` — promote a built string to static storage; returns
  `const char*` (or `const T*` for element type `T`).
- `std::define_static_array(range)` — promote a query result to a `span` you can iterate with
  `template for`.
- `std::define_static_object(value)` — promote any literal object.

### Exceptions

`std::meta::exception` derives from `std::exception`, is catchable inside consteval, and offers
`what()` (message), `from()` (the reflection that caused it), `where()` (`source_location`).

## Verified behaviour matrix (gcc 16.2)

Callable/shape validation with `is_invocable_type` / `invoke_result`, zero args (`{}`):

| type                                                    | `is_invocable_type(^^T,{})` | `invoke_result(^^T,{})`      | notes                                                                                                                                            |
| ------------------------------------------------------- | --------------------------- | ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| functor `int operator()() const`                        | true                        | `^^int`                      |                                                                                                                                                  |
| functor `void operator()() const`                       | true                        | `^^void`                     |                                                                                                                                                  |
| functor `int operator()(int) const`                     | false                       | throws "type member missing" |                                                                                                                                                  |
| non-callable struct                                     | false                       | throws "type member missing" |                                                                                                                                                  |
| functor `int operator()() &`                            | false                       | throws                       | prvalue receiver; `add_lvalue_reference(^^T)` gives true (verified) |
| functor `int operator()() &&`                           | true                        | `^^int`                      |                                                                                                                                                  |
| functor `int operator()() const &`                      | true                        | `^^int`                      |                                                                                                                                                  |
| mixed `int operator()() const` + `void operator()() &&` | true                        | `^^void`                     | overload-resolution quirk: `is_invocable_type` and `invoke_result` do not agree                                                                  |
| `int(*)()`                                              | true                        | `^^int`                      |                                                                                                                                                  |
| `void(*)()`                                             | true                        | `^^void`                     |                                                                                                                                                  |
| `int(*)(int)`                                           | false                       | throws                       |                                                                                                                                                  |
| function type `int()`                                   | true                        | `^^int`                      | function types cannot be stored as data members — reject before storing                                                                          |
| function type `void()`                                  | true                        | `^^void`                     | same caveat                                                                                                                                      |
| capturing lambda                                        | true                        | `^^int`                      | closure types are ordinary class types (`is_class_type` true); no `is_closure_type`/`is_lambda` exists anywhere in `<meta>`                      |
| non-capturing lambda                                    | true                        | `^^int`                      | same                                                                                                                                             |
| `void`                                                  | —                           | —                            | `is_complete_type` false, `is_object_type` false                                                                                                 |
| incomplete class                                        | —                           | —                            | `is_complete_type` false; passing to `is_invocable_type`/`invoke_result` is a **hard, non-catchable gcc error** — guard `is_complete_type` first |

The lvalue-ref-qualified row deserves emphasis: `is_invocable_type` uses prvalue/`INVOKE` semantics,
so `operator() &`-only callables are rejected for the bare type. **Verified correction to a common
assumption**: `is_invocable_type(add_lvalue_reference(^^T), {})` is **true** for an `&`-only
callable (it matches `INVOKE` on an lvalue receiver) — so if you store/invoke the object as an
lvalue, test the reference-qualified form rather than trusting the bare-type answer.

## Gotchas / hard-won findings

- **`members_of` includes implicitly-declared special members.** A 2-data-member struct yields 8
  members. Symptom: counting "fields" with `members_of` gives 8, not 2. Fix: use
  `nonstatic_data_members_of` (2), `subobjects_of` (2), `static_data_members_of`, `bases_of` as
  appropriate.
- **`vector<info>` query results allocate (`operator new`)**, so they cannot be bound to a
  namespace-scope `constexpr` variable ("is not a constant expression because it refers to a result
  of 'operator new'") and cannot be returned from a `consteval` function and used at runtime.
  Workarounds: (a) do the work inside one `consteval` call and reduce to scalars; (b) index inside
  consteval and extract only what you need; (c) `std::define_static_array(<query>)` then iterate
  with `template for` (verified working); (d) `std::define_static_string(...)` to promote a built
  string to static storage.
- **`template for` directly over `members_of(...)` or any constexpr vector FAILS** with the
  operator-new error. `template for (constexpr auto e : std::define_static_array(<query>))` WORKS.
- **`std::to_string` is not constexpr in libstdc++ 16.2** ("call to non-'constexpr' function"), and
  neither is `std::format` — avoid both in consteval. Build messages by `std::string +=` /
  `string_view` concatenation. A `std::string` built in consteval must not be returned and used at
  runtime (allocation): return a `string_view` into a string literal, or promote with
  `std::define_static_string`.
- **`identifier_of` throws** "reflection with has_identifier false" for anonymous entities (e.g.
  unnamed member struct types). Guard with `has_identifier`, or use `display_string_of`, which never
  throws.
- **Type-category queries throw for non-type reflections.** Symptom:
  `is_class_type(^^some_variable)` throws `std::meta::exception` "reflection does not represent a
  type" (verified; the same applies to handing a function or namespace reflection to a category
  query). Note `is_complete_type` does *not* throw — it returns false. Fix: guard with
  `is_type`/`is_function`/`is_variable` or use `type_of` first.
- **`invoke_result` throws for non-invocable types** ("type member missing"). Gate it behind
  `is_invocable_type`.
- **`is_invocable_type` uses prvalue/INVOKE semantics**: accepts `operator() &&`-only callables,
  rejects `operator() &`-only ones. If you store/invoke the object as an lvalue, also test
  `is_invocable_type(add_lvalue_reference(^^T), {})` — verified **true** for an `&`-only callable
  (an lvalue receiver satisfies the `&` qualifier), so this recovers the lvalue case.
- **`^^var` reflects the variable, not its type** (`is_variable` true, `is_value` false). Use
  `^^decltype(var)` or `type_of(^^var)`.
- **Alias reflections and `==`**: `^^Alias == ^^int` is false and `is_same_type(^^Alias, ^^int)` is
  true (verified; `is_same_type` sees through aliases). Call `dealias` before `==`.
- **Function types `int()` pass invocability but cannot be data members.** Add an explicit
  `is_function_type`/`is_object_type` shape check if you intend to store by value.
- **`is_invocable_type(^^T)` (no braces) fails to compile** — the range parameter has no default
  function argument. Always pass `{}` for zero args.
- **Incomplete types passed to invocability queries are a hard gcc error** ("type trait
  'std::meta::is_invocable_type\<>' preconditions not satisfied"), not a catchable exception. Always
  guard with `is_complete_type` first.

## Validation-error-message pattern

For compile-time type validation with readable errors:

1. Implement the validator as a **structural functor** with
   `consteval std::string_view operator()(std::meta::info) const` — empty means valid, non-empty is
   the human-readable reason.
2. Expose an `inline constexpr` instance, and inject the validator into templates as an `auto`
   non-type template parameter so tests can stub it (pass a trivial always-empty functor).
3. A free `consteval` function **can** also be passed on this toolchain (verified: both
   `checked<free_fn>` with `auto` NTTP and a plain function pointer NTTP compile and run on gcc
   16.2), but prefer the structural functor: it composes better as a default template argument
   (`auto validator = my_validator{}`) and avoids surprises on other compilers.
4. Guard any splice/`template_of` with `if constexpr (validator(meta).empty())` (or put the query in
   the passing branch only) so a failed validation produces **one readable `static_assert`** instead
   of a cascade of splice errors.
5. Build the message by concatenation because `std::format` is not constexpr on libstdc++ 16.2.
6. `static_assert(false)` in a discarded branch is fine on gcc 16 (P2593), but a dependent
   `always_false<T...>` is portable:

```cpp
template <class...>
inline constexpr bool always_false_v = false;
```

```cpp
template <class T, auto validator = validate_is_int>
struct validated
{
  static_assert (validator (^^T).empty (), "T failed validation");
  using type = T;
};

// Downstream reflection only in the validated branch:
template <class T, auto validator = validate_is_int>
struct deep_check
{
  static_assert (validator (^^T).empty (), "T failed validation");
  static constexpr bool value = true;
};
```

## Reference probe

`reference/reflection_probe.cpp` (next to this document) is a self-contained, compilable playground
exercising everything above: the invocability matrix printed at runtime from consteval-computed
data, `members_of` vs `nonstatic_data_members_of` vs `subobjects_of`, `define_static_array` +
`template for`, alias `dealias`, incomplete-type guard, and the validator pattern.

Compile and run from this directory:

```bash
g++ -std=c++26 -freflection reference/reflection_probe.cpp -o /tmp/probe \
    && /tmp/probe
```

Copy the file to a scratch directory and edit it to answer questions this document does not cover;
it is designed to be hacked on.
