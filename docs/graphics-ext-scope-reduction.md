# Graphics extension scope reduction (MOD-RETIRE-1)

This is the dependency classification made before the deletion pass. The source of truth for
surviving APIs is the code and the validation results below, not the historical engine plan.

## A. Retain

- `AsciiPostProcessEffect`, `AsciiQuantizeMode`, and the internal ASCII atlas and quantizer.
- `CRTEffect`, `CRTMaskType`, `DepthEffect`, `DepthEffectMode`, and `DitherMode`.
- `DebugDraw` as a standalone BasicEffect and primitive drawing utility.
- Core XNA Effect and EffectPass, ShaderEffect/custom shaders, PbrEffect/SkinnedPbrEffect,
  texture/render-target APIs, all renderer backends, glTF import, CNB/CNJ, and the content pipeline.
- C routes and tests for the retained ASCII, CRT, and colour-depth effects.

## B. Remove

- The graphics-ext render pipeline and settings; post-process chain, passes and pools; HDR,
  exposure, LUT, sky, transparency and fog helpers; clustered lighting, probes, shadow maps,
  culling/LOD, particles, engine compute/resources/timers, area-light and thin-film helpers.
- Shader-generation scripts dedicated to the removed engine shaders; the retained
  CRT/Depth HLSL generator no longer contains LUT or SSR special cases.
- The optional engine material layer: PbrMaterial, PbrMaterialExtensions, MaterialBinding,
  GltfMaterialBridge, and their C routes. The ordinary glTF importer constructs core PBR effects
  independently of this bridge.
- DebugGizmos, whose API uses removed engine light, probe, and shadow types.
- Engine-only tests, examples, shaders, generated data, and current documentation claims.

## C. Analyze by consumer before removal

- ShaderCodeEXT/ShaderPackageEXT (including ShaderBindingTypeEXT,
  ShaderBindingRequirementEXT and ShaderPackageSelectionEXT) and the small CRT/DepthEffect
  package factory: retained effects use these to select portable shader variants. Core
  ShaderEffect also uses their overloads.
- Renderer compute, storage, cube textures, render targets, shader dialects, and PBR code:
  retain generally exposed capabilities and core/custom-shader consumers; remove only proven
  engine-only policy and tests.
- C API object kinds, ABI inventory, binding metadata, umbrella headers, and module probes:
  prune engine identities while keeping retained effect routes and unrelated extensions.

## DebugGizmos future intent

DebugGizmos was removed together with the previous modern graphics-engine layer because its API
depended directly on engine-specific types. A lightweight replacement may be reimplemented in the
future, but not as part of the current scope-reduction work. The future version should be built on
top of DebugDraw and should avoid dependencies on engine-specific types. It should accept generic
geometric data and parameters instead, such as positions, directions, ranges, angles, matrices,
frusta and colors.

## Resulting ownership boundary

The graphics-extension module now owns only the direct ASCII/CRT/colour-depth effects,
standalone DebugDraw, and the portable shader-package values used by those effects and the
core ShaderEffect package overload. The module has no render-pipeline or post-process-chain
adapter. The XNA `EffectPass` and `EffectPassCollection` remain in `modules/graphics`.

The normal glTF runtime path in `ContentManager` constructs `PbrEffect` or
`SkinnedPbrEffect` and applies glTF material factors/maps directly. Its importer does not
include the removed `GltfMaterialBridge`. CNB maintains its own `CnbMaterial` data and the
`PbrEffect`/`SkinnedPbrEffect` effect kinds; CNJ-to-CNB maps those names independently.

Renderer production implementations were left intact. Their compute/storage, texture-array,
render-target, cube-map, shader-dialect, indirect-draw, PBR and timestamp operations are
renderer capabilities below the removed engine policy. Removing the backend implementations
would alter core renderer contracts and custom-shader/PBR behavior. Two core graphics
exemptions used only by the old engine were also removed: the float-filtering bypass and the
oversized render-target scope. XNA profile rules now apply uniformly. Engine-only renderer
examples and tests that instantiated deleted graphics-extension resources were removed.

## Graphics-extension line count

Counts include nonblank and blank physical lines in `modules/graphics-ext/{include,src,tests,examples}`;
`*.generated.*` files form the generated category.

| Category | Before | After |
|---|---:|---:|
| Handwritten production | 44,781 | 3,462 |
| Generated production | 27,489 | 1,813 |
| Tests | 39,319 | 2,096 |
| Generated tests | 2,190 | 0 |
| Examples | 11,583 | 1,357 |
| Generated examples | 587 | 0 |
| Total | 125,949 | 8,728 |

## Validation (2026-09-27)

- `CNA_CNAEXT=ON`, OPENGLES3: full build passed; 607/607 selected CNB, CNJ, glTF and
  content-pipeline tests passed; 78/78 retained-effect and graphics-profile tests, 4/4 ASCII
  examples, and 17/17 focused C API tests passed in the private GPU runner.
- `CNA_CNAEXT=OFF`, OPENGLES3: `CnaTests` and C API smoke targets built; 606/607 selected
  content tests passed in parallel. The sole failure was a CLI log-comparison test that saw
  another test's temporary staging directory; that test passed alone (1/1). Focused core
  profile tests and C API smoke tests passed (28/28 and 4/4).
- Vulkan: `cna_renderer_vulkan` and the slim `cna_graphics_ext` target compiled. Earlier
  HEADLESS OFF configuration also compiled; its five unsupported texture-storage tests
  motivated the comparable OPENGLES3 OFF run above.
- C API baseline matches the ON and OFF libraries: 3,213 exports. The route-test coverage
  checker names all 3,213 routes, and the generated coverage inventory is current.
- The full ON `CApi_` group passed 94/98. The four remaining failures are
  `CApi_TextureVolumeSmoke` and three audio smoke tests (`AudioStreaming`,
  `AudioSoundEffect`, `Audio3D`); they reproduce outside the focused extension/content
  selection. Shader-package reproducibility requires `naga`, unavailable on this machine;
  runtime shader tests and the retained package manifest digest passed.
