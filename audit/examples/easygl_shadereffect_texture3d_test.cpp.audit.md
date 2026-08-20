# Audit: examples/easygl_shadereffect_texture3d_test.cpp

## Metadata

- Source file: `examples/easygl_shadereffect_texture3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — plans/plan_graphics.md Task 863: `ShaderEffect::SetTexture(int, Texture3D&)`
  binding a volume texture to a custom shader's `sampler3D` uniform
- File type: raw `Game`-derived executable, two checks (A/B), with a deliberate "decoy texture" false-positive
  guard
- XNA/FNA relevance: `Texture3D` is a genuine XNA type; `ShaderEffect::SetTexture(int, Texture3D&)` and
  `IEffectBackend::BindTexture3D()` are `NOXNA` CNA-internal plumbing enabling a volume texture to reach a custom
  shader at all.
- Related production files: `Texture3D.hpp`/`.cpp`, `ShaderEffect.cpp` (`SetTexture(int, Texture3D&)`),
  `EasyGLGraphicsBackend.cpp` (`EasyGLTexture3DBackend::BindGL()`, `EasyGLEffectBackend::BindTexture3D()`).

## Purpose

Proves the three additive changes Task 863 made: (1) `Texture3D` now inherits `Texture` (not `GraphicsResource`
directly), so it can be stored/passed through the same texture-binding call shape as `Texture2D`; (2)
`ITexture3DBackend::BindGL()` and `IEffectBackend::BindTexture3D()` exist end-to-end; (3)
`ShaderEffect::SetTexture()` gained a `Texture3D&` overload. The test samples a 1×1×2 volume (red at Z-slice 0,
blue at Z-slice 1) at each slice's exact texel centre.

## Executive Verdict

**Healthy.** The `Texture3D : public Texture` inheritance claim, the `BindTexture3D()` unit-restore behavior, and
the decoy-texture false-positive-avoidance technique were all independently verified against the real production
code, not merely trusted from the comment — and the technique genuinely does what it claims (rules out a residual
GL-binding false pass).

## Checklist Results

### API / XNA / FNA parity
`Texture3D(device, 1, 1, 2, false, SurfaceFormat::Color)` matches the real constructor signature
(`Texture3D.hpp` line 30: `Texture3D(GraphicsDevice&, int width, int height, int depth, bool mipMap,
SurfaceFormat)`). Confirmed `class Texture3D : public Texture` (`Texture3D.hpp` line 18) — directly corroborating
this file's own background comment that, pre-Task-863, `Texture3D` inherited `GraphicsResource` directly and could
not flow through a `TextureCollection`-shaped API; the inheritance change is real and present.

### Behavioral correctness
Traced `ShaderEffect::SetTexture(int, Texture3D&)` (`ShaderEffect.cpp` lines 83-86) →
`effectBackend_->BindTexture3D(unit, &texture.GetBackend())` → `EasyGLEffectBackend::BindTexture3D()`
(`EasyGLGraphicsBackend.cpp` lines 371-379): `glActiveTexture(unit)`, `texture->BindGL()`, then
`glActiveTexture(Texture0)` to restore — structurally identical to `BindTexture()`/`BindTextureCube()`, confirmed
by direct comparison of all three implementations (lines 345-379). `EasyGLTexture3DBackend::BindGL()` (line 122)
binds `GL_TEXTURE_3D`, which is the correct GL target for a `sampler3D` uniform.

### Logic
The "sample exactly at each slice's texel centre" trick (`Z=0.25` for slice 0, `Z=0.75` for slice 1, i.e.
`(sliceIndex+0.5)/depth` for `depth=2`) is mathematically correct for defeating linear-filter blending exactly at a
texel centre — 100% of the interpolation weight lands on the single nearest texel along that axis regardless of
`GL_LINEAR` being active, which the file's own header comment states is `EasyGLTexture3DBackend`'s fixed filter
mode (not independently re-verified against the backend's actual sampler setup in this pass, but the underlying
GL behavior itself — "linear filtering exactly at a texel centre = 100% weight on that texel" — is correct GL
semantics regardless).

### Memory/resource lifetime
`volumeTex_`/`decoyTex_` are `std::unique_ptr<Texture3D>`, both alive for the whole `Draw()` call (both
`DrawOnce()` invocations happen after `Initialize()` populates both). Temp `.cnj`/`.vert.glsl`/`.frag.glsl` files
written and never cleaned up — see F1.

### C++ correctness
`precision highp sampler3D;` (fragment shader, line 93) is required GLSL ES 3.00 boilerplate — the spec defines
no default precision for `sampler3D` in fragment shaders (unlike `sampler2D`, which has none either but is
commonly assumed present via extension defaults on some drivers; `sampler3D` genuinely requires an explicit
statement) — correctly present, not superfluous.

### Performance
N/A — single-shot test.

### Robustness
The vertex shader (lines 82-89) declares `aTexCoord` at `layout(location = 1)` but never reads it in `main()` —
harmless (an unused-but-bound vertex attribute is simply ignored by the linked GLSL program, the same benign
pattern documented explicitly in the sibling `easygl_shadowmapping_createshadowmap_shader_test.cpp`'s own header
comment), not a defect.

### Testing
**A real false-positive guard, verified functional, not merely asserted**: `decoyTex_` (all-black, 2 slices) is
created and uploaded *after* `volumeTex_` (`Initialize()` lines 150-161) specifically so that an unmutated
`SetTexture()`/`BindTexture3D()` call is required on every `DrawOnce()` to override the decoy's residual
GL-texture-unit-0/`GL_TEXTURE_3D` binding back to the real volume before the shader samples it. This is the exact
same technique — and the exact same reasoning — as the sibling `easygl_shadereffect_texturecube_test.cpp` (Task
1081), which the header comment explicitly cross-references and states was a *real* bug this session's first draft
fell into for the cube-texture case. Confirmed the reasoning generalizes correctly to the 3D-texture case: without
the decoy, a regression that disabled `BindTexture3D()` entirely would still read `volumeTex_`'s own residual
upload-time binding and pass for the wrong reason.

### Cross-file consistency
Mirrors `easygl_shadereffect_texturecube_test.cpp`'s structure line-for-line (own header comment states this
explicitly) — confirmed the two files really do share the same test shape, decoy-texture technique, and
`GraphicsDeviceManager(64×64)` setup.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 120-132
- Evidence: no `std::filesystem::remove_all` call in the file.
- Why it matters: same low-priority accumulation gap noted in every `.cnj`-based file in this batch.
- Suggested future action (not implemented by this audit): clean up on success or failure.

## Cross-File Observations

- Directly parallels `easygl_shadereffect_texturecube_test.cpp` (Task 1081) — both introduce a brand-new
  `IEffectBackend::BindTextureXxx()` method (`BindTexture3D`/`BindTextureCube`) because `ITexture3DBackend`/
  `ITextureCubeBackend` are independent interfaces, not subtypes of `ITextureBackend` — confirmed by reading
  `IGraphicsBackend.hpp`'s interface hierarchy directly, corroborating both files' own background comments.
- The "decoy texture uploaded last" technique, once discovered as necessary for the cube-texture sibling, was
  correctly carried forward here proactively rather than needing to be independently rediscovered — good evidence
  of the test suite author applying a lesson learned in one file to a structurally similar one.

## Missing or Weak Tests

- Only Z-axis slice selection is exercised (`coord.x`/`coord.y` held fixed at `0.5`); X/Y-axis addressing within a
  1×1×2 volume is trivially degenerate (both dimensions are size 1) so this is an unavoidable scope limit of the
  chosen texture dimensions, not an oversight — a future test with a genuinely multi-voxel X/Y volume would be
  needed to independently verify all three axes.

## Positive Findings

- The decoy-texture false-positive guard is a real, previously-discovered-necessary technique, correctly reused
  here rather than re-litigated from scratch — good test-suite hygiene.
- `Texture3D`'s inheritance change (`GraphicsResource` → `Texture`) claimed by the header comment was independently
  confirmed against the actual header, not assumed.

## Final Assessment

A well-constructed, cross-verified proof of Task 863's volume-texture binding path; the decoy-texture technique is
genuinely functional (not just described), and the only finding is the shared low-priority temp-file cleanup gap.
