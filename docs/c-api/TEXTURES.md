# C Texture and Texture2D Contract

## Scope

`texture.h` completes the C-native mapping of the public `Texture` and `Texture2D` declarations.
It extends the earlier level-zero slice without changing those entry points. Texture objects
remain validated, creation-thread-affine handles; no C++ object, renderer pointer, weak pointer or
Sharp Runtime stream crosses the ABI.

Texture2D handles may be standalone or game children. Default, file-only and CPU-only factories
create standalone handles and report an invalid owning graphics device. Device, file-with-device,
RGBA8 and encoded-memory factories create game children that must be destroyed before their game.
RenderTarget2D continues to satisfy the common Texture/Texture2D query and transfer routes because
that is its native inheritance relationship.

## Common texture state and helpers

`cna_texture_get_info` returns the native format and allocated mip-level count. The static helper
family exposes block area, format-unit byte size, pixel-store alignment, destination element-size
validation and the renderer/profile-aware creation gate for every stable SurfaceFormat identity.
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

| C data identity | Caller element representation | Native format contract |
|---|---|---|
| `COLOR` | `CNA_Color` | Color-compatible RGBA-shaped format |
| `BGR565`, `BGRA5551`, `BGRA4444` | Matching 16-bit `CNA_Packed*` value | Matching packed format |
| `BYTE` | `uint8_t` | ByteEXT or a renderer-supported compressed block stream |
| `NORMALIZED_BYTE2`, `NORMALIZED_BYTE4` | Matching `CNA_Packed*` value | Matching packed format |
| `RGBA1010102`, `RG32`, `RGBA64`, `ALPHA8` | Matching `CNA_Packed*` value | Matching packed format |
| `SINGLE` | `float` | Single |
| `VECTOR2`, `VECTOR4` | `CNA_Vector2`, `CNA_Vector4` | Vector2, Vector4 |
| `HALF_SINGLE`, `HALF_VECTOR2`, `HALF_VECTOR4` | Matching `CNA_Packed*` value | Matching half format; HalfVector4 also maps HdrBlendable |
| `USHORT` | `uint16_t` | UShortEXT |

An uncompressed region requires `width * height` elements. A compressed byte transfer requires
the exact padded block count multiplied by the format block size. Range, mip and rectangle checks
run before the native operation. Readback first reports the exact region requirement; insufficient
capacity writes no destination prefix. Conversions use byte copies rather than assuming caller
alignment or C/C++ object-layout equivalence.

`cna_texture2d_set_data_rgba8_bytes` is the direct C form of the native `SetDataRGBA` route. It
accepts exactly `width * height * 4` caller-owned RGBA8 bytes for level zero; size or format
failure leaves the texture unchanged.

Surface-format creation and successful typed transfer remain native-renderer capabilities.
HEADLESS and SDL_RENDERER currently create Color textures only. HEADLESS exercises higher mip
uploads; SDL_RENDERER has no native mip-level upload path, so a compatible level-above-zero upload
returns `CNA_RESULT_NOT_SUPPORTED` before changing the CPU shadow. The Skia build exposes its
larger promoted native format set, but that matrix does not yet have C-only runtime evidence.

## Images, files and streams

Sharp Runtime `Stream` never appears in a public signature. Encoded input is a caller-owned byte
block copied through a native `MemoryStream` during the call. A null decode descriptor preserves
the source size; a versioned descriptor selects the native fit or cover-and-crop overload.

PNG and JPEG stream outputs use count/copy operations with target dimensions. Capacity failure
reports the complete encoded size and performs no partial write. File outputs accept validated,
length-delimited UTF-8 paths and invoke the matching native filename overload. The file-only and
file-with-device constructors cover both native path constructors.

## Evidence

`TextureSmoke.c` is compiled as strict C17 and runs unchanged under HEADLESS and SDL_RENDERER. It
covers all 27 format helpers/creation gates, dispatch and rejection for all 18 data identities,
successful Color full/rectangle transfers, HEADLESS mip transfer, SDL's explicit mip limitation,
every public factory/query/encoding route, PNG/JPEG signatures and file round-trips, both decoded
resize modes, storage/type text, capacity atomicity and invalid/stale/wrong-kind/wrong-thread
handles. The same strict-C test now also runs on EasyGL OPENGLES3, where native-supported non-Color
formats pass the device-aware creation gate. `ContentSmoke.c` independently proves exact two-level
`NormalizedByte2` format and payload preservation through the XNB content route on that renderer.
C17 and C++23 assertions freeze every new public structure and identity endpoint. The same focused
test passes under ASan+UBSan; LeakSanitizer is disabled only in the ptrace-constrained test
environment where the leak checker itself cannot attach.
