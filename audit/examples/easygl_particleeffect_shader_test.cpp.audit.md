# Audit: examples/easygl_particleeffect_shader_test.cpp

## Metadata

- Source file: `examples/easygl_particleeffect_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for XNA Game Studio's
  `ParticleEffect.fx` (`Particles3DSample`/`XmlParticles`)
- File type: C++ example/integration-test executable (`EasyGLParticleEffectTest : Microsoft::Xna::Framework::Game`,
  `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`),
  `ContentManager`'s `.cnj` `EffectTypeReader` (`ContentManager.cpp` lines 715-820), `VertexDeclaration`/
  `VertexElement` custom-layout binding (`EasyGLGraphicsBackend.cpp::ApplyLayout`, "Task 1080" generic path, lines
  2201-2226)
- XNA/FNA relevance: not a `Microsoft::Xna` stock effect — a hand-written GLSL port of an XNA Game Studio sample
  shader (`ParticleEffect.fx`), loaded through CNA's `ShaderEffect`/`.cnj` content pipeline (a `NOXNA` mechanism).
  Judged against the real sample source, confirmed present on disk at
  `.../Particles3DSample_4_0/Particle3DSample/Content/ParticleEffect.fx` (also byte-identical in `XmlParticles_4_0`,
  confirmed via `diff` during this audit).
- Main related tests: this file itself (Task 947, Phase 78 rollout).

## Purpose

Proves GPU-animated billboarded particles (`ParticleEffect.fx`'s velocity+gravity position integral,
clip-space corner billboarding, alpha fade-in/out curve, and rotation) work end-to-end through CNA's custom-GLSL
`ShaderEffect` + Task 1080 custom-`VertexDeclaration` path, using a non-standard 52-byte
Corner+Position+Velocity+Random+Time vertex layout that matches none of CNA's built-in strides.

## Executive Verdict

**Healthy.** This is an unusually rigorous test: its header comment transcribes the real FNA-reference-tree-absent
(XNA Game Studio sample, not FNA proper) HLSL source, and every one of its non-trivial claims — the HLSL
transcription, the `Projection[1][1]`/`M22` FOV-scale-factor reasoning, the vertex-attribute-location wiring, and
the hand-derived expected pixel values for all 3 checks — was independently re-verified during this audit and
found correct. Its own header additionally discloses two genuine, non-hidden coverage blind spots from its own
mutation testing (see Missing or Weak Tests). No HIGH/CRITICAL findings; one shared LOW housekeeping item (temp
directory never cleaned up) already established elsewhere in this shard.

## Checklist Results

### API / XNA / FNA parity
Not an XNA-namespace type under test — `ShaderEffect` (`NOXNA`, confirmed in `ShaderEffect.hpp`) is CNA's own
custom-GLSL effect wrapper. This audit independently confirmed the file's own HLSL transcription (lines 13-58)
against the real sample source at `Particles3DSample_4_0/.../ParticleEffect.fx`: `ComputeParticlePosition`,
`ComputeParticleSize` (`size * Projection._m11`), `ComputeParticleColor` (including the genuinely-dead
`projectedPosition` parameter, confirmed unused in the real `.fx` body too), `ComputeParticleRotation`, and
`ParticleVertexShader`/`ParticlePixelShader` all match verbatim (also confirmed byte-identical between the
`Particles3DSample_4_0` and `XmlParticles_4_0` copies via `diff`, corroborating the file's own claim, line 3-5).
The `Projection._m11` (HLSL 0-indexed) → GLSL `Projection[1][1]` mapping (line 65-71) is correct reasoning: since a
diagonal matrix element survives transpose, and this project's established `ToColumnMajor()` convention means the
uploaded GLSL matrix already equals HLSL's transposed, no swizzle correction beyond the reindex is needed — verified
independently, this is not a "common misreading" as the comment itself warns against.

### Behavioral correctness
Re-derived the 3 checks' expected values independently rather than trusting the header comment at face value:
- Check A (centre, size=1.5): alpha fade at `normalizedAge=0.5` → `0.5*(1-0.5)^2*6.7 = 0.8375` →
  `alpha_byte = round(255*0.8375) = 214`. Colour `(200,100,50)` unchanged (white texture × colour = colour).
  Billboard half-extent: `size/clip.w = 1.5/3 = 0.5` NDC → pixel span `[16,48]` on a 64px viewport; sample at pixel
  32 is well inside. **Matches the file's claimed `(200,100,50,214)`.**
- Check B (pixel 4, size=1.5): pixel 4 is well outside `[16,48]` → clear colour `(10,10,10,255)`. **Matches.**
- Check C (pixel 20, size=0.75): half-extent `0.75/3=0.25` NDC → span `[24,40]`; pixel 20 is inside Check A's
  `[16,48]` but outside `[24,40]` → clear colour. **Matches.**
All three independently recomputed values agree exactly with the file's own claims.

### Logic
`MakeQuad()`'s custom `VertexDeclaration` (lines 332-338) declares fields in the order Corner(Vector2)/
Position(Vector3)/Velocity-as-Normal(Vector3)/Random-as-Color(Vector4)/Time-as-Single(Single) at offsets
0/8/20/32/48 — checked against `ParticleVertex`'s actual field layout (`cx,cy`/`px,py,pz`/`vx,vy,vz`/
`rx,ry,rz,rw`/`time`, `#pragma pack(push,1)`, `static_assert(sizeof==52)`) and against
`EasyGLGraphicsBackend.cpp::ApplyLayout`'s Task 1080 generic path (lines 2201-2226), which binds "attribute
location = the element's own index within the declaration's list" — the 5 declared elements map to GLSL
`layout(location=0..4)` `aCorner`/`aPosition`/`aVelocity`/`aRandom`/`aTime` exactly, offset-for-offset. This is a
genuinely correct, cross-file-verified wiring, not an assumption.
`m22_ = 1.0f / std::tan(MathHelper::PiOver4 * 0.5f)` (line 325) and `size = sizeK / m22_` (line 385) is a
self-cancelling construction (`size*m22 == sizeK` exactly, up to floating-point rounding in `m22` itself) —
verified this is algebraically sound and avoids hard-coding an irrational decimal, exactly as the header claims.

