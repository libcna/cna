# CNA C API Handles

## Representation and identity

Objects with C handle lifetimes use the fixed-width definition in `CNA/C/abi.h`:

```c
typedef uint64_t CNA_Handle;
```

`CNA_INVALID_HANDLE` is zero. Handles are opaque, process-local tokens. Store or compare them and
pass them back to the documented routes; do not decode them, perform arithmetic on them, convert
them to pointers or persist them as resource IDs. Value types use their own POD structures, and
native-window interop uses its separately documented token/descriptor contract.

Copying a handle's bits does not create a second owned reference. Two variables containing the
same owned handle share one release obligation. Conversely, two different handles may be aliases
of the same native object; numeric inequality does not prove independent native state. Ownership
and borrow duration belong to the route that supplied the handle, not to its numeric value.

The current registry encodes a nonzero slot identifier in the low 32 bits and a generation in the
high 32 bits. The slot identifier is the internal zero-based index plus one. This describes the
implementation, not a layout consumers should depend on.

## Registry and family validation

The registry is process-wide within the loaded CNA library. A slot stores its generation, internal
object kind, a shared registered-object reference, creation thread and C tag token. Typed lookup
checks that the slot is live, its generation matches and its kind is the expected one; ordinary
typed lookup also checks its creation thread and keeps the registered object alive for the call.

Parent dependencies, borrowed-view restrictions, device ownership and native disposal state are
checked by family adapters and native objects. They are not separate runtime/ownership/parent
fields in the registry. For example, a live texture handle may still be refused for use on another
device or for destruction while a font retains it.

For a required handle lookup, the normal results are:

| Problem | Result |
|---|---|
| Zero where a live handle is required | `CNA_RESULT_INVALID_HANDLE` |
| Unknown or released slot | `CNA_RESULT_INVALID_HANDLE` |
| Old generation after slot reuse | `CNA_RESULT_INVALID_HANDLE` |
| Correct live handle but wrong object kind | `CNA_RESULT_INVALID_HANDLE` |
| Expired callback-scoped handle after its entry is released | `CNA_RESULT_INVALID_HANDLE` |
| Valid thread-affine handle used from another thread | `CNA_RESULT_THREAD`, unless a documented exception applies |

These are lookup rules, not a universal order for all route checks. Required output validation,
argument checks or a compiled-out feature may return a result before lookup is reached. A null
required output normally returns `CNA_RESULT_INVALID_ARGUMENT`; an invalid required handle
normally returns `CNA_RESULT_INVALID_HANDLE`. Result categories and boundary ordering are in
[`ERRORS.md`](ERRORS.md) and [`GENERATED_ROUTES.md`](GENERATED_ROUTES.md).

Zero also has route-specific meanings. For example, `cna_graphics_device_set_texture` accepts it
to clear a slot, and an optional lookup may succeed with an invalid output to report absence.
`cna_effect_set_current_technique`, by contrast, rejects a zero technique with
`CNA_RESULT_INVALID_ARGUMENT` under its native null-assignment rule. Read the parameter's
contract before interpreting zero as a failed lookup.

## Reuse and generation exhaustion

Release removes the object from its slot, clears the kind/thread/tag metadata and advances the
generation before the slot can be reused. A stale handle therefore cannot resolve to an object
later allocated in the same slot. Reuse also resets the C tag to zero.

After the last nonzero 32-bit generation is released, the slot is permanently retired with
generation zero. Allocation skips retired slots instead of wrapping to generation one. Exhausting
the representable slot range returns `CNA_RESULT_OVERFLOW`. Normal game destruction releases the
game and its scoped entries without replacing the process-wide registry, so later games retain
the same generation protection.

## Kinds and type conversion

Public names such as `CNA_EffectHandle` are aliases of `CNA_Handle`. C's type system does not enforce
the runtime kind; a cast or assignment cannot make a handle valid for an unrelated family.

