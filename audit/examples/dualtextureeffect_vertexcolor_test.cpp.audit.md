# Audit: examples/dualtextureeffect_vertexcolor_test.cpp

## Metadata

- Source file: `examples/dualtextureeffect_vertexcolor_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — `DualTextureEffect.VertexColorEnabled` pixel test,
  shared verbatim source registered on **all 3 backends that support 3D effects**: EasyGL
  (`cmake/Tests/EasyGLTests.cmake:1449`, `EasyGL_DualTextureEffect_VertexColorEnabled`), Vulkan
  (`cmake/Tests/VulkanTests.cmake:233`, `Vulkan_DualTextureEffect_VertexColorEnabled`), Bgfx
  (`cmake/Tests/BgfxTests.cmake:668`, `Bgfx_DualTextureEffect_VertexColorEnabled`) — confirmed by
  direct `grep` across `cmake/Tests/*.cmake`, matching the file's own header comment's claim
  exactly ("verified on all 3 backends (EasyGL/Vulkan/Bgfx share this exact source").
- XNA/FNA relevance: direct — `DualTextureEffect.VertexColorEnabled`,
  `DualTextureEffect.DiffuseColor`/`Alpha`, `Texture`/`Texture2`.
- FNA reference: `Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx` — `VSDualTextureVc`'s
  `vout.Diffuse *= vin.Color` (applied on top of `ComputeCommonVSOutput()`'s
  `vout.Diffuse = DiffuseColor`), and `PSDualTexture`'s
  `color = SAMPLE_TEXTURE(Texture,pin.TexCoord); color.rgb *= 2; color *= overlay * pin.Diffuse`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams()` lines 248-275, `OnApply()`'s shader-index dispatch lines 236-245),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureDualTexturedColored3DProgram()` lines 3072-3142), `src/CNA/Internal/Backends/Vulkan/shaders/
  dual_texture_colored3d.vert.glsl` + `dual_texture3d.frag.glsl`,
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_dual_texture_colored3d.sc` + `fs_dual_texture3d.sc`.

## Purpose

Two-case pixel test proving `DualTextureEffect.VertexColorEnabled` actually gates whether the
per-vertex color multiplies into the final pixel color. Renders a full-screen quad with a half-gray
base texture (`kHalfGray=(128,128,128,255)`, chosen so `base.rgb*2 ≈ 1.0`, an identity factor —
avoiding the un-clamped-doubling trap a *white* base texture would hit, per the file's own comment)
and a white overlay texture, with `VertexColor=(200,100,50,200)`, `DiffuseColor=(0.6,0.4,0.8)`,
effect `Alpha=0.8`. Case (a) `VertexColorEnabled=true` expects
`RGB = VertexColor * DiffuseColor * Alpha`; case (b) `VertexColorEnabled=false` expects
`RGB = DiffuseColor * Alpha` only (vertex color factor dropped). The header comment documents the
real regression this test guards (Task 889): before the fix, all 3 backends' dual-texture vertex
shaders declared only position+UV (no color attribute at all) and never read
`VertexColorEnabled`, so both cases produced the *same* output — this test would have failed case
(a) on every backend prior to the fix.

## Executive Verdict

**Healthy** — this audit independently re-derived both expected constants by hand against the
current production formula (CPU-side `FillGpuDrawParams` and all 3 backends' actual shader source)
and both match closely; the test genuinely discriminates the two code paths it claims to, and the
underlying Task 889 fix is confirmed present and correct on all 3 registered backends.

## Checklist Results

### API / XNA / FNA parity
`getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` (used at line 114 via the
setter) correctly follow this project's C# property convention — notably, and worth calling out
positively, `DualTextureEffect` does **not** repeat the `BasicEffect::VertexColorEnabled` bare-field
lapse documented in `AUDIT_CROSS_CUTTING_FINDINGS.md`; `DualTextureEffect.cpp` lines 170-178 show a
proper `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` pair with dirty-flag
tracking, matching every other property on the class. `setTextureProperty`/`setTexture2Property`/
`setDiffuseColorProperty`/`setAlphaProperty` all map correctly to FNA's `DualTextureEffect` surface.

### Behavioral correctness
Re-derived both expected colors from the actual production formula:
- `FillGpuDrawParams()` (`DualTextureEffect.cpp:260-263`) pushes
  `diffuseColor = (DiffuseColor.X*Alpha, .Y*Alpha, .Z*Alpha, Alpha) = (0.48, 0.32, 0.64, 0.8)`.
- EasyGL's fragment shader (`EnsureDualTexturedColored3DProgram`, lines 3120-3124):
  `vc = VertexColorEnabled ? vColor : (1,1,1,1); base=texture(tex0); base.rgb*=2.0;
  FragColor = base * texture(tex1) * vc * uDiffuseColor`.
  - Case (a): `base.rgb ≈ (0.502*2, ...) ≈ (1.004,1.004,1.004)`, `tex1=white=(1,1,1,1)`,
    `vc = VertexColor/255 * DiffuseColor*Alpha = (0.7843,0.3922,0.1961)*(0.48,0.32,0.64)
    ≈ (0.3765,0.1255,0.1255)`; times `base.rgb≈1.004` ≈ `(0.378,0.126,0.126)` → `*255 ≈ (96,32,32)` —
    **matches `kExpectedEnabled(96,32,32,255)` exactly**.
  - Case (b): `vc = (1,1,1,1)*uDiffuseColor = (0.48,0.32,0.64,0.8)`; times `base.rgb≈1.004` ≈
    `(0.482,0.321,0.643)` → `*255 ≈ (123,82,164)` — the asserted `kExpectedDisabled(122,82,163,255)`
    is within 1 unit per channel, comfortably inside the file's own `±8` tolerance
    (`matches()`, lines 93-98). **This audit independently confirms both constants are correct for
    the current formula**, not merely self-consistent guesses.
- Vulkan's `dual_texture_colored3d.vert.glsl` (line 36):
  `fragTint = (vertexColorEnabled>0.5) ? inColor*diffuseColor : diffuseColor;` and
  `dual_texture3d.frag.glsl` (lines 30-33): `tex1.rgb*=2.0; outColor = tex1*tex2*fragTint` — same
  formula, confirmed by direct shader inspection, not assumed from the EasyGL derivation alone.
- Bgfx's `vs_dual_texture_colored3d.sc` (line 20-21) and `fs_dual_texture3d.sc` (lines 11-15) —
  same formula again, independently confirmed.
- All three backends dispatch correctly on `VertexColorEnabled`: EasyGL via a **separate compiled
  program** (`EnsureDualTexturedColored3DProgram()` vs. `EnsureDualTextured3DProgram()`, selected in
  `SelectProgram`-equivalent dispatch at line 3948-3958's `if (params.dualTexture)` branch based on
  `p.vertexColorEnabled`); Vulkan/Bgfx via a **branch inside the shared vertex shader** reading a
  `vertexColorEnabled` uniform/push-constant. Both are legitimate ways to implement the same
  semantics and both were independently confirmed correct.

### Logic
`renderWith()` (lines 108-133) retries the render+readback loop up to 20 times, breaking as soon as
a non-black pixel is read back — a defensive pattern against a possible first-frame blank-readback
race also seen in `environmentmapeffect_alphascaledlerp_test.cpp` (audited separately) and several
other example files in this shard; reasonable and not a masking hazard here since the loop
specifically detects "still black" (the `Clear(kBlack)` color) rather than accepting any arbitrary
early frame.

### C++ correctness
`matches()`/`closeTo()` (lines 91-98) use plain `int` arithmetic on `Color::getXProperty()` byte
values — no overflow risk at this magnitude. `renderWith()` takes `tex0`/`tex1`/`quad` by reference
and a `bool` by value — no lifetime issues; `DualTextureEffect fx(dev)` is constructed fresh inside
`renderWith()` for each call, so no state leaks between the enabled/disabled cases (each starts from
`DualTextureEffect`'s own constructor defaults except for what's explicitly set).

### Memory/resource lifetime
`Texture2D tex0`/`tex1` and the `DualTextureEffect fx` are all stack-local with value/RAII semantics
consistent with `Texture2D`'s confirmed shared-ownership design (see the companion
`dxt1_texture_test.cpp` audit for a deeper look at `Texture2D`'s copy/move semantics) — no leaks or
UAF risk identified.

### Performance
N/A — 64×64 single-quad pixel test, trivial cost, appropriate for a correctness-focused example.

### Thread safety
N/A — single-threaded `Game` harness, standard for this example tree.

### Architecture
Correctly exercises only the public XNA-compatible `DualTextureEffect` API plus
`GraphicsDevice`/`RasterizerState`/`BlendState` — no backend-specific code in the test itself (the
"shared verbatim source across 3 backends" claim is architecturally sound and confirmed).

### Maintainability
Extensive, accurate header comment cross-referencing the exact FNA HLSL formula and the specific
regression (Task 889) this test guards — a strong example of the "good" documentation pattern this
audit has otherwise found lacking in some sibling test files (see `dxt1_texture_test.cpp`'s audit,
F1, for a contrasting case in this same batch). No magic numbers without inline justification
(`kHalfGray`'s choice is explained in a comment, not just asserted).

### Portability
No platform-specific code.

### Robustness
The `renderWith()` retry-until-non-black loop and the `±8` tolerance both correctly account for
plausible GPU floating-point/blend-order variance across 3 different backend implementations without
being so loose the test would pass a genuinely broken formula — verified by hand-computing that a
"VertexColorEnabled forced true" (i.e. the pre-Task-889 bug, where case (b) would incorrectly
produce the same result as case (a)) would fail case (b)'s check by `|122-96|=26` /
`|82-32|=50` / `|163-32|=131`, all far outside the `±8` tolerance — the test would have genuinely
caught the regression it claims to guard.

### Testing
Both cases are precise, correctly-scoped pixel assertions with independently-verified expected
constants (see Behavioral correctness). Coverage gap: `FogEnabled` is never exercised by this file
(defaults to `false`), so it cannot reveal the separately-confirmed cross-backend fog-formula defect
(`AUDIT_CROSS_CUTTING_FINDINGS.md`'s "pre-Task-1111 fog formula... fixed in EasyGL but never ported
to Bgfx or Vulkan") — not a flaw in this test (fog isn't its subject), but worth noting for
completeness since `DualTextureEffect` does have its own `FogVector`/`FogColor` parameters that a
dedicated fog test for this specific effect does not appear to exist for in this shard.

### Cross-file consistency
`DualTextureEffect::FillGpuDrawParams()` was read in full and cross-checked against all 3 backends'
actual shader source rather than assumed — a genuine cross-file verification, not a boilerplate
citation.

## Detailed Findings

None found — no CRITICAL/HIGH/MEDIUM/LOW defects identified. This is a well-constructed, verified
test of a correctly-implemented feature.

## Cross-File Observations

- `DualTextureEffect`'s `VertexColorEnabled` property is correctly implemented with getter/setter
  wrappers, in contrast to `BasicEffect::VertexColorEnabled`'s bare-field API-design lapse
  (`AUDIT_CROSS_CUTTING_FINDINGS.md`) — worth noting as a positive counter-example showing the
  correct convention *is* followed elsewhere in the same effect family, which strengthens the case
  that `BasicEffect`'s lapse is an isolated oversight rather than a project-wide pattern.
- Shares the `RasterizerState::CullNone` / "Task 896" CCW-winding workaround comment with the other
  3 files in this audit batch that draw explicit-winding quads (`cross_backend_diagnostic_scene.cpp`
  uses a differently-wound triangle so doesn't need it; `environmentmapeffect_alphascaledlerp_test.cpp`
  and `graphicsdevice_clear_depth_test.cpp` both cite the identical finding) — consistent, correct
  usage across the batch.
- This file's own header comment explicitly cross-references `graphicsdevice_clear_depth_test.cpp`
  ("mirroring Task 950's own... pattern") for its "one shared source, three backend registrations"
  structure — verified accurate; both files do follow that exact registration shape.

## Missing or Weak Tests

- No dedicated `DualTextureEffect` fog test appears in this shard (see Testing section) — a gap
  worth flagging given the confirmed multi-backend fog-formula defect already documented in
  `AUDIT_CROSS_CUTTING_FINDINGS.md` for other 3D effects; `DualTextureEffect.fx`'s own `PSDualTexture`
  does apply fog in real FNA, so this is a plausible additional instance of that same bug that no
  current test would catch.

## Positive Findings

- Precise, independently-verified pixel expectations for both cases, matching the actual current
  production formula on all 3 backends it's registered against.
- Correctly demonstrates that a naive re-introduction of the Task 889 regression would be caught
  (verified by this audit's own tolerance-margin calculation, not merely assumed).
- Strong header-comment documentation quality, directly citing the exact FNA HLSL source lines and
  the specific regression being guarded.
- `DualTextureEffect.VertexColorEnabled`'s correct getter/setter implementation stands as a useful
  positive counter-example to the `BasicEffect` bare-field issue tracked elsewhere.

## Final Assessment

A strong, accurate, well-verified test with no defects found in either the test file itself or the
production code paths it exercises across all 3 backends it's registered on. The only actionable
observation is a coverage gap (no `DualTextureEffect`-specific fog test in this shard), not a defect
in this file.
