# C Texture3D and TextureCube Contract

## Scope

`texture_volume.h` completes the C-native mapping of the public `Texture3D` and `TextureCube`
declarations. Both are creation-thread-affine, game-owned `CNA_Handle` resources. Their inherited
Texture and GraphicsResource state is available through the existing generic operations; C++
objects, renderer pointers and Sharp Runtime streams remain behind the ABI.

Version-one creation accepts the native Color format. Another known SurfaceFormat returns
`CNA_RESULT_NOT_SUPPORTED`; an unknown numeric identity is invalid. Dimensions must be positive,
fit native `int` limits and produce a representable complete level-zero element count. Texture3D
creation additionally preserves the native `GraphicsCapability::Texture3D` gate.

## Texture3D transfers

`CNA_Texture3DTransfer` selects a mip level, an exclusive-bound box and a caller-array window.
The mip dimensions halve independently to a minimum of one texel. Validation completes before
native access: the level and box must be in range, `element_count` must cover the box, and the
caller capacity must cover `start_index + element_count`.

`cna_texture3d_set_data` and `cna_texture3d_get_data` map all Color overloads. Only the exact box
voxel count is copied; unused array prefix and tail elements are not read or written. Readback
uses native scratch storage and copies to the caller only after complete success.
`cna_texture3d_set_data_bytes` is the direct form of `SetDataPointerEXT`; for the current Color
contract its byte count must equal four times the requested voxel count.

HEADLESS and SDL_RENDERER report no real volume storage. Their C creation route therefore returns
`CNA_RESULT_NOT_SUPPORTED` with an invalid output handle instead of manufacturing a usable
Texture3D. Renderers that advertise the capability remain responsible for accepting or explicitly
refusing each mip/box operation.

## TextureCube transfers and DDS

TextureCube creation may succeed even when the backend has no cube-map storage, matching CNA's
native construction contract. `CNA_TextureCubeTransfer` selects one of all six cube faces, a mip
level, an optional rectangle and a caller-array window. Set/Get calls then either transfer the
complete region or return `CNA_RESULT_NOT_SUPPORTED`; failed readback never changes the caller's
destination. A RenderTargetCube handle is accepted by the query and transfer routes because it is
a native TextureCube subclass, but only `cna_render_target_destroy` may release that concrete
handle.

`cna_texturecube_create_from_dds_memory` copies a complete caller-owned DDS payload into a native
MemoryStream for the duration of `DDSFromStreamEXT`. No stream object crosses the ABI. Decoding
still fails with `NOT_SUPPORTED` when the selected renderer cannot store all decoded faces.

## Ownership and verification

Successful Texture3D and TextureCube factories create game children that must be destroyed before
the game. Typed destroy validates the exact concrete kind, invalidates the generation-tagged
handle and rejects stale, wrong-kind and wrong-thread use. Explicit generic disposal keeps the C
handle alive for disposal-state queries until typed destruction.

`TextureVolumeSmoke.c` is strict C17 and runs unchanged under HEADLESS and SDL_RENDERER. It covers
all six faces, full/mip/rectangle validation, array capacity and atomicity, exact native type text,
DDS input, common Texture and GraphicsResource routes, RenderTargetCube inheritance, disposal,
parent order and invalid/stale/wrong-kind/wrong-thread failures. C17 and C++23 compile-time
assertions freeze every version-one descriptor size and offset; ASan+UBSan covers the same runtime
contract.
