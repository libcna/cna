# FNA3D renderer — stock effect binaries

## Why these files exist

`FNA3D.h` exposes exactly one way to get a shader onto the GPU: `FNA3D_CreateEffect()`, which takes
a **compiled Direct3D 9 Effect Framework binary** and runs it through MojoShader. There is no
"compile this GLSL/HLSL string" entry point anywhere in the library, and the OpenGL driver refuses
to draw without a bound MojoShader program. A renderer built on FNA3D therefore cannot author its
own shaders the way CNA's Vulkan/SDL_GPU/sokol families do — it must supply `.fxb` blobs.

These are the XNA 4.0 stock effects, exactly the artefacts FNA itself ships and loads through the
same `FNA3D_CreateEffect()` call. Using them is not a shortcut around writing shaders: it is how
FNA3D is designed to be consumed, and it is what makes this the one CNA renderer that executes
*XNA's own* `BasicEffect`/`SkinnedEffect`/… shader programs rather than a reimplementation of them.

## Provenance

| Field | Value |
|---|---|
| Upstream project | [FNA-XNA/FNA](https://github.com/FNA-XNA/FNA) |
| Upstream path | `src/Graphics/Effect/StockEffects/FXB/` |
| Pinned revision | `30a427365a1684d6599329560efcfb90233701a9` (2026-08-06) |
| License | Microsoft Permissive License (Ms-PL) — `LICENSE.StockEffects`, copied verbatim from `src/Graphics/Effect/StockEffects/LICENSE` |
| Original source | Microsoft's XNA Game Studio stock effect `.fx` sources, released under Ms-PL; `.fxb` are their `fxc`-compiled form |

CNA is itself MS-PL (`LICENSE`), so these blobs are redistributed under the same license they
arrived with. The attribution also appears in the repository-level `THIRD_PARTY_NOTICES.md`.

| File | Bytes | SHA-256 | Techniques × passes | Shader variants |
|---|---:|---|---|---|
| `SpriteEffect.fxb` | 1104 | `ebed64c8…d8c56` | `SpriteBatch` × 1 | 1 |
| `BasicEffect.fxb` | 28824 | `b3cedbb9…c0b7` | `BasicEffect` × 1 | 32, selected by `ShaderIndex` |
| `AlphaTestEffect.fxb` | 5592 | `6db69651…a48` | `AlphaTestEffect` × 1 | 8, selected by `ShaderIndex` |
| `DualTextureEffect.fxb` | 4680 | `e3c58149…98025` | `DualTextureEffect` × 1 | 4, selected by `ShaderIndex` |
| `EnvironmentMapEffect.fxb` | 10968 | `3b66dc78…5e8395` | `EnvironmentMapEffect` × 1 | 16, selected by `ShaderIndex` |
| `SkinnedEffect.fxb` | 55532 | `933a315f…c94931` | `SkinnedEffect` × 1 | 18, selected by `ShaderIndex` |

Technique/parameter inventories above were measured, not assumed — `fna3d-spike/` prints them for
each blob through the same MojoShader build the renderer links.
`Fna3dCompiledEffectTest.StockFixtureHashesMatchDocumentedFnaRevision` pins and verifies each full
digest in CI; the shortened values above are for readability.

## Why they are committed rather than fetched

Every stock effect carries exactly **one technique with one pass**; the shader variant is chosen by
an `int ShaderIndex` parameter plus the effect's own `VSIndices`/`PSIndices` arrays, which is XNA's
own dispatch mechanism. That makes the blobs behavioural contract, not build input: the renderer's
`ShaderIndex` arithmetic is only correct against these exact artefacts. Committing them (106 KB
total) keeps that pairing reproducible and keeps the renderer buildable offline, whereas fetching
FNA at configure time would add a second network dependency for six small files.

They are **not** rebuildable from source in this repository: compiling `.fx` → `.fxb` requires
`fxc` from the DirectX SDK, which exists on no platform CNA builds on. The HLSL sources remain
readable upstream at `src/Graphics/Effect/StockEffects/HLSL/`.

## Generated header

`embed_effects.py` turns these files into `Fna3dStockEffectBlobs.hpp` (`constexpr unsigned char`
arrays) at build time — the module's `CMakeLists.txt` drives it. The generated header is a build
artefact and is not committed; only these inputs are.
