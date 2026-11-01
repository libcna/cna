# Sharp Runtime Boundary for the CNA C API

Sharp Runtime is a native C++ implementation dependency. A C consumer includes `CNA/C/*.h` and
links the C API through its documented build target or installed library dependencies; it does not
include Sharp Runtime headers or depend on Sharp Runtime's C++ object layout. This does not imply
that a static C build has no transitive native link dependencies.

| Native concept | Current C representation |
|---|---|
| String | `CNA_StringView` input and UTF-8 size/copy output. |
| Exception | `CNA_Result`, per-thread `CNA_ErrorInfo` and diagnostic text; documented supplemental payloads use separate query routes. |
| List/vector/span | Pointer and fixed-width count, count/copy, indexed access or a typed collection/enumerator handle. |
| Dictionary | Explicit lookup/enumeration or a documented opaque foreign-object value; no native map layout. |
| TimeSpan/date-time | Fixed-width ticks/duration/timestamp values with the route's explicit unit and epoch. |
| Stream | A typed C storage-stream handle where exposed; other routes consume/produce caller-owned encoded bytes or paths. No Sharp Runtime stream pointer. |
| Begin/End asynchronous operation | A family-specific completion route/callback. Networking/storage routes wait and complete synchronously at the C boundary; Guide operations follow their documented native completion behavior. No general future/poll/cancel operation-handle API exists. |
| Delegate/event | Typed C callback plus context; subscriptions use explicit registration handles where supported. |
| Buffer/memory | Caller-owned pointer and fixed-width count/capacity, or a documented callback-scoped borrow. |
| Object hierarchy | POD when value semantics suffice; otherwise typed, generation-checked handles with only the family-specific type/base operations actually exported. |

No public generic kind-query, object cast or runtime-operation API should be inferred from the
native hierarchy. An opaque foreign pointer explicitly supplied/received by a caller is also not
permission to pass a `System::Object*` across the boundary.

Conversions and exception handling live on the native adapter side. C callers need not recreate
Sharp Runtime collection, task, delegate, exception or string-layout rules. Internal refactors
that preserve the documented C behavior must preserve its ABI.

`modules/c-api/cmake/CheckPublicHeaders.cmake` screens public headers for C++-only constructs.
The strict C17 and C++23 header targets and ABI layout assertions complement that token gate:
a text scan alone is not proof of a valid C declaration or a compatible binary layout.

See [ABI_VERSIONING.md](ABI_VERSIONING.md), [OWNERSHIP.md](OWNERSHIP.md),
[ERRORS.md](ERRORS.md) and [STRINGS_AND_BUFFERS.md](STRINGS_AND_BUFFERS.md).