### Memory/resource lifetime
Temp `.cnj`/GLSL files are written under `std::filesystem::temp_directory_path() / "cna_particleeffect_test_
<this-pointer>"` (lines 302-313) and never removed (`std::filesystem::remove_all` is never called in this file) —
see Detailed Findings F1. `gdm_` (`std::unique_ptr<GraphicsDeviceManager>`), `fxBase_` (`std::shared_ptr<Effect>`),
`ib_` (`std::unique_ptr<IndexBuffer>`) are all correctly-owned members with no dangling-pointer risk observed.

### C++ correctness
`ParticleVertex` uses `#pragma pack(push,1)` plus a `static_assert(sizeof(ParticleVertex) == 52)` (lines 188-198) —
a good defensive pattern against silent padding drift on a struct whose exact byte layout is load-bearing for the
GPU upload.

### Performance
N/A beyond the general single-shot-test shape — `DrawOnce()` is called exactly 3 times, each a single indexed
draw call; no hot-path concern.

### Robustness
`Draw()` checks `!fx || !fx->IsEffectValid()` before proceeding (line 404-409) and fails cleanly with a diagnostic
message rather than dereferencing a possibly-null/uncompiled effect — consistent with every other hand-rolled
`ShaderEffect` test in this batch.

### Testing
This file is itself a test, and its own header comment (lines 128-142) discloses the results of deliberate
mutation testing it performed on itself: dropping the corner-offset line and dropping the alpha-fade curve were
both cleanly caught by the existing checks (re-confirmed 3/3 PASS after reverting); dropping the `Projection[1][1]`
FOV-scale factor and dropping the `StartSize`/`EndSize` lerp were both found to be **invisible** to this test's own
3 sample points, and this is stated plainly rather than hidden — see Missing or Weak Tests.

## Detailed Findings

### F1 — Temp directory written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource lifetime
- Location/symbol: `Initialize()`, lines 302-313 (`std::filesystem::create_directories(root)`,
  `WriteFile(root / "pe.vert.glsl", ...)` etc.)
- Evidence: the per-instance-pointer-suffixed temp directory (`cna_particleeffect_test_<this>`) is created and
  populated with 3 files but no `std::filesystem::remove_all(root)` (or equivalent) call exists anywhere in this
  file.
