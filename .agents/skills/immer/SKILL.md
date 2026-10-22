---
name: immer
description: >-
  Working with Immer persistent containers, especially immer::array and array_transient:
  immutable element access, shared ownership, copy-on-write consumption, move-only values,
  memory policies, and safe asynchronous element processing. Use when adding or changing
  Immer code or diagnosing unexpected element copies.
license: MIT
---

<!-- fnfelt
SPDX-License-Identifier: MIT
Copyright 2026 David Feltell
-->

# Immer arrays: ownership, mutation and moves

## Scope and evidence

These findings were established with **Immer 0.9.1**, the header-only dependency pinned in
`conanfile.txt`, during implementation of fnfelt's array `io::sequence`. Ownership behaviour was
checked against the installed headers and copy-counting tests; synchronous and asynchronous
sequencing, empty arrays, non-default-constructible results and move-only results were tested.

Use this guide instead of rescanning Immer's sources for the cases below. It describes
`immer::array`, not a verified API reference for `immer::vector`, `flex_vector` or every memory
policy. Recheck the relevant public documentation when changing versions or using another container
or policy.

## Mental model

- An immutable array handle can share its backing storage with other handles. Moving a handle does
  **not** prove that the storage is unique.
- Immutable access stays read-only, even through a non-const handle or an rvalue handle.
- A transient is the supported mutable interface. Converting to a transient and requesting mutable
  data are separate operations: the mutable-data request handles detachment if needed.
- Runtime uniqueness can avoid element copies, but does not remove compile-time requirements imposed
  by a copy-on-write implementation.
- `immer::array` has contiguous element storage; do not generalise its raw-data pattern to other
  Immer containers.

## Headers and useful operations

```cpp
#include <immer/array.hpp>
#include <immer/array_transient.hpp>
```

Include the transient header explicitly when using its methods; do not rely on `array.hpp` alone
providing the complete transient definition.

| Operation                        | Behaviour relevant to ownership                                                     |
| -------------------------------- | ----------------------------------------------------------------------------------- |
| `immer::array<T, TMemoryPolicy>` | Immutable array with an optional memory-policy parameter.                           |
| `ios[idx]`                       | Returns `T const&`, including on a non-const array.                                 |
| `std::move(ios)[idx]`            | Still returns `T const&`; there is no consuming subscript overload.                 |
| `std::move(ios[idx])`            | Produces `T const&&`, not a mutable rvalue; normally copies rather than moves.      |
| `std::move(ios).transient()`     | Transfers the handle into transient form; storage may still be shared.              |
| `elements.data_mut()`            | Returns mutable contiguous data, detaching by copying elements if necessary.        |
| `elements.size()`                | Supplies the length for a span over the mutable data.                               |
| `array{first, last}`             | Constructs fresh storage from an iterator range; move iterators can move values in. |

There is no ordinary public `array::unique()` accessor in the checked version. Do not inspect
reference-count internals or cast away `const`; let `data_mut()` check whether mutation is safe.

## Consume elements through a transient

Take ownership of an array handle by value, then move that handle into a transient:

```cpp
#include <span>
#include <utility>

#include <immer/array.hpp>
#include <immer/array_transient.hpp>

template <class T, class TMemoryPolicy, class TConsume>
void consume_array(immer::array<T, TMemoryPolicy> ios, TConsume && consume)
{
    auto elements = std::move(ios).transient();
    auto children = std::span{elements.data_mut(), elements.size()};
    for (auto & child : children)
    {
        consume(std::move(child));
    }
}
```

With the project's default policy:

- **Unique storage:** mutable access reuses it, so consuming the elements needs no element copies.
- **Shared storage:** mutable access first copies the elements into separate storage. Moving from
  those copies leaves the other immutable handles unchanged.
- **Empty arrays:** the zero-length span and loop need no special element access.

