# C IndexBuffer Contract

## Creation and ownership

[`index_resources.h`](../../modules/c-api/include/CNA/C/index_resources.h) exposes static
`IndexBuffer` and `DynamicIndexBuffer` objects through one owned `CNA_IndexBufferHandle` kind.
Transfers select `uint16_t` or `uint32_t` elements; this is not a generic blittable-element API.

`cna_index_buffer_create` accepts an independently owned graphics device or a currently valid
borrowed device, including the game's pre-run borrowing window. A game-device buffer survives
its creation callback and counts as a game child until typed destruction. Standalone-device
buffers do not count as game children. Destroy buffers before their creating device; see
[`GRAPHICS_DEVICE.md`](GRAPHICS_DEVICE.md#borrowed-and-caller-owned-lifetimes) and
[`HANDLES.md`](HANDLES.md).

The version-one creation descriptor requires a **positive** `index_count`, a valid index width
and `BufferUsage`, a canonical `dynamic` Boolean and zero reserved bytes. Capacity is fixed for
the buffer's lifetime. ThirtyTwoBits requires HiDef; Reach rejects it with
`CNA_RESULT_NOT_SUPPORTED`. Creation also requires the renderer's ThreeD capability and obeys
native buffer-size limits. Failed creation leaves the output handle invalid. Newly allocated
indices read as zero when readback is permitted.

`get_info` reports capacity, width, usage, dynamic identity and renderer presence. Its
`is_content_lost` field reads the native dynamic buffer's current loss flag; static buffers
report false. Device loss sets the dynamic flag, an empty upload leaves it unchanged, and a
successful nonempty upload clears it.

The dedicated type-name count/copy routes deliberately return `CNA_INDEX_BUFFER_TYPE_NAME`
(`Microsoft.Xna.Framework.Graphics.IndexBuffer`) for **both** kinds. Use the snapshot's `dynamic`
field to distinguish them. Count/copy uses UTF-8 bytes without a terminator and does not partially
write an undersized destination. Generic GraphicsResource name, string, tag, device, disposal and
Disposing-subscription routes accept either kind; their shared rules and the owned-device getter
defect are documented in [`GRAPHICS_RESOURCES.md`](GRAPHICS_RESOURCES.md).

## Caller windows and buffer offsets

`CNA_IndexBufferTransfer.start_index` always selects an element in the caller's array. `capacity`
and `element_count` are element counts, not byte counts. The descriptor's width must exactly match
the buffer's creation width; there is no implicit narrowing or widening.

| Route | Caller window | Native buffer window |
|---|---|---|
| `cna_index_buffer_set_data` | Source starts at `start_index` | Destination starts at index zero |
| `cna_index_buffer_set_data_at` | Source starts at `start_index` | Destination starts at `buffer_offset_in_bytes` |
| `cna_index_buffer_get_data` | Destination starts at `start_index` | Source starts at index zero |

The C adapter copies uploads through aligned typed scratch storage. Readback reads the native
CPU shadow into scratch and copies it into the caller's destination only after the native call
succeeds. It does not fetch current GPU storage. A failed read leaves the destination array
unchanged, including on width, WriteOnly and range refusals. This array guarantee does not cover
the separate `out_element_count`: after handle/usability validation it normally receives the
requested count even on failure, provided the transfer has a valid versioned structure prefix.
Earlier handle/usability failures leave it unchanged.

An out-of-bounds **caller** window returns `CNA_RESULT_BUFFER_TOO_SMALL`; a nonzero capacity with
a null pointer is invalid. Counts beyond the native Int32 range return `CNA_RESULT_OVERFLOW`.
`set_data` explicitly rejects a count beyond buffer capacity with `CNA_RESULT_INVALID_ARGUMENT`
and `CNA_ERROR_CATEGORY_RANGE`.
Nonempty `set_data_at` and `get_data` windows beyond native storage currently map the native
size exception to `CNA_RESULT_INVALID_STATE`. These are distinct checks.

## Upload and readback behavior

`set_data` writes a prefix of the fixed-capacity buffer. With `None`, untouched indices keep
their values. Dynamic `NoOverwrite` also preserves them; dynamic `Discard` clears the full CPU
shadow before copying the prefix, so untouched indices become zero. For example, uploading
`[5, 6]` with `None` over `[10, 20, 30, 40]` produces `[5, 6, 30, 40]` without changing capacity.

Static buffers accept only `CNA_SET_DATA_NONE`; dynamic buffers additionally accept `Discard`
and `NoOverwrite`. The native resource-in-use guard refuses a nonempty `None` upload while the
buffer is bound; dynamic `Discard` and `NoOverwrite` pass that guard. Partial writes compose the
CPU shadow and re-upload the whole buffer because the renderer contract has no destination-offset
operation. Only a full-buffer dynamic upload forwards the streaming option to the renderer.
`NoOverwrite` therefore does not promise a smaller or unsynchronized renderer transfer.

`set_data_at` writes at an explicit **byte** offset into either buffer kind and preserves all
indices outside that window. For nonempty transfers the offset must be aligned to the buffer's
element width (two or four bytes). The C route rejects offsets beyond Int32 with
`CNA_RESULT_INVALID_ARGUMENT` and accepts only `None`; `Discard` and `NoOverwrite` return
`CNA_RESULT_NOT_SUPPORTED`, even for a dynamic buffer. Its partial-write upload cost is shared
with ordinary prefix writes.

`get_data` requires `None`; other readback options return `CNA_RESULT_INVALID_ARGUMENT`.
Positive-count readback from a WriteOnly buffer returns `CNA_RESULT_NOT_SUPPORTED`.

A zero-count transfer is a C-adapter no-op after handle, disposal, descriptor, caller-window,
width and option validation. It may use a null pointer with zero capacity. It skips native
WriteOnly, binding and window checks; `set_data_at` still applies its C Int32-offset limit but
does not check native alignment or destination range for an empty window. Empty uploads do not
clear content-loss state. Uploads do not promise resource rollback on renderer failure: native
code updates its CPU shadow before submitting to the renderer.

## ContentLost, disposal and evidence

`cna_index_buffer_subscribe_content_lost` supports dynamic buffers; a static buffer returns
`CNA_RESULT_NOT_SUPPORTED`. Device recovery can notify content-losable resources, and
`cna_graphics_device_notify_content_lost_resources_ext` can explicitly trigger that notification.
Native `DynamicIndexBuffer` sets its loss flag and raises `ContentLost`; a subsequent nonempty
upload clears the flag. `get_info` exposes that flag without changing it or suppressing callbacks.

ContentLost registrations use a different handle kind from generic Disposing registrations.
They hold a weak native reference, can be unsubscribed after buffer destruction and still require
explicit unsubscription. Callback/context storage remains caller-owned for the subscription's
active lifetime; see [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

`cna_graphics_resource_dispose` raises Disposing, releases the native renderer and keeps the C
handle and immutable metadata queryable. Later data operations return
`CNA_RESULT_INVALID_STATE`. Repeated explicit disposal is a no-op. By contrast,
`cna_index_buffer_destroy` releases the handle without calling public `Dispose`; do not rely on
typed destruction to emit Disposing. Dispose explicitly first when that notification is needed.
An active `ModelMeshPart` reference blocks both explicit disposal and typed destruction with
`CNA_RESULT_INVALID_STATE`. Typed destruction invalidates the handle generation and removes any
game-child count; it remains necessary after explicit disposal.

`CApi_IndexBufferSmoke` is a strict-C17 test covering both widths, static/dynamic creation,
caller windows, all streaming options, offset uploads, WriteOnly refusal, metadata and base type
names, event-kind separation, disposal, failure atomicity and invalid/stale/wrong-kind/wrong-thread
paths. Its no-ThreeD branch checks creation refusal. Header checks cover version-one layouts.
The CNA-FIX-D16 regression adds two loss/reupload cycles, verifies the event and true dynamic
snapshot, checks that an empty upload preserves the loss and a nonempty upload clears it, and
confirms a static buffer stays false. The extended smoke failed before the fix and passes on
OPENGLES3 and SDLGPU through the private runner. These focused runs do not establish a new
sanitizer or native Windows result.