Legal base/derived use is explicitly implemented by the accepting route. A RenderTarget2D handle,
for example, is accepted by the Texture2D routes and the shared graphics-resource routes. An
effect parameter or collection view has its own kind and cannot replace its parent effect handle.
There is no public generic kind-query operation.

## Process scope and games

The C API has no public create/destroy-runtime handle. `cna_game_create` permits one active C-owned
game and returns `CNA_RESULT_INVALID_STATE` for a second while that game remains live. After
`cna_game_destroy`, another game may be created. Game-independent objects can be created without a
game, and multiple devices from `cna_graphics_device_create` may coexist.

There is no separate runtime identifier encoded or checked by the registry. Its handles belong to
the CNA library instance that issued them. The single-game rule must not be interpreted as a
single-device rule or a requirement that every handle have a game parent.

`CNA_RESULT_SHUTTING_DOWN` is a defined ABI result, but the current registry has no closing-state
gate that returns it. Game destruction with live tracked children returns
`CNA_RESULT_INVALID_STATE`; disabling/destroying the foreign-thread service also cancels pending
calls with `CNA_RESULT_INVALID_STATE`. Already released handles subsequently fail lookup normally.
The supported shutdown sequence is in
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md#shutdown-order).

## Handle creation and release

Each route documents whether its output is owned, borrowed or retained by another object. An owned
handle is released with its family destroy/unsubscribe route. No public generic release or generic
retain operation exists. Successful release invalidates that registry entry; releasing it again
returns `CNA_RESULT_INVALID_HANDLE` under the family's enabled contract.

An owned view can retain a native owner independently of its parent's handle. Effect descendant
views are one example: they remain usable after the parent handle is released, but each view still
requires its own release. A borrowed device has a validity window and no release obligation;
copying or retaining its numeric value does not extend that window. See
[`EFFECTS.md`](EFFECTS.md), [`OWNERSHIP.md`](OWNERSHIP.md) and
[`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md#borrowed-graphics-device-handle).

Native disposal and handle release are separate operations. A permitted
`cna_graphics_resource_dispose` leaves its handle valid for disposal-state queries and typed
cleanup; it does not turn the handle into a stale token. Other families have their own disposal
rules, including observable repeated component disposal. A game destroy can finish releasing the
game while returning `CNA_RESULT_CALLBACK` for a shutdown callback failure. Follow the family
contract rather than assuming that every failed result leaves ownership unchanged.

## Thread ownership

Ordinary lookup and release require the thread that created the handle. The two documented
sound-effect play routes have an any-thread lookup path. A game may also enable the foreign-thread
service, which queues qualifying calls to its owner thread and returns their result to the caller.
It does not transfer handle ownership, and game-driving, game destruction and queue-control calls
remain owner-only. The full exceptions, queue pumping and shutdown rules are in
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

Registry locking protects table access and lookup ownership; it does not make every native object
safe for concurrent calls. Release drops the registered object's shared reference outside its mutex
so native destruction can perform its own cleanup. Applications must release deterministically on
the appropriate thread; process-exit cleanup is not a replacement for explicit release.

## Verification boundary

`CApi_HandleRegistry` tests ordinary slot reuse, stale and double release, per-handle tags and
wrong-thread refusal. It tests the generation helper's transition at `UINT32_MAX` to zero and its
zero-to-zero retirement behavior; it does not perform billions of public create/release cycles.
`CApi_StressSmoke` checks opaque-handle uniqueness across recorded reuse cycles, revisits stale
handles after churn, checks wrong kinds/threads and distinguishes argument from handle diagnostics.
It also exercises game-child and surviving-subscription lifetimes.

Lifecycle tests cover callback loans, effect tests cover retained descendant views, and the
foreign-thread/owner-thread suites cover dispatch and operations that must never be queued. ABI
and generated boundary tests supplement these cases. A census of route names is not proof of
every family's lifetime behavior, and registry validation does not replace native-state tests.
