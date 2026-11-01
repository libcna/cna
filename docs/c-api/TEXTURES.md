# C Texture and Texture2D Contract

## Scope

`texture.h` exposes common `Texture` queries and `Texture2D` creation/transfer/encoding operations;
`graphics.h` also contains the full-image Color convenience routes. Texture objects
remain validated, creation-thread-affine handles; no C++ object, renderer pointer, weak pointer or
Sharp Runtime stream crosses the ABI.

Default, file-only and CPU-only factories create standalone handles without an owning device.
Device, file-with-device, RGBA8 and encoded-memory factories accept an owned device or a valid
game-device loan, including its pre-run window. Release them before that device/game. They may
survive the callback that creates them; independently owned device resources are not game children.
RenderTarget2D continues to satisfy the common Texture/Texture2D query and transfer routes because
that is its native inheritance relationship.

## Common texture state and helpers

`cna_texture_get_info` returns the native format and allocated mip-level count. The static helper
family exposes block area, format-unit byte size, pixel-store alignment, destination element-size
validation for every stable SurfaceFormat identity. `cna_texture_validate_format` is deliberately
the renderer-independent Color-only base gate; the device-aware format query is separate.
Outputs are assigned only after argument and handle validation.

Width, height and bounds remain in the existing `cna_texture2d_get_info` snapshot. Exact native
type text uses count/copy operations without a terminator. `cna_texture2d_get_storage_info` reports
only renderer and CPU-shadow availability; native renderer/shared/weak pointers deliberately stay
private.

## Typed transfers

All native SetData/GetData overloads collapse to `cna_texture2d_set_data` and
`cna_texture2d_get_data`. `CNA_Texture2DTransfer` selects a mip level, an optional texel rectangle
and a window in the caller array. `start_index` is an index in that caller array. `element_count`
is the capacity passed to the corresponding native overload, while the function-level capacity
must cover `start_index + element_count`.

The fixed data identities map as follows:

| C data identity | Caller element representation | Typical format/use |
|---|---|---|
| `COLOR` | `CNA_Color` | Color-compatible RGBA-shaped format |
| `BGR565`, `BGRA5551`, `BGRA4444` | Matching 16-bit `CNA_Packed*` value | Matching packed format |
| `BYTE` | `uint8_t` | ByteEXT, byte views of larger texels, or supported compressed blocks |
| `NORMALIZED_BYTE2`, `NORMALIZED_BYTE4` | Matching `CNA_Packed*` value | Matching packed format |
| `RGBA1010102`, `RG32`, `RGBA64`, `ALPHA8` | Matching `CNA_Packed*` value | Matching packed format |
| `SINGLE` | `float` | Single |
| `VECTOR2`, `VECTOR4` | `CNA_Vector2`, `CNA_Vector4` | Vector2, Vector4 |
| `HALF_SINGLE`, `HALF_VECTOR2`, `HALF_VECTOR4` | Matching `CNA_Packed*` value | Matching half format; HalfVector4 also maps HdrBlendable |
| `USHORT` | `uint16_t` | UShortEXT |

These identities describe element representations, not a general pixel-format conversion service.
Native element-size divisibility and transfer restrictions determine compatibility; an uncompressed
region can use smaller elements that evenly divide a texel. For example, a Color region requires
four BYTE elements per texel, and BYTE upload/readback round-trips its RGBA bytes. A compressed byte
transfer requires the padded block count multiplied by the format block size. Range, mip and rectangle checks
run before the native operation. `element_count` may exceed the required region count; unused
caller elements are preserved. Too small a descriptor count is `CNA_RESULT_INVALID_ARGUMENT`,
while insufficient function-level array capacity is `CNA_RESULT_BUFFER_TOO_SMALL`. Readback reports
the requirement once the region can be computed, including the too-small descriptor-count failure;
earlier descriptor/handle failures can leave that output untouched. Failed readback writes no
destination prefix. Conversions use byte copies rather than assuming caller
alignment or C/C++ object-layout equivalence.

`cna_texture2d_set_data_rgba8_bytes` is the direct C form of the native `SetDataRGBA` route. It
accepts exactly `width * height * 4` caller-owned RGBA8 bytes for level zero; size or format
failure leaves the texture unchanged.

Surface-format creation and successful typed transfer remain native-renderer capabilities.
HEADLESS's native creation path is Color-only; support on other renderers must be queried rather
than inferred from an enum's existence.

## Images, files and streams

Sharp Runtime `Stream` never appears in a public signature. Encoded input is a caller-owned byte
block copied through a native `MemoryStream` during the call. A null decode descriptor preserves
the source size; a versioned descriptor selects the native fit or cover-and-crop overload.

PNG and JPEG stream outputs use count/copy operations with target dimensions. Capacity failure
reports the complete encoded size and performs no partial write. File outputs accept validated,
length-delimited UTF-8 paths and invoke the matching native filename overload. The file-only and
file-with-device constructors cover both native path constructors.

A size probe encodes the image too; size and copy can each allocate and encode. Input image bytes
and output paths are not retained after the call. Encoding/decoding failures retain their ordinary
argument, I/O, allocation or renderer results rather than guaranteeing an image on every platform.

## Retention and disposal

SpriteBatch intervals, fonts, effect parameters, models and render-target scopes can retain a
Texture2D and block typed destruction. Release those relationships first. Generic disposal leaves
the handle available for disposal queries and typed cleanup; it does not release its parent child
count. A RenderTarget2D uses its concrete render-target destroy route. See
[GRAPHICS_RESOURCES.md](GRAPHICS_RESOURCES.md) and [OWNERSHIP.md](OWNERSHIP.md).

## Evidence

`CApi_TextureSmoke` is strict C17. It checks format helpers, transfer identity dispatch, Color and
BYTE views, mip/rectangle/caller windows, factories, PNG/JPEG round trips and resize modes, plus
capacity and handle failures. `CApi_ContentSmoke` supplies a separate compiled-content path.
Actual non-Color successes depend on the renderer/profile exercised. C17/C++23 header targets pin
public layouts and identities; historical results from other renderers or sanitizers are not a
substitute for the current run.
