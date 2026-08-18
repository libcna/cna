# glTF renderer stride conformance

Status: verified 2026-08-14 (`GLTF-158`…`GLTF-160`, `GLTF-168`, `GLTF-388`, `GLTF-389`).

This matrix records the complete boundary between CNA's glTF importer and the four renderers in
the glTF correctness gate. The source of truth is
`CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`; the importer now constructs a real
`VertexDeclaration` from that table before the first upload. A byte stride is no longer the only
description passed by the glTF path.

## Canonical layouts emitted by glTF

Every usage index is zero. `Color` is four normalized unsigned bytes and `Byte4` is four unsigned
integer bytes; every other format below is IEEE-754 `float`.

| Stride | Declaration order: semantic @ byte offset (`VertexElementFormat`) | Corpus witness |
|---:|---|---|
| 20 | Position @ 0 (`Vector3`), TextureCoordinate @ 12 (`Vector2`) | `tex-dual-texture-stride` |
| 24 | Position @ 0 (`Vector3`), Color @ 12 (`Color`), TextureCoordinate @ 16 (`Vector2`) | `normalized-u8-color` |
| 32 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), TextureCoordinate @ 24 (`Vector2`) | `mat-unlit` |
| 48 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), Tangent @ 24 (`Vector4`), TextureCoordinate @ 40 (`Vector2`) | `mat-authored-tangent` |
| 52 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), TextureCoordinate @ 24 (`Vector2`), BlendWeight @ 32 (`Vector4`), BlendIndices @ 48 (`Byte4`) | `skin-unlit` |
| 56 | The stride-52 declaration plus Color @ 52 (`Color`) | `skin-vertex-color` |
| 68 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), Tangent @ 24 (`Vector4`), TextureCoordinate @ 40 (`Vector2`), BlendWeight @ 48 (`Vector4`), BlendIndices @ 64 (`Byte4`) | `skin-parented-joints` |

Stride 16 (`Position` + `Color`) is also in the shared built-in table, but the current glTF
packing policy does not emit it; it is therefore outside this seven-stride matrix.

## Renderer matrix

“Draw boundary” means `Model::Draw` followed by `GraphicsDevice::Present`, so deferred Vulkan
commands are submitted rather than merely queued. “Native binding” means every declared element
is translated to an API input attribute. It is not meaningful for the two non-rasterizing
renderers, and is marked N/A rather than advertised as rendering support.

| Renderer | All seven uploads and public declarations | All seven reach draw boundary | Every element bound natively | Implementation/audit result |
|---|---|---|---|---|
| `STUB` | ✅ | N/A — `ThreeD=false`, explicit test skip | N/A | The common `VertexBuffer` retains and validates the complete declaration and exact upload bytes; STUB intentionally has no 3D pipeline. |
| `HEADLESS` | ✅ | ✅ validation draw | N/A | Upload/count/index validation and the stock-effect draw route accept all seven. Optional PBR/skinned base textures follow the native renderers' neutral-white contract. No pixels or native attributes exist. |
| `OPENGLES3` | ✅ | ✅ Mesa OpenGL ES 3.2 | ✅ | `EasyGLVertexBufferRenderer::ApplyLayout` takes the non-empty declaration path, binds one attribute per declaration element, maps `Color` to normalized `UNSIGNED_BYTE` and `Byte4` to the integer attribute path, then `RequireDeclarationFitsStockProgramEXT` checks the ordered semantic/format list against the selected shader. The old magic-stride switch remains an empty-declaration compatibility path and is no longer used by glTF. |
| `VULKAN` | ✅ | ✅ lavapipe/llvmpipe | ✅ | `VulkanVertexBufferRenderer` remembers the declaration and `RequireFaithfulDeclarationEXT` compares it with the canonical layout at draw time. The audited native tables bind `20` as P/UV, `24` as P/C/UV, `32` as P/N/UV, `48` as P/N/T/UV, `52` as P/N/UV/W/I, `56` as P/N/UV/W/I/C and `68` as P/N/T/UV/W/I, with `R8G8B8A8_UNORM` for Color and `R8G8B8A8_UINT` for BlendIndices. |

