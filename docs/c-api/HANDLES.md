# CNA C API Handles

## Representation

Every native CNA object crossing the C boundary is represented by:

```text
typedef uint64_t CNA_Handle;
```

`CNA_INVALID_HANDLE` is zero. Nonzero handles encode a 32-bit table slot in the low half and a
32-bit generation in the high half. The bit pattern is opaque: applications must compare it only
with zero or another handle, store it in the same process, and pass it back to the documented CNA
functions. Handles are never pointers, serialized resource IDs, platform handles or stable across
processes.

## Validation

The native handle registry records the owning CNA runtime, object kind, slot generation, ownership
state and any parent dependency. Every handle-taking API validates all of these before touching the
C++ object.

| Problem | Result |
|---|---|
| Zero where a live handle is required | `CNA_RESULT_INVALID_HANDLE` |
| Unknown or released slot | `CNA_RESULT_INVALID_HANDLE` |
| Old generation after slot reuse | `CNA_RESULT_INVALID_HANDLE` |
| Correct live handle but wrong object kind | `CNA_RESULT_INVALID_HANDLE` |
| Handle from a different runtime | `CNA_RESULT_INVALID_HANDLE` |
| Runtime is closing or closed | `CNA_RESULT_SHUTTING_DOWN` |

The registry must increment a generation before a released slot can be reused. A double release or
stale use therefore cannot become a reference to a newly created unrelated object.

`CNA_INVALID_HANDLE` answers `CNA_RESULT_INVALID_HANDLE` rather than an argument failure, because
zero is a handle *value* — the one that names nothing — not a missing argument. The argument
category is for the things that are not handles at all: a null output pointer, a structure whose
`struct_size` is wrong, an identity outside its `_MAXIMUM`. Keeping the two apart is what lets a
caller tell "I passed a handle that is dead" from "I passed the wrong kind of thing", and
`CApi_StressSmoke` checks both categories rather than only the results.

These rules are measured, not merely stated. `CApi_StressSmoke` creates and destroys thousands of
objects, keeps every handle it was ever issued, and requires that none is issued twice and that
every dead one still refuses after the slots beneath it have been recycled many times over; the
bit-level generation case, which a C caller cannot construct without decoding a handle it is told
to treat as opaque, is proved instead in `CApi_HandleRegistry`.

## Kinds and type conversion

The registry uses an internal object-kind tag. The C API exposes explicit, documented operations
for any legal base/derived conversion or query; it never treats a numeric handle as automatically
interchangeable between unrelated APIs. A generic `cna_handle_get_kind` diagnostic/query operation
may be added, but does not relax the typed function contracts.

## Runtime scope

The first experimental ABI permits one active CNA runtime per process. Every created handle belongs
to that runtime. Attempting to create a second active runtime returns `CNA_RESULT_INVALID_STATE`.
This deliberately makes the interaction with CNA's existing process-level graphics/input state
explicit. Supporting simultaneous runtimes later is an ABI-semantic change requiring a reviewed
design and coverage tests.

## Handle creation and release

A `create`, `load`, `clone`, `retain`, or successful callback registration function explicitly
documents whether its out handle is owned by the caller. The generic release operation and any
type-specific destroy operation are idempotence-*detecting*, not silently idempotent: releasing a
live owned handle succeeds once; a second release returns `CNA_RESULT_INVALID_HANDLE`.

Handle destruction respects the object's native thread affinity. A graphics resource released off
the required graphics thread returns `CNA_RESULT_THREAD` unless that API family documents a
deferred release queue. C code must release resources deterministically; no garbage collector,
finalizer or C++ destructor is assumed at the ABI boundary.
