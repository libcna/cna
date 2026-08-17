# Sharp Runtime Boundary for the CNA C API

Sharp Runtime remains an internal C++ dependency of CNA. A C consumer includes only `CNA/C/*.h`,
links the CNA C API library and does not include Sharp Runtime headers or depend on a Sharp Runtime
binary ABI.

| Native implementation concept | C API mapping |
|---|---|
| `System::String` / `std::string` | `CNA_StringView` input; UTF-8 size/copy output. |
| `System::Exception` / `std::exception` | `CNA_Result` plus `CNA_ErrorInfo` and UTF-8 diagnostic copy. |
| `List<T>` / `std::vector<T>` / span | Pointer plus fixed-width count, or count/query/copy collection API. |
| Dictionary/map | Explicit UTF-8 lookup and key/value enumeration APIs. |
| `System::TimeSpan`, date/time | Fixed ticks/duration/timestamp POD values with explicit epoch/unit. |
| `System::IO::Stream` | CNA stream callback table or CNA stream handle, never a native pointer. |
| `Task<T>` / future | CNA operation handle with explicit poll/cancel/result/callback contract. |
| Delegate/event | C function pointer, context pointer and registration handle. |
| `Memory<T>` / managed buffer | Caller-owned pointer and fixed-width count/capacity. |
| `System::Object` hierarchy | Typed/generation-checked CNA handle and explicit type query/cast function. |

The adapter performs these conversions once on the CNA native side. A C caller must never recreate
Sharp Runtime ownership, exception, task, delegate, collection or string-layout rules. A Sharp
Runtime internal refactor that does not change CNA behavior must not change this C ABI.

The C header gate rejects `System::`, `SharpRuntime`, `std::`, `namespace`, `class`, `template`,
references, `throw` and other C++-only surface tokens. A real C compiler is the authoritative test
that the public ABI boundary is clean.