- Why it matters: a small, harmless accumulation of orphan temp-directory content across repeated CTest runs — not
  memory, not a GPU resource, and not a correctness issue, since the pointer-address suffix already avoids
  cross-run collisions. Consistent with the same already-recorded finding in sibling files in this shard (e.g.
  `easygl_animsprite_shader_test.cpp`'s own audit report), so this is a shared, low-priority pattern across the
  hand-rolled `ShaderEffect` test family, not unique to this file.
- FNA/XNA comparison: N/A.
- Related files: every other file in this batch that writes its own `.cnj`/GLSL pair to a temp directory
  (`easygl_perpixellighting_diffuseonly_shader_test.cpp`, `easygl_perpixellighting_shader_test.cpp`,
  `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`,
  `easygl_postprocesseffect_shader_test.cpp`).
- Suggested future action (not implemented by this audit): a shared `PixelTestGame`-style RAII temp-directory
  helper (`std::filesystem::remove_all` in its destructor) would close this cleanly for the whole family at once,
  if this pattern is revisited.

## Cross-File Observations

- Shares the exact same "write `.cnj` + GLSL to a per-instance temp dir, load via `ContentManager`" pattern as the
  3 `PerPixelLighting` tests and the `PostprocessEffect` test in this same batch — confirmed the `.cnj` schema
  (`"cnjVersion":1, "type":"Effect", "vertex":..., "fragment":...`) matches `ContentManager.cpp`'s actual
  `EffectTypeReader` (lines 715-820) in every one of these files, not just asserted by convention.
- The Task 1080 generic `VertexDeclaration` attribute-binding path this file exercises (`ApplyLayout`,
  `EasyGLGraphicsBackend.cpp:2201-2226`) is shared with `easygl_pbreffect_golden_test.cpp`'s stride-48 vertex
  layout in this same batch, though that file uses the fixed-stride `switch` path (stride 48 is a recognized
  built-in case) rather than this file's genuinely custom, declaration-driven path — this file is the more
  demanding exercise of the two for that specific backend code path.

## Missing or Weak Tests

- Self-acknowledged (file's own header, lines 128-134): dropping the `Projection[1][1]` FOV-scale factor from
  `computeParticleSize()`, and separately making the size formula ignore the `StartSize`/`EndSize` lerp entirely,
  were both found **invisible** to this test's own 3 sample pixels — every chosen sample point still landed inside
  or outside the resulting quad's footprint regardless. Combined with the test's own deliberate
  `StartSize==EndSize` simplification (which makes the lerp a structural no-op here regardless of whether the
  lerp logic is even present), this means `ComputeParticleSize`'s FOV-scaling and its start/end interpolation are
  effectively untested by this file's own numeric assertions, despite the shader code correctly implementing both.
- `ComputeParticleRotation` is exercised only with `RotateSpeed=(0,0)` (identity rotation) — a genuinely broken
  rotation formula would not be caught by this test at all, for the geometric reason the header gives (a square
  billboard has 4-fold symmetry).

## Positive Findings

- The header comment's own mutation-testing disclosure (lines 128-142) is a genuinely good practice: rather than
  claiming blanket coverage, it names exactly which two mutations were undetectable by this test's own design and
  why, while also reporting the two mutations that *were* cleanly caught (corner-offset removal, alpha-fade-curve
  removal) with their exact observed failure values.
- Byte-identical FNA-sample-source transcription independently confirmed via `diff` against both source copies
  (`Particles3DSample_4_0` and `XmlParticles_4_0`) during this audit.
- The custom 52-byte vertex layout and its GLSL attribute-location wiring were independently traced end-to-end
  (`VertexElement` list → `ApplyLayout`'s generic path → GLSL `layout(location=N)`) and found fully consistent.

## Final Assessment

A rigorously-verified shader-conversion test whose HLSL transcription, matrix-indexing reasoning, custom
vertex-layout wiring, and hand-derived expected pixel values all independently check out. Its only real gaps are
the two honestly-disclosed mutation blind spots (FOV-scale factor, size lerp) and the shared, low-priority
temp-directory cleanup omission common to this test family.