The Vulkan tables remain internal pipeline-selection details: they cannot silently reinterpret a
glTF buffer because the propagated declaration is checked first. EasyGL instead consumes the
declaration directly. STUB and HEADLESS keep honest non-rendering capability boundaries.

## Automated evidence

- `GltfStrideAndBuffer.EveryImportedGltfStrideCarriesItsCanonicalVertexDeclaration` loads the
  seven corpus witnesses and compares stride, element count, ordered offsets, formats, semantics
  and usage indices with the canonical table.
- `RendererStrideConformance.EveryGltfStrideReachesTheNativeDrawBoundary` draws and presents all
  seven on every renderer with `ThreeD`; STUB's capability skip is deliberate and checked in CI.
  Since `GLTF-473` its contract is **reach the draw boundary, or refuse with a named layout
  incompatibility** rather than a plain `EXPECT_NO_THROW` — which is stricter, because a
  fixed-function renderer's failure mode is not throwing.
- `RendererStrideConformance.AColourCarryingPbrPrimitiveEitherDrawsOrRefusesByName` (`GLTF-472`)
  and `RendererStrideConformance.NoPbrOrSkinnedRecordIsEverReadThroughAnIncompatibleLayout`
  (`GLTF-473`) draw the colour-carrying and the whole PBR/skinned record families through a **live
  device**. These are standing renderer-conformance gates, not artefacts of the glTF campaign that
  found the bugs, and the reason to keep them is empirical: between them they found three defects
  (`SDL_GPU`'s and `DILIGENT`'s unreachable `COLOR_0` shaders and `OPENGLES1`'s misread PBR records)
  that **every static inventory in this repository reported as correct**, and the search they
  prompted found two more (`GLTF-475`). A source audit can only see what a renderer declares; only a
  draw sees what it reads.
- `CrossRendererContract.NoRendererPaintsAVertexAttributeInsteadOfTheEffectsDiffuseColour`
  (`GLTF-475`) is the same tier applied one level lower: not "does the draw arrive?" but "is the
  answer the one the caller asked for?". It draws a plain XNA `VertexPositionNormalTangentTexture`
  buffer (stride 48) with a `BasicEffect` whose `DiffuseColor` is opaque red, then reads the centre
  pixel back on every renderer in the build. A renderer may render it -- the answer must be exactly
  that red -- or refuse it by name; a third answer is a defect. It found two, in two renderers,
  giving two different wrong colours: `OPENGL4` painted the record's `NORMAL` and `DILIGENT` drew
  black, where the plan row that opened the investigation had predicted one renderer. It needs a
  multi-renderer build (`CNA_MULTI_RENDERER`) and colour readback, so it is not in the CI matrix
  below; it runs in `cmake-build-multi`, which covers eight renderers with `ThreeD`.
- `FixedFunctionArrayLayout.*` (`GLTF-473`) pins the shared fixed-function layout guard's decision
  as a table over every canonical stride. It needs no device, so it runs in every build.
- `.github/workflows/gltf-renderer-stride-ci.yml` builds and runs those tests for `STUB`,
  `HEADLESS`, `OPENGLES3`, `VULKAN` and `SOFTWARE` on every relevant push and pull request.
  **`OPENGLES1` is not in that matrix and cannot be**: the runner's Mesa is built with `gles1`
  disabled, so the renderer compiles but cannot create a device. It is exercised locally against the
  side-by-side ES 1.1 Mesa build recorded in `NEXT_gltf.md`, which is where its evidence comes from.
- `scripts/gltf-renderer-parity.sh` compares each renderer against the same committed L1–L5
  goldens. The 2026-08-14 four-renderer run was byte-identical for all 42 selected L1–L5 tests;
  HEADLESS, OPENGLES3 and Vulkan also had identical outcomes for all 507 tests in `*Gltf*`.

This is an input-layout and submit oracle, not an image oracle. Corpus-wide pixel tolerances and
reference PNGs remain the separate L7 work tracked by `GLTF-009` and `GLTF-390`.
