# C VertexBuffer Contract

## Creation, device and metadata

`vertex_resources.h` maps static and dynamic buffers to the same owned
`CNA_VertexBufferHandle` kind. Creation accepts either an independently owned device or a valid
game-device loan, including the pre-run window. A buffer survives a callback but must be destroyed
before its owning game/device. The renderer must advertise the ThreeD capability.

`CNA_VertexBufferCreateInfo` requires a positive fixed vertex capacity, valid usage and a canonical
Boolean dynamic flag. It copies the supplied declaration, so that declaration handle may then be
destroyed. A zero declaration token selects the static empty-declaration extension and requires
`BufferUsage.None`; it cannot create a dynamic buffer.

The info snapshot reports capacity, usage, dynamic identity, renderer presence and declaration
stride/element count. Both static and dynamic handles use the dedicated base type-name routes'
`Microsoft.Xna.Framework.Graphics.VertexBuffer` name, even though the native dynamic type has its
own name. Declaration elements use count/copy. No renderer or native object pointer is exposed.

The C info field `is_content_lost` is currently hard-coded false. This does not describe the native
dynamic state: device content-loss notification sets the native flag and raises `ContentLost`,
and a nonempty write clears it. The C snapshot defect is recorded in
[handoff.md §5.1](../../handoff.md#51-defects).

## Typed array transfers

`CNA_VertexBufferTransfer` selects one of the seven built-in layouts, options and a window in the
caller's array. `start_index` indexes that array; it is not a buffer-side offset. Capacity must cover
`start_index + element_count`, and typed counts must fit native Int32. A too-small caller array
returns `CNA_RESULT_BUFFER_TOO_SMALL`.

Upload converts the selected C values and writes a prefix beginning at native vertex zero.
It preserves the remaining fixed-capacity shadow unless Discard is selected; unwritten bytes are
zero. Typed readback begins at native vertex zero and writes at the caller's start index. Native
scratch storage and conversion complete before caller memory is changed. WriteOnly buffers
refuse nonempty reads, and data operations on a disposed buffer return `CNA_RESULT_INVALID_STATE`.

A zero-element typed transfer is a no-op after handle, disposal, descriptor, capacity and
option validation; a null pointer is allowed with zero capacity. Readback requires
`CNA_SET_DATA_NONE`. Its count output reports the requested transfer count after descriptor
processing, including some descriptor/capacity failures; earlier handle/disposal failures leave it
untouched. It is not a query for the buffer's full capacity.

For dynamic typed uploads, None, Discard and NoOverwrite are implemented for PositionColor,
PositionColorTexture, PositionNormalTexture and PositionTexture. Non-None options on static
buffers or the other three extension layouts return `CNA_RESULT_NOT_SUPPORTED`.
Options are discrete identities, not combinable flags.

## Raw bytes and buffer-side windows

The raw routes describe contiguous bytes representing `vertex_count` vertices at a positive
`vertex_stride`. The source/destination byte capacity must cover their product. Native validation
checks stride/declaration compatibility and logical buffer bounds. Typed C++ layout equivalence is
not assumed.

| Route | Buffer origin / options |
|---|---|
| `cna_vertex_buffer_set_data_raw` | Writes from buffer byte zero with None behavior. |
| `cna_vertex_buffer_set_data_raw_at` | Writes at `buffer_offset_in_bytes`. |
| `cna_vertex_buffer_get_data_raw` | Reads at `buffer_offset_in_bytes`, including custom layouts. |
| `cna_vertex_buffer_set_data_raw_with_options` | Dynamic-only prefix upload with explicit options. |
| `cna_vertex_buffer_set_data_raw_at_with_options` | Dynamic-only window upload with explicit options. |

Buffer-side offsets must fit Int32 and fall on vertex boundaries; windows must fit the declared
capacity. Zero-count raw transfers retain their own range/stride validation. Explicit-options raw
routes require a dynamic buffer even when options is None.

None and NoOverwrite preserve bytes outside the uploaded prefix/window; Discard zeros the entire
shadow before copying. NoOverwrite is a caller synchronization promise, not automatic hazard
tracking. The native in-use guard refuses None writes to a currently bound buffer; explicit
dynamic Discard and NoOverwrite bypass that guard.

CNA composes updates on its CPU shadow and sends the complete fixed-capacity buffer to the renderer.
A small window therefore has whole-buffer GPU upload cost. Readback uses that shadow, not a GPU
synchronization/readback operation.

## Events and release

`cna_vertex_buffer_subscribe_content_lost` accepts a dynamic buffer and returns an owned
registration. It receives explicit device content-loss notifications; a static-buffer subscription
returns `CNA_RESULT_NOT_SUPPORTED`. Destroy the registration with its matching unsubscribe route.
Its weak native source reference permits unsubscription after buffer destruction.

A ModelMeshPart can retain the buffer; typed destroy and generic graphics-resource disposal then
return `CNA_RESULT_INVALID_STATE`. Remove the retention first. A permitted generic dispose leaves
the handle and metadata available while releasing native renderer state; typed destroy releases the
C registry entry and owning game/device child count.

## Verification

`CApi_VertexBufferSmoke` covers built-in layouts, dynamic/raw options, offsets, prefix preservation,
declaration copies, disposal and invalid/stale/kind/thread refusals. Its existing event assertions
cover subscription lifetime and an initial false loss state, not a notification-to-info transition.
`CApi_GraphicsDeviceSmoke` and model/lifetime tests cover related device and resource use.
C17/C++23 header targets check ABI layout. Renderer refusals are capability-dependent; a run on one
renderer is not evidence for all renderer implementations.
