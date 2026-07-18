# Audit: examples/easygl_perpixellighting_shader_test.cpp

## Metadata

- Source file: `examples/easygl_perpixellighting_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for XNA Game Studio's
  `PerPixelLighting.fx`, `PerPixelDiffuseAndPhong` technique
- File type: C++ example/integration-test executable
  (`EasyGLPerPixelLightingShaderTest : Microsoft::Xna::Framework::Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`),
  `ContentManager`'s `.cnj` `EffectTypeReader` (`ContentManager.cpp` lines 715-820)
- XNA/FNA relevance: real XNA Game Studio sample content (not FNA-tree source) — independently confirmed the
  file's transcription of `PerPixelDiffuseVS`/`DiffuseAndPhongPS` against the actual
  `PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx` (lines 142-213, technique
  `PerPixelDiffuseAndPhong` lines 243-257) — verbatim match.
- Main related tests: this file itself (Task 947, Phase 78 rollout, described in its own header as "First real 3D
  (not SpriteBatch) shader conversion"); sibling of
  `easygl_perpixellighting_diffuseonly_shader_test.cpp` and
  `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`, both audited in this same batch.

## Purpose

Ports `PerPixelLighting.fx`'s `PerPixelDiffuseAndPhong` technique (ambient + per-pixel Lambertian diffuse +
per-pixel Phong specular, using a real light *position* rather than just a direction) to GLSL, and verifies it for
two `World` matrices (Identity, RotationY180) that flip the diffuse term to zero while leaving the specular term
unchanged by this scene's own geometry, proving diffuse and specular are independently computed and that `World`
genuinely reaches the per-pixel normal.

## Executive Verdict

**Healthy.** Both of this file's expected pixel triples were independently recomputed step-by-step
(`directionToLight`/`diffuseIntensity`/`reflect()`/`specular`) from the real GLSL formula and matched the file's
own claimed values exactly, including the specific claim that specular is unaffected by the `World` rotation in
this particular symmetric-geometry setup while diffuse clamps to zero. No HIGH/CRITICAL findings; one shared LOW
housekeeping item (temp-directory cleanup) already established elsewhere in this shard.

## Checklist Results

### API / XNA / FNA parity
N/A directly, but the ported logic is real, independently-verified sample content. `DiffuseAndPhongPS` (real
source lines 190-208): `directionToLight`, `diffuseIntensity=saturate(dot(...))`, `diffuse`,
`reflectionVector=normalize(reflect(-directionToLight, input.WorldNormal))`, `directionToCamera`,
`specular=specularLightColor*specularIntensity*pow(saturate(dot(reflectionVector,directionToCamera)),
specularPower)`, `color=specular+diffuse+ambientLightColor` — matches the ported GLSL (`kFragSrc`, lines 114-140)
term-for-term, **including** the file's own explicitly-called-out non-obvious fidelity choice (lines 32-36):
`WorldNormal` is used raw in both the diffuse dot product and the `reflect()` call, never re-normalized after
per-vertex interpolation — confirmed this matches the real HLSL source exactly (no `normalize(input.WorldNormal)`
call anywhere in `DiffuseAndPhongPS`), i.e. the port faithfully preserves what could look like an oversight in the
original rather than "fixing" it — the correct behavior per this project's own `CLAUDE.md` fidelity rule ("match
XNA/FNA behavior over personal C++ preference").

### Behavioral correctness
Independently re-derived both checks (light `(0,0,5)`, camera `(0,0,3)`, `ambientLightColor=(0.1,0.05,0.02)`,
`diffuseLightColor=(0.4,0.3,0.2)`, `specularLightColor=(1,1,1)`, `specularIntensity=0.3`, `specularPower=20`):
- Check A (`World=Identity`): at screen centre, `WorldPosition=(0,0,0)`, `worldNormal=(0,0,1)`,
  `directionToLight=normalize((0,0,5))=(0,0,1)` → `diffuseIntensity=1` → `diffuse=(0.4,0.3,0.2)`.
  `reflect(-directionToLight, worldNormal) = reflect((0,0,-1),(0,0,1))`: `dot((0,0,-1),(0,0,1))=-1`, so
  `reflect = I - 2*(-1)*N = (0,0,-1)+(0,0,2) = (0,0,1)` → `reflectionVector=(0,0,1)`.
  `directionToCamera=normalize((0,0,3))=(0,0,1)` → `dot(reflectionVector,directionToCamera)=1` →
  `specular=(1,1,1)*0.3*pow(1,20)=(0.3,0.3,0.3)`.
  `color=(0.3,0.3,0.3)+(0.4,0.3,0.2)+(0.1,0.05,0.02)=(0.8,0.65,0.52)` → bytes
  `(204, 165.75→166, 132.6→133)`. **Matches the file's claimed `(204,166,133)` exactly.**
- Check B (`World=RotationY180`): same on-screen footprint (X-symmetric quad), `worldNormal` flips to `(0,0,-1)`.
  `diffuseIntensity=clamp(dot((0,0,1),(0,0,-1)),0,1)=0` → `diffuse=(0,0,0)`.
  `reflect(-directionToLight,worldNormal)=reflect((0,0,-1),(0,0,-1))`: `dot((0,0,-1),(0,0,-1))=1`, `reflect =
  (0,0,-1)-2*1*(0,0,-1) = (0,0,-1)+(0,0,2) = (0,0,1)` → same `reflectionVector=(0,0,1)`, same `directionToCamera`,
  same `specular=(0.3,0.3,0.3)`.
  `color=(0.3,0.3,0.3)+(0,0,0)+(0.1,0.05,0.02)=(0.4,0.35,0.32)` → bytes `(102, 89.25→89, 81.6→82)`.
  **Matches the file's claimed `(102,89,82)` exactly.**
Both checks recompute correctly, and the file's own claim that these values are "distinct from Check A" (proving
`World` reaches the vertex shader's normal transform) is verified true by a wide margin (204 vs 102, well outside
the ±6 tolerance).

### Logic
`kVertSrc`/`kFragSrc` (lines 95-140) match `easygl_perpixellighting_diffuseonly_shader_test.cpp`'s vertex shader
character-for-character (both share `PerPixelDiffuseVS`), correctly reflecting that the real `.fx` file's
`PerPixelDiffuse` and `PerPixelDiffuseAndPhong` techniques share one vertex shader and differ only in pixel shader.

### Memory/resource lifetime
Same per-instance-pointer temp-directory pattern as every sibling test in this batch (lines 157-169), never
cleaned up — see Detailed Findings F1.

### Robustness
`Draw()` checks `!fx || !fx->IsEffectValid()` (lines 232-237) before proceeding — consistent with the rest of this
batch.

### Testing
This file is itself a test. Its 2-check design deliberately isolates a scenario where the diffuse term changes but
the specular term doesn't (by the scene's own reflection-symmetry), which is a genuinely more discriminating check
than two checks that both moved in the same direction would be — it independently confirms diffuse and specular
are computed from *separate* per-pixel quantities, not accidentally coupled.

## Detailed Findings

### F1 — Temp directory written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource lifetime
- Location/symbol: `Initialize()`, lines 157-169
- Evidence: no cleanup call for the created temp directory exists anywhere in this file.
- Why it matters: identical, shared, low-priority finding already recorded for every other hand-rolled
  `ShaderEffect` test in this batch — harmless orphan temp-directory accumulation across CTest runs, not a
  correctness defect.
- FNA/XNA comparison: N/A.
- Related files: `easygl_particleeffect_shader_test.cpp`,
  `easygl_perpixellighting_diffuseonly_shader_test.cpp`,
  `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`,
  `easygl_postprocesseffect_shader_test.cpp`.

## Cross-File Observations

- This file, its `PerPixelDiffuse`-only sibling, and its `PerVertexDiffuseAndPerPixelPhong` sibling together cover
  3 of `PerPixelLighting.fx`'s 5 real techniques (the file's own header, line 3-4, correctly notes the remaining 2
  as not yet ported at time of writing) — a genuinely systematic technique-by-technique port, not an arbitrary
  subset.
- The raw (non-renormalized) `WorldNormal` fidelity choice this file documents recurs as a named, deliberate
  pattern across this project's other ported `.fx` shaders per the file's own comment (contrasted with
  `NormalMapping.fx`, which does renormalize) — worth a cross-reference note for whichever future audit catalogues
  this project's HLSL→GLSL fidelity conventions as a whole.

## Missing or Weak Tests

- Both checks land at the exact same screen-space sample point (viewport centre) with the same light/camera
  geometry each time — there is no check that varies the light or camera position itself (only `World`), so a
  hypothetical bug that swapped `lightPosition`/`cameraPosition` in the specular calculation (both currently
  distinct uniforms feeding `directionToLight`/`directionToCamera`) would likely still be caught here (since they
  have different values), but a bug that swapped them consistently in both the diffuse and specular halves would
  not necessarily be — a low-priority, largely theoretical gap given the values already differ.

## Positive Findings

- FNA-Game-Studio-sample transcription (including the specific "raw, non-renormalized `WorldNormal`" nuance)
  independently confirmed verbatim against the real `.fx` source during this audit.
- Both expected pixel triples independently recomputed step-by-step from the real GLSL formula and found to match
  exactly, including correct byte-rounding.
- The 2-check design genuinely isolates diffuse-vs-specular independence via the scene's own geometric symmetry,
  rather than relying on two checks that would trivially pass together.

## Final Assessment

A rigorously-verified shader-conversion test. Every mathematical claim in its header comment — including a subtle
fidelity choice (preserving the original HLSL's non-renormalized `WorldNormal`) — was independently traced and
confirmed correct during this audit. Its only gap is the shared, low-priority temp-directory cleanup omission
common to this test family.
