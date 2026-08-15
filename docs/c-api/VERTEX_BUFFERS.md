# C VertexBuffer Contract

## Scope and ownership

`vertex_resources.h` maps the complete public `VertexBuffer` and `DynamicVertexBuffer` surface to
an owned, generation/type/thread-validated `CNA_VertexBufferHandle`. A successful create call
produces a game child that survives the lifecycle callback but must be destroyed before its game.
The callback-scoped graphics-device handle is accepted only during that callback.

`CNA_VertexBufferCreateInfo` selects a copied `VertexDeclaration`, nonnegative logical capacity,
`BufferUsage` and static/dynamic kind. An invalid declaration is reserved for CNA's static
empty-declaration constructor and therefore requires `BufferUsage.None`. The declaration handle
may be destroyed immediately after successful buffer creation because the native buffer owns its
own value copy.

The versioned info snapshot reports capacity, usage, dynamic identity, inert content-loss state,
renderer presence, stride and declaration-element count. Declaration elements and the exact
native type name use count/copy operations; renderer objects and native pointers never cross the
C ABI. Generic GraphicsResource name, tag, device, disposal and Disposing-event operations accept
the vertex-buffer handle.

## Typed and raw transfer

`CNA_VertexBufferTransfer` selects one of all seven `CNA_VertexType` values and a window in the
caller's typed C array. `cna_vertex_buffer_set_data` converts that window to native values before
calling the matching overload. `cna_vertex_buffer_get_data` reads into native scratch storage and
copies into the requested destination window only after complete success, so capacity, WriteOnly
and native failures never partially modify caller memory.

For both directions `start_index` is an index in the caller array. Upload replaces the buffer from
native destination vertex zero, matching CNA's current overloads. Readback begins at native
buffer vertex zero and writes at the caller start index, matching the documented XNA overload
contract without exposing CNA's internal CPU-shadow representation. A zero-element window is a
validated no-op and may use a null pointer with zero capacity.

`SetDataOptions.None`, `Discard` and `NoOverwrite` are accepted by the four native dynamic
overloads for PositionColor, PositionColorTexture, PositionNormalTexture and PositionTexture.
Non-None options on a static buffer or one of the three CNA extension vertex types return
`CNA_RESULT_NOT_SUPPORTED`. Raw upload takes an explicit byte capacity, vertex count and positive
stride; a nonempty declaration requires the native stride to match.

## Events, disposal and evidence

`cna_vertex_buffer_subscribe_content_lost` retains a callback/context registration for a dynamic
buffer. CNA's `IsContentLost` property is currently always false and `ContentLost` is never
raised, but the registration and unregistration lifetime is fully represented. A distinct event
handle kind prevents confusion with GraphicsResource Disposing registrations, and a registration
may be released after its buffer is destroyed.

Explicit generic disposal keeps the C handle and immutable metadata alive while releasing the
renderer; subsequent data operations return `CNA_RESULT_INVALID_STATE`. Typed destruction then
invalidates the handle generation and releases the game-child ownership count.

`VertexBufferSmoke.c` is strict C17 and runs unchanged under HEADLESS and SDL_RENDERER. HEADLESS
covers all seven value layouts, count and window overloads, all four dynamic option routes, raw
upload, WriteOnly refusal, copied declarations, type/metadata queries, event separation, disposal,
capacity atomicity and invalid/stale/wrong-kind/wrong-thread paths. SDL_RENDERER advertises no 3D
capability, so the same suite verifies atomic `CNA_RESULT_NOT_SUPPORTED` creation instead of
inventing a buffer. C17/C++23 header checks freeze the version-one descriptor layout, and the
focused successful flow also runs under ASan+UBSan.
