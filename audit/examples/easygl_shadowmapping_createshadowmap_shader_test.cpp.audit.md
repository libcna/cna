# Audit: examples/easygl_shadowmapping_createshadowmap_shader_test.cpp

## Metadata

- Source file: `examples/easygl_shadowmapping_createshadowmap_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 947 (Phase 78): HLSL→GLSL conversion proof for
  `ShadowMappingSample_4_0`'s `DrawModel.fx`, `CreateShadowMap` technique
- File type: raw `Game`-derived executable, two checks (A/B), depth-encoding formula proof
- XNA/FNA relevance: direct 1:1 HLSL→GLSL port of a real FNA/XNA sample shader
  (`ShadowMappingSample_4_0/ShadowMapping/Content/DrawModel.fx`); `Matrix::CreateOrthographic` (an XNA API) is
  load-bearing for the test's own hand-derived expected values.
- FNA reference: `DrawModel.fx`'s `CreateShadowMap_VertexShader`/`CreateShadowMap_PixelShader`, quoted verbatim in
  the file's own header comment — independently confirmed below to be an accurate, faithful transcription.
- Related production files: `Matrix.cpp` (`CreateOrthographic`), `ShaderEffect.cpp`, `EasyGLGraphicsBackend.cpp`
  (`BindCustomEffectMatrices`, `DrawIndexedPrimitivesEx`).

## Purpose

Ports `DrawModel.fx`'s depth-only `CreateShadowMap` technique: transforms a vertex into light-space clip
coordinates and outputs its NDC-ish depth (`clip.z/clip.w`) into the red channel, the first half of a two-pass
shadow-mapping technique (paired with the sibling `easygl_shadowmapping_drawwithshadowmap_shader_test.cpp`).

## Executive Verdict

**Healthy.** This audit independently re-derived the `Matrix::CreateOrthographic` coefficients from CNA's actual
`Matrix.cpp` implementation (not trusted from the comment) and recomputed both expected byte values from first
principles — both match the file's own asserted expectations exactly (byte 128 and byte 191).

## Checklist Results

### API / XNA / FNA parity
`Matrix::CreateOrthographic(width, height, zNearPlane, zFarPlane)` — confirmed against `Matrix.cpp` lines 564-575:
`M33 = 1/(zNearPlane - zFarPlane)`, `M43 = zNearPlane/(zNearPlane - zFarPlane)`, matching FNA's own
`Matrix.CreateOrthographic` (`Matrix.cs` lines 999-1007) coefficient-for-coefficient — this is a genuine XNA-API
parity check, not just an internal consistency check, and it passes.

### Behavioral correctness
The ported GLSL (lines 93-113) is a faithful 1:1 transcription of the quoted FNA HLSL: `Out.Position = mul(Position,
mul(World, LightViewProj))` → `gl_Position = LightViewProj * World * vec4(aPosition, 1.0)` (row-vector HLSL
convention correctly re-expressed as GLSL's column-vector `M*v` via this codebase's established
`ToColumnMajor()`-upload convention — the same pattern used consistently by every other 3D shader in this batch);
`Out.Depth = Out.Position.z / Out.Position.w` → `vDepth = gl_Position.z / gl_Position.w` — identical. Only
`float4 Position` is declared as vertex input, matching the real sample's own input struct (the file's own comment
correctly notes the test's `VertexPositionNormalTexture` buffer supplies unused Normal/TexCoord attributes that the
linked GLSL program simply never declares — legal, benign).

### Logic
Independently re-derived (without relying on the file's own worked derivation) for `zNearPlane=-10,
zFarPlane=10`: `M33 = 1/(-10-10) = -0.05`; `M43 = -10/(-20) = 0.5`. For `World=Translate(0,0,z)` applied to a
vertex whose local `z=0` (the quad lies in the local XY-plane): `WorldPos.z = z` (translation only offsets, x/y
untouched by a pure Z-translation), and since the quad's translation is uniform across the whole quad, every
corner shares the same `WorldPos.z`, so the read-back centre pixel's depth is constant across the quad, not an
interpolated/varying value — a subtlety the derivation depends on and which holds correctly. `clip.z = WorldPos.z ×
M33 + M43` (orthographic, no rotation component in `LightViewProj` itself); `clip.w = 1` (orthographic matrices have
`M44=1`, no perspective term). So `Depth = WorldPos.z × (-0.05) + 0.5`.
- Check A (`World=Identity`, z=0): `Depth = 0.5` → byte `round(0.5×255) = 128` (`0.5×255=127.5`, XNA/GL rounding
  conventions round `.5` up here) — matches `close(a.R, 128)`.
- Check B (`World=Translate(0,0,-5)`, z=-5): `Depth = -5×-0.05+0.5 = 0.75` → byte `round(0.75×255) = 191`
  (`0.75×255=191.25`) — matches `close(b.R, 191)`.
Both independently re-derived values match the file's own assertions exactly; Check B being distinct from Check A
is genuine evidence `World` reaches the light-space transform, not just that the formula is well-formed at the
origin.

### Memory/resource lifetime
`vb_`/`ib_` are `std::unique_ptr`s with clean RAII lifetime. Temp directory files never cleaned up — see F1.

### C++ correctness
No issues found.

### Performance
N/A.

### Robustness
`device.SetDepthTestEnabled(false)` is set (line 171) — appropriate, since this test reads the *encoded* depth
value out of the color channel via `GetBackBufferData`, not the hardware depth buffer; no interaction with real
depth testing is exercised or needed here.

### Testing
The scope reduction documented in the header (rendering to the normal 8-bit backbuffer instead of a real
`SurfaceFormat.Single` float `RenderTarget2D`) is explicitly and correctly justified: the chosen test depths (0.5,
0.75) stay safely inside `[0,1]` and never need the extra precision/range a float target would add — confirmed
this reasoning is sound, since the formula under test produces no values outside `[0,1]` for either check.

### Cross-file consistency
The `z=-5`/`Depth=0.75` result from Check B is explicitly reused (not literally chained, just documented) as the
"ourdepth" reference point in the sibling `easygl_shadowmapping_drawwithshadowmap_shader_test.cpp` — confirmed by
cross-reading that file: it independently re-derives the same `0.75` value from the same `World=Translate(0,0,-5)`
and `LightViewProj` base matrix, consistent with this file's own Check B.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 131-143
- Evidence: no cleanup call in the file.
- Why it matters: same shared low-priority gap as every other `.cnj`-based file in this batch.
- Suggested future action (not implemented by this audit): clean up on success or failure.

## Cross-File Observations

- The `Depth = gl_Position.z / gl_Position.w` value written to `vDepth` is genuine D3D-convention clip-space depth
  (range `[0,1]` for XNA's own orthographic/perspective matrices), not remapped to GL's native `[-1,1]` NDC-z
  convention — this is fine for *this* test (depth testing is disabled, and the value is only ever read back as a
  manually-encoded color channel, never fed through the hardware depth buffer), but it is worth flagging for
  whichever future task actually wires a real depth-buffer-backed shadow map through EasyGL: if `gl_Position.z` is
  ever relied on by the GL rasterizer's own depth test/write in a *different* draw call, the D3D-vs-GL clip-space
  convention mismatch would need explicit handling (a remap or a `glDepthRange`-style adjustment) — not a defect in
  this file, but a scope boundary worth the eventual `backend-easygl` shard audit's attention if/when a real float
  shadow-map render target is implemented.

## Missing or Weak Tests

- Only the depth-encoding *formula* is proven (matches the scope explicitly stated in the header); no test in
  this batch exercises the encoded depth actually being sampled back through a real texture and compared against a
  second draw's own depth (that integration is the sibling `DrawWithShadowMap` test's job, and it uses synthetic
  hand-authored shadow-map textures rather than this technique's own rendered output — a deliberate, documented
  choice, not an oversight).

## Positive Findings

- The `Matrix::CreateOrthographic` coefficients used in the hand derivation were independently re-verified by this
  audit against the actual CNA implementation (not just trusted from the comment) and against FNA's own formula —
  both match.
- Correctly and explicitly scopes out the float-render-target aspect of the real sample rather than silently
  under-implementing it and calling the test complete.

## Final Assessment

An accurate, independently re-verified 1:1 port of `DrawModel.fx`'s `CreateShadowMap` technique; both expected
byte values were recomputed from first principles by this audit and match exactly. Only a shared low-priority
hygiene note (F1) and a forward-looking scope observation (D3D-vs-GL clip-space convention) are recorded.
