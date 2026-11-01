# CNA C API Strings, Buffers and Collections

## UTF-8 input

Text at the ABI boundary uses UTF-8:

```c
typedef struct CNA_StringView {
    const char* data;
    uint64_t byte_length;
} CNA_StringView;
```

A null pointer is valid only with zero length; a non-null empty string is valid too. CNA reads the
explicit byte range, without requiring a terminator. Input must encode valid Unicode scalar
values. Paths, asset names, window titles and identifiers generally reject embedded U+0000 with
`CNA_RESULT_ENCODING`; each declaration states whether it permits it.

Views borrow caller storage for the call. Routes that retain a logical name/path copy its bytes;
they do not retain the caller's pointer. No `wchar_t`, locale encoding, native string layout or
temporary native string pointer is an output contract.

## Size and copy

Text and copied arrays normally offer a size/count query followed by a copy into caller storage.
Counts name bytes or typed elements explicitly. Text counts exclude a terminator, and ordinary
copy routes do not append one. Allocate an extra byte and append it yourself when a C-string
consumer needs one.

For the shared copy protocol:

| Input/capacity | Result |
|---|---|
| Null required count output | `CNA_RESULT_INVALID_ARGUMENT`. |
| Null destination, nonzero capacity | `CNA_RESULT_INVALID_ARGUMENT`. |
| Null destination, zero capacity, nonempty result | `CNA_RESULT_BUFFER_TOO_SMALL`, required count reported. |
| Valid destination with insufficient capacity | `CNA_RESULT_BUFFER_TOO_SMALL`, required count reported, no partial data write. |
| Zero-size result and zero capacity | Success, count zero; null destination allowed. |
| Sufficient capacity | Success and the complete result, without changing the unused suffix. |

This table describes the size/copy protocol, not every operation taking a pointer and count.
Required-count timing on other failures is route-specific: argument, handle, thread or native
failure may occur before the count is known or written. A capacity probe can allocate or perform
native work, such as image encoding; it is not necessarily a cheap metadata query.

A query and a copy are separate observations. Mutable collections, files and service state may
change between them, so handle a new required count and retry where appropriate. Indexed listings
can have their own snapshot/refresh rule. CNA writes caller storage; it does not return a buffer
that the caller must free with an undocumented native allocator.

## Bulk transfer and ranges

Binary/typed data uses a pointer plus fixed-width count/capacity. Read each declaration's units:
bytes, pixels, samples, vertices and elements are not interchangeable. Nullable-empty buffers are
common, but not universal: packet receive requires positive byte capacity, while typed buffer
transfers have their own validated zero-window no-op rules.

Adapters check native size conversions and range arithmetic and report
`CNA_RESULT_OVERFLOW` when a requested size cannot be represented. Native `Int32` limits still
apply to many transfers even though the C count is `uint64_t`. A nonempty input must point to
the complete readable range, and a destination must actually hold its claimed capacity.

The adapter converts independently declared C values into native values; identical-looking
layouts do not authorize casting C arrays to C++ objects. Scratch allocation, native rendering and
I/O can still fail after validation. Failure atomicity beyond the documented output-buffer
guarantee is specific to each operation.

### Textures and graphics buffers

The convenience `cna_texture2d_set_data_rgba8` / `get_data_rgba8` routes transfer a full
level-zero image as `CNA_Color` elements. The separate `set_data_rgba8_bytes` route takes raw
RGBA8 bytes. These are no longer the whole texture surface: versioned transfers support mip
levels, rectangles and typed format identities. Cube/3D textures and vertex/index buffers have
their own face/volume/window descriptors. See [TEXTURES.md](TEXTURES.md),
[TEXTURE_VOLUMES.md](TEXTURE_VOLUMES.md), [VERTEX_BUFFERS.md](VERTEX_BUFFERS.md) and
[INDEX_BUFFERS.md](INDEX_BUFFERS.md) for alignment, format, range and renderer restrictions.

Readback stages native values and copies them only after the documented checks/native read succeed.
Backbuffer color readback reports logical width-times-height capacity and preserves caller pixels
on insufficient capacity or an unsupported native read.

### Sprite commands

`cna_sprite_batch_submit_many` takes a contiguous `CNA_SpriteCommand` array with a fixed stride
of `sizeof(CNA_SpriteCommand)` for this ABI. Each element must declare at least that size and
version 1; a larger `struct_size` does not change the array stride or authorize passing an array
of some other structure type.

The adapter resolves the whole command array, checks texture/device relationships and rejects
unknown effect bits before native submission. Non-finite rotation, origin and layer depth are
passed through under the native XNA behavior. It copies command values and retains the referenced
textures for the active interval; it does not retain the caller's command array. A native
submission failure can still occur after preflight. See [GRAPHICS_RESOURCES.md](GRAPHICS_RESOURCES.md).

### Input snapshots

Keyboard pressed-key output comes from a copied 256-bit snapshot, in ascending numeric order.
Insufficient capacity reports the full count without writing a prefix. Local snapshot queries do
not poll a platform device. Other input snapshots follow their own capture/consumption rules in
[INPUT_SNAPSHOTS.md](INPUT_SNAPSHOTS.md).

## Collections and borrows

Collections use count/copy, indexed POD/handle access, name/ID lookup or explicit enumerators.
Each family specifies whether it observes live values or a snapshot and whether a returned handle
is a new owned view, an existing protected token or a callback-only loan. A pointer into a borrowed
video frame or callback payload is not a size/copy result and follows its shorter validity window.

No native collection, iterator or allocator layout crosses the ABI.
`CnaCApiOutput.hpp` implements the shared copy helpers; `CApi_BoundaryDetail` and
`CApi_Utf8Oracle` test boundary/encoding rules, while resource and collection smoke tests cover
their individual range and lifetime contracts.
