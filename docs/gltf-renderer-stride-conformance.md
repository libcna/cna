# glTF renderer stride conformance

Current source/test contract checked on 2026-11-01. Historical GLTF-158…389 campaign results
remain in Git history; the table below separates declaration, draw and pixel evidence.

This page records the canonical importer layouts and the focused renderer-conformance boundary. The source of truth is
`CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`; the importer now constructs a real
`VertexDeclaration` from that table before the first upload. A byte stride is no longer the only
description passed by the glTF path.

## Canonical layouts emitted by glTF

Every usage index is zero except the second `TextureCoordinate`, which is usage index 1. `Color` is
four normalized unsigned bytes and `Byte4` is four unsigned integer bytes; every other format below
is IEEE-754 `float`. The importer selects a row through `VertexLayoutTableEXT` (`GLTF-099`).

| Stride | Declaration order: semantic @ byte offset (`VertexElementFormat`) | Corpus witness |
|---:|---|---|
| 24 | Position @ 0 (`Vector3`), Color @ 12 (`Color`), TextureCoordinate @ 16 (`Vector2`) | `mat-unlit-vertex-color-alpha` |
| 28 | Position @ 0 (`Vector3`), TextureCoordinate @ 12 (`Vector2`), TextureCoordinate1 @ 20 (`Vector2`) | — |
| 32 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), TextureCoordinate @ 24 (`Vector2`) | `mat-unlit`, `tex-dual-texture-stride` |
| 48 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), Tangent @ 24 (`Vector4`), TextureCoordinate @ 40 (`Vector2`) | `mat-authored-tangent` |
| 52 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), TextureCoordinate @ 24 (`Vector2`), BlendWeight @ 32 (`Vector4`), BlendIndices @ 48 (`Byte4`) | `skin-unlit` |
| 56 | The stride-52 declaration plus Color @ 52 (`Color`) | `skin-vertex-color` |
| 60 | The stride-48 declaration plus TextureCoordinate1 @ 48 (`Vector2`) and Color @ 56 (`Color`) | `uv1-material`, `normalized-u8-color`, `mat-vertex-color-pbr` |
| 68 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), Tangent @ 24 (`Vector4`), TextureCoordinate @ 40 (`Vector2`), BlendWeight @ 48 (`Vector4`), BlendIndices @ 64 (`Byte4`) | `skin-parented-joints` |
| 76 | The stride-68 declaration plus TextureCoordinate1 @ 68 (`Vector2`) | — |
| 80 | The stride-76 declaration plus Color @ 76 (`Color`) | `skin-vertex-color-pbr` |

Strides 16 (`Position` + `Color`) and 20 (`Position` + `TextureCoordinate`) are also in the shared
built-in table, but the current glTF packing policy emits neither. The renderer matrix and the
first two tests below cover the six strides that the witness test loads (32, 48, 52, 56, 60 and
68); strides 24, 28, 76 and 80 have no witness there. Since CNA-FIX-D07, `tex-dual-texture-stride`
(an unlit material with a fallback occlusion map) stays BasicEffect's stride 32, so no corpus
fixture selects the dual-UV stride 28.

## Declaration and draw boundary

The public declaration is authoritative. Some native routes translate it directly; others use a
fixed layout plus the shared fidelity guard. A stride alone cannot prove semantic/offset/format
compatibility. The guard requires declared fields to fit, not overlap, and match their actual
native interpretation; an extra unused native attribute is not automatically a violation.
Unknown layouts retain their route's explicit refusal policy.

EveryCanonicalStrideHasExactlyItsDeclaredElements covers the table itself. The eight-witness
declaration test covers six emitted strides, not every row. The two COLOR_0 fixtures exercise
60/80 at the live draw boundary; EveryPbrAndSkinnedFixtureImportsToItsCanonicalStride provides
broader import coverage. These distinct tests must not be collapsed into “all layouts rendered”.

Model.Draw followed by Present reaches deferred submission. The current ordinary-stride test
accepts success or a named absent-shading-model refusal containing GLTF-477. The color test also
accepts a COLOR_0 refusal. Other generic layout errors do not satisfy those allowances.
A passing test therefore needs its capability/refusal context before it can be called rendering
support.

## Current execution evidence

On 2026-11-01 the existing multi tree builds the Content/Graphics test targets and passes the
42-case source-policy/buffer filter. The three RendererStrideConformance cases have these results:

| Selected identity | Result | Meaning |
|---|---|---|
| OPENGLES3, OPENGL33 | 3/3 each | Current private Linux native GL draw routes |
| WEBGPU, SDLGPU, FNA3D | 3/3 each | Current private Linux device draw routes; not all drivers/OSes |
| SOFTWARE | 3/3 | CPU implementation/submit route |
| HEADLESS | 3/3 | Upload, declaration and validation draws; no native input binding or pixels |
| STUB | 0 passed, 3 capability skips | No 3D; upload/declaration tests are separate |
| DIRECTX9, WEBGL2 | Not run here | Not compiled into this multi tree / requires browser build |

The focused cases are not corpus-wide image oracles. The cross-renderer
NoRendererPaintsAVertexAttributeInsteadOfTheEffectsDiffuseColour case also passes, checking a
stride-48 BasicEffect red witness where 3D/readback is available; it permits diagnostic refusal and
does not judge HEADLESS pixels.

## CI and independent layers

.github/workflows/gltf-renderer-stride-ci.yml currently selects STUB, HEADLESS, OPENGLES3,
SDLGPU, WEBGPU and SOFTWARE. This is six identities, not every public identity or all native
drivers. Its source describes the configured gate; a workflow file is not a newly successful
remote run. Run local window/device tests through the private runner:

```sh
tools/platform/run_gpu_tests_private.sh --exec cmake-build-multi/CnaContentTests \
  '--gtest_filter=GltfStrideAndBuffer.*:RendererStrideConformance.*'
```

scripts/gltf-renderer-parity.sh compares selected L1–L5 oracle results; the retained 2026-08-14
42/507-case numbers are historical evidence. L7 pixels and per-policy goldens are separate:
see [conformance harness](gltf-conformance.md) and [PBR contract](gltf-renderer-pbr-fallbacks.md).
