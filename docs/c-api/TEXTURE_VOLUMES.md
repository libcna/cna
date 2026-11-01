# C Texture3D and TextureCube Contract

## Creation and identity

`texture_volume.h` exposes owned, generation/kind/thread-validated Texture3D and TextureCube
handles. Creation accepts an independently owned device or a valid game-device loan, including
before the first run. Release resources before the owning device/game. They are not necessarily
game children.

The ordinary creation descriptors currently accept Color only; another known SurfaceFormat returns
`CNA_RESULT_NOT_SUPPORTED`, and an unknown identity is invalid. Positive dimensions and complete
level-zero element counts must fit the adapter's native Int32 boundary. Native profile, dimension,
format and renderer gates apply too: volume textures require HiDef and Texture3D support.

Common Texture queries and applicable GraphicsResource operations accept both types. A
RenderTargetCube also satisfies cube query/transfer operations, but its concrete handle must be
released with `cna_render_target_destroy`. Common disposal and device-query limitations are in
[GRAPHICS_RESOURCES.md](GRAPHICS_RESOURCES.md).

## Texture3D transfers

`CNA_Texture3DTransfer` selects a mip level, exclusive box bounds and a window in a caller
`CNA_Color` array. Each mip dimension halves to a minimum of one. The box must be nonempty and
inside that mip; `element_count` must cover its voxel count, and caller capacity must cover
`start_index + element_count`.

Set/Get transfer the exact box, preserving unused caller prefix/tail values. Readback stages native
Color values and copies only after success. It reports the box requirement after descriptor
validation and before checking the caller array, so earlier descriptor/handle failures need not
write the count.

`cna_texture3d_set_data_bytes` maps the native pointer extension for that box. It requires a
non-null source, exactly four bytes per voxel, an Int32-representable byte count and
`start_index == 0`. The descriptor's array element-count field is not used by this byte route;
box/mip validation still applies.

HEADLESS does not provide volume storage; its creation path returns
`CNA_RESULT_NOT_SUPPORTED` with an invalid output token. A renderer capability is a preflight,
not a promise that every profile, size, format and mip/box operation will succeed.

## TextureCube transfers and DDS

The cube descriptor selects one of six faces, mip level, optional rectangle and caller-array
window. Current C Set/Get routes use `CNA_Color`; they do not expose the native generic byte or
packed-format cube overloads. Capacity, region and format validation precede caller-output writes,
and failed readback preserves caller memory. Required-count publication follows descriptor
validation, as for Texture3D.

Native cube construction can succeed without a usable renderer allocation on some paths; subsequent
face transfers then return `CNA_RESULT_NOT_SUPPORTED`. Do not infer storage from successful handle
creation alone.

`cna_texturecube_create_from_dds_memory` borrows the encoded bytes during the call and decodes via
a native MemoryStream. Supported DDS source formats are CPU-decoded to RGBA8, including DXT
decompression, and every face/mip is uploaded as `SurfaceFormat::Color`. The source's compressed
or non-Color storage format is not retained. The selected renderer must support the resulting
Color cube and mip chain; this factory does not add a raw compressed-transfer route.

## Retention and release

Effect texture parameters retain volume/cube resources and block typed destruction until the
parameter reference is cleared or released. Generic resource disposal has corresponding retention
guards. Typed destroy checks the concrete resource kind and releases its owning game/device
dependency. Content-created cube handles also follow the content manager's load/disposal contract
in [CONTENT.md](CONTENT.md).

Explicit generic disposal leaves the C token available for disposal queries and final typed
cleanup; it does not promise usable renderer storage afterwards.

## Verification

`CApi_TextureVolumeSmoke` covers face/box/mip validation, capacity and output preservation, DDS,
generic queries, RenderTargetCube inheritance, disposal and lifetime failures. Unsupported renderer
paths are asserted rather than reported as successful storage. The C17/C++23 header targets pin
descriptor sizes and offsets. These checks must be interpreted for the renderer actually run;
historical HEADLESS or sanitizer results do not establish all current GPU paths.
