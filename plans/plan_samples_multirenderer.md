# Native Sample Multi-Renderer Qualification Plan

**Started:** 2026-10-04 on CNA branch `samples` and cna-samples branch `develop`.

**Scope:** qualify the existing cna-samples gallery corpus with one executable per sample and CNA's
existing runtime renderer selection.  Linux targets are `OPENGLES3`, `OPENGL33`, `VULKAN`,
`WEBGPU`, `SDL_GPU` and `FNA3D`; `OPENGLES2` has its own truthful capability subset.  Windows and
macOS work in this Linux phase is preparatory and must not be reported as native qualification.

All compilation in this campaign uses at most 12 parallel jobs.  GPU/window execution uses
`tools/platform/run_gpu_tests_private.sh`, never the owner's live display.

## Starting state

- CNA: `b0e97bb1bb876f9b3edd6f4ff1ef3067908ae8ac`
- cna-samples: `5db32e6a2631f216e85082b1c390321548924aa2`
- cna-samples manifest: 89 completed public-gallery executables plus the two completed native-only
  ports `ClientServerSample` and `NetRumble`, for a 91-sample primary corpus.

## Work items

| ID | Status | Work |
|---|---|---|
| MSR-001 | **DONE** | Route `SurfaceFormat::Color` byte/generic `TextureCube` and `Texture3D` transfers through the canonical RGBA8 renderer path. |
| MSR-002 | **DONE** | Restore the `SDL_GPU` + compiled-XNA-effect build and its focused tests (`SMG-0043`). |
| MSR-003 | **OPEN** | Repair compiled-effect passes that bind no shader pair, exposed by `BloomSample` on Vulkan, WebGPU and SDL_GPU. |
| MSR-004 | **OPEN** | Repair compiled-effect texture-parameter indexing, exposed by `ShadowMapping` on Vulkan and WebGPU. |
| MSR-005 | **OPEN** | Classify `LensFlare` occlusion-query limits truthfully for SDL_GPU and FNA3D's selected internal driver; never fabricate a query result. |
| MSR-006 | **IN PROGRESS** | Complete single-renderer and six-renderer Linux CNA qualification. |
| MSR-007 | **IN PROGRESS** | Qualify the representative sample set, then the complete 91-sample corpus. |
| MSR-008 | **OPEN** | Prepare and cross-build the Windows renderer set; record Wine evidence separately from native qualification. |
| MSR-009 | **OPEN** | Prepare the macOS renderer set and exact Mac mini M4 qualification commands. |

## MSR-001 — Color byte-transfer routing

`TextureCubeReader` uploads an uncompressed XNB cube as byte arrays, face by face and mip by mip.
The shared `TextureCube::SetTypedDataBytesEXT` route sent even `SurfaceFormat::Color` through the
new optional declared-format hook.  EasyGL and Vulkan implement that hook, while FNA3D, SDL_GPU and
WebGPU correctly implement their established RGBA8 `SetData` route; the sample therefore failed on
those three with “did not store the complete declared-format cube region.”  `Texture3D` had the
same routing defect, and both readback paths had the symmetric problem.

For `SurfaceFormat::Color`, byte/generic set and get operations now use the canonical renderer
`SetData`/`GetData` route.  Other uncompressed formats continue to require the exact
`SetDataBytesEXT`/`GetDataBytesEXT` contract, so the change does not reinterpret packed, float or
normalized formats.

Regression evidence:

- reproduced before the fix with two `TextureCubeTest` and two `Texture3DTest` generic/byte cases;
- those four cases pass after the fix on SDL_GPU and OPENGLES3;
- new `TextureCubeTest.ColorByteTransfersPopulateEveryFaceAndMipLikeTextureCubeReader` exercises
  all six faces and every mip using the XNB reader's byte-array shape; it passes on SDL_GPU and
  OPENGLES3;
- the same `ReachGraphicsDemo` executable now starts and remains stable on all six Linux target
  renderers, with the active renderer verified in each process.

## Representative automated matrix

This is startup/active-renderer/stability evidence, not a manual visual pass.  Before `MSR-001`,
the ten-sample matrix was:

| Renderer | Automated pass | Render fail |
|---|---:|---:|
| OPENGLES3 | 10 | 0 |
| OPENGL33 | 10 | 0 |
| VULKAN | 8 | 2 |
| WEBGPU | 7 | 3 |
| SDL_GPU | 7 | 3 |
| FNA3D | 8 | 2 |

After `MSR-001`, `ReachGraphicsDemo` moves from failure to automated pass on FNA3D, WebGPU and
SDL_GPU.  The full ten-sample matrix will be regenerated after the remaining compiled-effect
repairs so intermediate evidence is not mistaken for final qualification.
