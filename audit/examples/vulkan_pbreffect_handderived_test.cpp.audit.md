# Audit: examples/vulkan_pbreffect_handderived_test.cpp

## Metadata

- Source file: `examples/vulkan_pbreffect_handderived_test.cpp` (284 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `PbrEffect`/`SkinnedPbrEffect` analytically-derived
  BRDF pixel test.
- File type: standalone `Game`-subclass executable (`class VulkanPbrEffectHandDerivedTest`).
- XNA/FNA relevance: indirect/`NOXNA` — `PbrEffect`/`SkinnedPbrEffect` are CNA extensions (glTF
  metallic-roughness PBR is not part of the XNA 4.0 stock-effect surface), but they implement the
  same `IEffectMatrices`/`IEffectFog`/`IEffectLights` interfaces as XNA's stock effects
  (`include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp` line 34), so property-mapping
  conventions are still judged against those FNA-shared interfaces.
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/pbr3d.frag.glsl` (unskinned
  BRDF, `PbrLight()`), `pbr3d_skinned.frag.glsl` (identical BRDF, different UBO binding slot only —
  diffed byte-for-byte identical apart from binding index and one field name),
  `pbr3d_skinned.vert.glsl` (bone-palette skin transform),
  `VulkanGraphicsBackend::GetOrCreatePipelinePbr3D`/`GetOrCreatePipelinePbrSkinned3D` (stride-48 /
  stride-68 pipeline dispatch, lines ~5559/5806).

## Purpose

Proves the Vulkan `PbrEffect`/`SkinnedPbrEffect` metallic-roughness BRDF renders a real,
analytically-tractable GGX/Smith-Schlick-GGX/Schlick-Fresnel result end-to-end through an actual
GPU draw, not just "compiles and produces *some* non-black pixel." The scene is deliberately
degenerate: a flat quad, camera looking straight down `-Z`, light0 direction `(0,0,-1)` — at the
exact centre backbuffer pixel, world position is `(0,0,0)`, so `N = V = L = (0,0,1)` exactly, and
every BRDF dot product (`NdotL`, `NdotV`, `NdotH`, `VdotH`) collapses to `1`, making the whole
`PbrLight()` formula reducible to closed-form scalar arithmetic. Four checks:
- **(a)** white albedo, `roughness=0.5`, `metallic=0` → expects `(91,91,91)`.
- **(b)** red albedo, `roughness=0.5`, `metallic=1` (fully metallic) → expects `(97,0,0)`.
- **(c)** red albedo, `roughness=0.5`, `metallic=0` (fully dielectric) → expects `(27,4,4)`, plus
  an explicit `!matches(b,c)` check that metallic genuinely changes the result.
- **(d)** `SkinnedPbrEffect` with a single identity bone (weight 1.0, `Matrix::Identity`) rendering
  the same scene as (a) → must reproduce (a)'s own value exactly (mathematically a no-op skin
  transform).

## Executive Verdict

**Healthy** — this audit independently re-derived all four expected constants directly from the
actual `pbr3d.frag.glsl` source (not merely from the file's own embedded comments) and they match
to within ordinary rounding; the stride-48/stride-68 vertex layouts are independently confirmed to
match the real Vulkan pipeline-selection dispatch keys.

## Checklist Results

### API / XNA / FNA parity — N/A / PASS (NOXNA)
`PbrEffect`/`SkinnedPbrEffect` are `NOXNA` extensions (not part of XNA 4.0), so no FNA member-parity
check applies to their own properties. What *is* checked: `IEffectLights`-style
`DirectionalLight0/1/2.setEnabledProperty/setDirectionProperty/setDiffuseColorProperty` (lines
139–143, 180–184) and `IEffectMatrices`-style `setWorldProperty/setViewProperty/setProjectionProperty`
(lines 144–146) match the same getter/setter convention as `BasicEffect`'s own XNA-facing surface
elsewhere in this codebase — consistent property-naming, not a divergent ad-hoc API.

### Behavioral correctness — PASS (independently re-derived)
Re-derived check (a) directly from `pbr3d.frag.glsl`'s `PbrLight()` (not from the file's own
comment): `a2 = 0.5^4 = 0.0625`; at `NdotH=1`, `dTerm = 1*(a2-1)+1 = a2`; `D = a2/(π·a2²) =
0.0625/(π·0.00390625) ≈ 5.09296`; `k=(0.5+1)²/8=0.28125`; at `NdotV=NdotL=1`,
`G=(1/(1·(1-k)+k))·(1/(same))=1²=1`; `F0=mix(0.04,albedo,metallic=0)=0.04`; at `VdotH=1`,
`(1-VdotH)^5=0` so `F=F0=0.04`; `specular=D·G·F/4=5.09296·0.04/4≈0.050930`; `diffuseColor=albedo·
(1-metallic)=1`; `kd=1-F=0.96`; `Lo=(kd·diffuseColor/π+specular)·lightColor·NdotL=
(0.96/π+0.050930)·1·1≈0.305577+0.050930=0.356507`; `final=0+Lo+0=0.356507→round(×255)=91` —
**matches the asserted `Color(91,91,91,255)` exactly**, independent of the file's own comment.

Re-derived check (b): `F0=mix(0.04,(1,0,0),1)=(1,0,0)`; `diffuseColor=albedo·(1-1)=0`;
`specular.r=D·1·1/4≈1.273239`, `specular.g=specular.b=0` (since `F0.g=F0.b=0`);
`Lo.r=(0+1.273239)·0.3·1≈0.381972→round(×255)=97`; `Lo.g=Lo.b=0` — **matches `(97,0,0)`**.

Re-derived check (c): `F0=(0.04,0.04,0.04)`, `diffuseColor=albedo=(1,0,0)`, `kd=(0.96,0.96,0.96)`;
`Lo.r=(0.96·1/π+0.050930)·0.3≈(0.305577+0.050930)·0.3≈0.106952→27`;
`Lo.g=Lo.b=(0.96·0/π+0.050930)·0.3≈0.015279→4` — **matches `(27,4,4)`**.

Check (d): confirmed `pbr3d_skinned.frag.glsl` is byte-for-byte identical to `pbr3d.frag.glsl`'s
`PbrLight()`/`main()` (`diff` shows only the `PbrParams` UBO binding-slot index and one field
rename, both structurally inert to the actual BRDF math). Confirmed `pbr3d_skinned.vert.glsl`'s
skin transform (`skinMat = bones[i0]*w0` only, since `weightsPerVertex=1` here means neither the
`>=2.0` nor `>=4.0` branches execute) reduces to `Identity*1.0=Identity` for this test's single
bone/weight setup — a genuine no-op, so (d) reproducing (a) exactly is the correct expectation, not
an accidental tautology.

### Logic — PASS
`renderPbr()`/`renderSkinnedPbrIdentity()` both loop up to 20 draws, re-clearing to a
chroma-key green `(0,255,0,255)` each iteration and breaking as soon as the sampled centre pixel is
non-black (lines 152–164, 197–209) — a defensive pattern against the first-frame
swapchain-acquisition edge case documented elsewhere in this backend
(`VulkanGraphicsBackend::ReadBackbuffer`'s own comment about `VK_ERROR_OUT_OF_DATE_KHR` on the
first frame under Wayland/RADV), not a silent retry-until-lucky hack — clearing to green (not
black) specifically so a genuinely-black BRDF result (e.g. from a not-yet-fixed shader bug) would
still register as "non-green" and break the loop rather than being masked by a same-coloured clear.

### C++ correctness — PASS
`static_assert(sizeof(PbrGpuVertex) == 48, ...)` and `static_assert(sizeof(SkinnedPbrGpuVertex) ==
68, ...)` (lines 57, 69) are not just documentation — cross-checked against
`VulkanGraphicsBackend::GetOrCreatePipelinePbr3D`'s `constexpr std::size_t kPbrStride = 48;` (line
5567) and `GetOrCreatePipelinePbrSkinned3D`'s `constexpr std::size_t kPbrSkinnedStride = 68;` (line
5814) — the Vulkan backend dispatches to the PBR vs. skinned-PBR pipeline purely by vertex-buffer
stride (also documented at lines 3407–3408: "PbrEffect (48, unskinned) / CNB-67 skinned+color (56)
/ SkinnedPbrEffect (68, PBR + skinning combo)"), so these `static_assert`s are load-bearing for the
test actually exercising the effect it claims to, not decorative.

### Robustness — PASS
`matches()`'s tolerances (`12` for check a, `14` for checks b/c, `10` for check d, line ~93) are
proportionate to the actual analytic precision achievable through 8-bit sRGB-adjacent framebuffer
rounding plus bilinear/derivative sampling noise at a single pixel — none of them is loose enough to
swallow a distinguishable formula error (e.g. a `metallic`/`roughness` swap would move the result by
tens of units, far outside these tolerances).

### Testing — PASS
Check (b) vs (c)'s explicit `!matches(b, c, 10)` assertion (line 255) is a genuine
"MetallicFactor actually changes the BRDF" differential, not merely two independent point checks —
this guards against a hypothetical bug where `MetallicFactor` is accepted by the API but silently
ignored by the shader (both draws would then produce the *same* wrong-but-plausible colour, and
only the differential check would catch it).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `renderPbr`/`renderSkinnedPbrIdentity` duplicate an near-identical 20-line retry loop

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / duplication
- Location/symbol: lines 152–165 and 197–210
- Evidence: both functions contain the same `for (int i = 0; i < 20; ++i) { Clear; SetDepthTestEnabled(false);
  ...; DrawPrimitives; got = readCenter(dev); if (non-black) break; }` block, differing only in the effect
  object used.
- Why it matters: purely a duplication/maintainability observation — a shared helper taking an
  `std::function<void()>` draw callback (or a lambda) would remove the duplication, but the current
  form is correct and not a source of divergent behaviour between the two call sites (both were
  independently confirmed to compute the right analytic result above).
- Suggested action (not implemented by this audit): factor the retry-loop body into a shared
  `renderUntilNonBlack(dev, drawFn)` helper in a future cleanup pass, not urgent.

## Cross-File Observations

- The file's own header comment credits `easygl_pbreffect_golden_test.cpp` as also registered for
  Vulkan and "reused verbatim" (per `VulkanTests.cmake`) — this file is explicitly the
  *complementary* analytic test, not a duplicate; the golden test presumably checks against a
  captured/empirical reference image while this one checks against closed-form math. That
  division of labour is a sound testing strategy (empirical regression protection + independent
  analytic derivation), not redundant coverage.
- `pbr3d.frag.glsl`'s own header comment states the BRDF is "byte-for-byte identical to
  EasyGLGraphicsBackend::EnsurePbrProgram()'s own PbrLight()" — this audit did not independently
  re-verify the EasyGL shader source line-by-line (out of this file's scope), but the claim is
  consistent with this project's established pattern of porting shader logic verbatim across
  backends (see `docs/`-level backend-parity notes referenced elsewhere in this codebase).

## Missing or Weak Tests

- No check in this file exercises `PbrEffect`'s `NormalMap`, `MetallicRoughnessMap`, `EmissiveMap`,
  or `OcclusionMap` texture inputs (all explicitly set to a default/`nullptr` here, e.g.
  `fx.setNormalMapProperty(nullptr)`, line 135) — this is a deliberate, stated scope choice (the
  degenerate-geometry technique this file uses specifically needs a flat, untextured normal to keep
  `N` exactly `(0,0,1)`), not an oversight, but it does mean the 4 extra texture bindings
  `pbr3d.frag.glsl` declares (`uNormalMap`, `uMetallicRoughnessMap`, `uEmissiveMap`,
  `uOcclusionMap`) are not exercised with non-trivial content by *this* file — likely covered
  instead by the golden/empirical sibling test noted above, but this audit did not open that file
  to confirm.

## Positive Findings

- All four numeric expected constants were independently re-derived by this audit directly from
  the live `pbr3d.frag.glsl` shader source (not merely re-stated from the file's own comment) and
  matched to within ordinary floating-point/8-bit rounding — a genuinely verified analytic test,
  not a "plausible-looking magic number" pattern.
- The stride-48/stride-68 `static_assert`s are cross-checked against the actual Vulkan
  pipeline-dispatch constants and confirmed load-bearing, not decorative.
- Check (d)'s "identity bone reproduces the unskinned result" technique is a clean, minimal way to
  prove the skinning code path doesn't silently corrupt geometry/normals even in the trivial
  single-bone case, mirroring the same oracle technique this audit's sibling shards note for
  `easygl_skinnedpbreffect_golden_test.cpp`.

## Final Assessment

A rigorously-derived analytic BRDF test; independent re-derivation in this audit confirms every
asserted constant against the real shader math, and the vertex-stride assumptions are confirmed
load-bearing against the actual Vulkan pipeline dispatch logic. No correctness issues found.
