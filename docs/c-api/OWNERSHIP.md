# CNA C API Ownership and Lifetime

Every handle and pointer parameter has one documented ownership category. A public declaration is
incomplete until its Doxygen documentation names the category.

| Category | Rule |
|---|---|
| Owned | Caller receives a live handle and must release it exactly once before runtime shutdown. |
| Borrowed | Caller may use it only for the documented call/callback duration and must not retain or release it. |
| Retained | CNA takes an additional reference or copied value; caller keeps its own ownership unless documentation says transfer. |
| Transferred | CNA consumes caller ownership on successful return; failure leaves caller ownership unchanged. |
| Callback context | Caller owns `void* context` and keeps it valid until successful unregister/destruction completes. |
| Caller buffer | Caller owns input/output storage for the entire call; CNA never stores it unless an API explicitly says it copies/retains. |

## Parent and child resources

Some CNA resources are logically tied to a parent, such as graphics resources to a graphics device
or content-loaded values to a content manager. The C mapping names that rule per family. A child
must either retain the required parent internally or fail with `CNA_RESULT_INVALID_STATE` after its
parent is disposed; it must never become an unchecked dangling C++ pointer.

Destroying the runtime invalidates every remaining handle. The runtime shutdown path releases native
resources in its documented dependency order. C code remains responsible for explicit release before
shutdown so release failures are observable and leak tests remain meaningful.

## Values, strings and arrays

POD input values are copied on entry. String views and arrays are borrowed only for the duration of
the function call unless a declaration explicitly uses a retained/copying API. Returned scalar and
POD values are written to caller-owned out parameters. Returned text and arrays follow the query and
copy protocol in [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md), never an undocumented native
allocator ownership rule.

## Services, events and callbacks

An event subscription returns an owned registration handle. Destroying that registration removes
the callback and synchronizes with any documented in-flight invocation before the call returns.
Destroying its source removes all registrations and invalidates their handles. A callback receives
borrowed CNA handles only; it may request supported state changes but may not retain them without a
separate documented retain operation.

## Disposal mapping

Where a public CNA C++ type implements `System::IDisposable`, its C API mapping exposes an explicit
release/destroy path. Where it is a C++ value type, it maps to a POD value/copy operation instead.
The C surface does not require applications to understand C++ RAII or Sharp Runtime disposal.
