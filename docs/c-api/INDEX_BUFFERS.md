# C IndexBuffer Contract

## Scope and ownership

`index_resources.h` maps the complete public `IndexBuffer` and `DynamicIndexBuffer` surface to an
owned, generation/type/thread-validated `CNA_IndexBufferHandle`. A successful create call produces
a game child that survives its lifecycle callback but must be destroyed before the game. The
callback-scoped graphics-device handle is accepted only while that callback is active.

`CNA_IndexBufferCreateInfo` selects a nonnegative logical capacity, 16- or 32-bit index width,
`BufferUsage` and static/dynamic kind. The 16-bit static `BufferUsage.None` configuration uses the
native convenience constructor; every other valid configuration uses the explicit native
constructor. The versioned info snapshot reports these immutable values, dynamic identity, the
current false content-loss invariant and renderer presence. The exact native type name uses
count/copy operations; renderer objects and native pointers never cross the ABI. Generic
GraphicsResource name, tag, device, disposal and Disposing-event operations accept either buffer
kind.

## Transfers

`CNA_IndexBufferTransfer` selects the stored 16- or 32-bit representation, a streaming option and a
window in the caller's typed array. `cna_index_buffer_set_data` copies that source window into
aligned native scratch storage before dispatching the matching count/window or dynamic overload.
`cna_index_buffer_get_data` first completes native readback into scratch storage and only then
copies it into the caller's destination window. Capacity, width, WriteOnly and native failures
therefore never partially modify caller memory.

For both directions `start_index` addresses the caller array. Upload replaces the native buffer
from index zero, matching CNA's existing overload behavior. Readback begins at native buffer index
zero and writes at the caller start index, matching the documented XNA destination-array contract
without exposing CNA's internal CPU shadow. A zero-element window is a validated no-op and may use
a null pointer with zero capacity.

Static buffers accept only `CNA_SET_DATA_NONE`. Dynamic buffers accept `None`, `Discard` and
`NoOverwrite` for both index widths. The descriptor width must match the buffer's creation width;
there is no implicit narrowing or widening.

## Events, disposal and evidence

`cna_index_buffer_subscribe_content_lost` retains a callback/context registration for a dynamic
buffer. CNA's `IsContentLost` property is currently always false and `ContentLost` is never raised,
but registration and unregistration lifetime are fully represented. A distinct event handle kind
prevents confusion with GraphicsResource Disposing registrations, and a registration may be
released after its buffer is destroyed.

Explicit generic disposal keeps the C handle and immutable metadata alive while releasing the
renderer; later data operations return `CNA_RESULT_INVALID_STATE`. Typed destruction invalidates
the handle generation and releases the game-child ownership count.

`IndexBufferSmoke.c` is strict C17 and runs unchanged under HEADLESS and SDL_RENDERER. HEADLESS
covers both index widths, count and caller-window overloads, static/dynamic construction, all three
streaming options, WriteOnly refusal, metadata/type queries, generic resource state, event
separation, disposal, capacity atomicity and invalid/stale/wrong-kind/wrong-thread paths.
SDL_RENDERER advertises no 3D capability, so the same suite verifies atomic
`CNA_RESULT_NOT_SUPPORTED` creation. C17/C++23 header checks freeze all version-one layouts, and the
focused successful flow also runs under ASan+UBSan.