Calling with an lvalue copies an array handle, retaining a shared owner; calling with an rvalue can
transfer the last owner. An rvalue is only an opportunity to avoid copying, not a guarantee.

Keep the transient alive while using its pointers, references or spans. Do not resize, replace, move
or otherwise invalidate its storage while those views are in use. Treat mutable access as exclusive;
a transient is not a synchronisation mechanism.

### Copyability is still required

`data_mut()` instantiates the branch that copies shared storage even when this particular call finds
unique storage at runtime. Consequently this consumption path requires copyable `T` in Immer 0.9.1;
it is **not** general support for arrays of move-only input IOs.

Do not claim that all uses of `immer::array<T>` require copyable `T`, either. Requirements depend on
the operation. Constructing fresh storage by moving from a range can support move-only values.

If owning and consuming move-only inputs is essential, an owned `std::vector<T>` is an alternative
without implicit sharing. Even there, use `std::move(ios[idx])`: moving the vector handle before
subscripting does not itself move an element.

## Construct results by moving into fresh storage

Avoid building results through operations that need to copy existing elements when the result type
may be move-only. Stage values in fresh storage and use move iterators:

```cpp
#include <iterator>
#include <memory>
#include <vector>

#include <immer/array.hpp>

std::vector<std::unique_ptr<int>> outputs;
outputs.push_back(std::make_unique<int>(20));
outputs.push_back(std::make_unique<int>(22));

auto values = immer::array<std::unique_ptr<int>>{
    std::make_move_iterator(outputs.begin()), std::make_move_iterator(outputs.end())};
```

This construction does not require default-constructing the result elements or copying them. It does
not make later element-copying operations valid for move-only types; in particular, do not assume
the transient copy-on-write consumption path will work on this result.

For generic transformations, preserve the input policy explicitly in the result type:
`immer::array<TValue, TMemoryPolicy>`. Avoid silently reverting to the default policy.

## Lazy and asynchronous consumers

Preserve ownership all the way to execution, not just when constructing a lazy operation:

- A reusable lvalue operation must retain its stored input handle. Execute through an owned copy of
  that handle so detaching preserves the original elements for subsequent runs.
- An rvalue operation can move its stored handle into execution. Avoid extra live handle copies
  along that path, or the transient will correctly detect sharing and copy the elements anyway.
- Materialise the transient in the async driver that owns the input, not in a short-lived caller
  whose storage could disappear before child tasks run.

fnfelt's implementation is in `include/fnfelt/monad/io/detail/sequence.hpp`:

- `ArraySequenceAction` forwards the stored array's value category into an owned handle.
- `ArraySequenceProxy::fn` moves its by-value input to a transient, then forks each moved child into
  a pre-sized indexed `lf::eventually` slot.
- The driver joins before reading any result slots and constructs one immutable result array with
  move iterators. Input order is preserved regardless of completion order.

The indexed slots are separate from Immer's input storage. Keep their addresses stable until the
join; do not grow the slot vector after forking. This avoids intermediate immutable-array
concatenations and does not require default-constructible result values. The scheduler details are
libfork-specific, not functionality supplied by Immer.

## Regression checklist

See `src/monad/io/sequence.cpp` for the existing regression cases. When changing ownership paths,
check both synchronous and asynchronous execution:

1. Reset element copy counters after constructing the input, excluding setup copies.
2. Consume a unique rvalue array: no element copies during execution.
3. Retain another handle, then consume: one copy per element to detach, and the retained handle
   remains usable with its original values.
4. Run an lvalue sequence twice, then consume it as an rvalue: each reusable run detaches, but the
   final consumption needs no further element copies if no other owners remain.
5. Cover empty and single-element inputs, input-order preservation, non-default-constructible
   results, move-only results and memory-policy preservation.

The copy-counting regression uses a copyable action owning a `std::unique_ptr`, a deep-copying copy
constructor, an atomic copy counter, and an rvalue-only consuming call operator. This catches both
accidental copies and unintended consumption of retained inputs.
