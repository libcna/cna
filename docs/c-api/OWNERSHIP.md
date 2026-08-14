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

The initial runtime adapter permits one C-owned game. `cna_game_destroy` is its owned-handle release
operation: it invalidates the game only after exit/unload notification and native disposal complete.
C code remains responsible for explicit release before shutdown so release failures are observable
and leak tests remain meaningful.

An owned C `Texture2D` remains valid after the lifecycle callback that creates it, even though the
graphics-device handle used for creation is callback-scoped. The texture remains a child of the
active game. `cna_game_destroy` returns `CNA_RESULT_INVALID_STATE` without starting shutdown while
any owned C graphics child is live; callers must destroy those children first. Texture destruction
performs canonical `Dispose`, releases its generation-checked handle and makes a second destroy a
deterministic `CNA_RESULT_INVALID_HANDLE`.

An owned C `SpriteBatch` follows the same game-child rule. A successful bulk submission retains
each referenced texture until `cna_sprite_batch_end` flushes successfully, so destroying such a
texture returns `CNA_RESULT_INVALID_STATE`. Destroying the batch during an active interval is the
explicit recovery path: it cancels unflushed deferred commands, releases texture references and
invalidates the batch handle. Immediate-mode commands already sent to the renderer cannot be
undone. Texture commands are rejected unless texture and batch belong to the same game.

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
