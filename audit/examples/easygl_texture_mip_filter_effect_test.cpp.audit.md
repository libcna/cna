# Audit: examples/easygl_texture_mip_filter_effect_test.cpp

## Metadata

- Source file: `examples/easygl_texture_mip_filter_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 298, mipmap filter behavior
  (`LinearMipPoint`/`Point`) on a 3D stock effect (`DualTextureEffect`)
- File type: hand-rolled `Game`-subclass executable, CTest-registered as
  `cna_test_easygl_texture_mip_filter_effect` (`cmake/Tests/EasyGLTests.cmake:1310-1312`).
- XNA/FNA relevance: `TextureFilter::LinearMipPoint`/`Point`, `Texture2D::SetData(level,...)`,
  `DualTextureEffect` — real XNA 4.0 API.
- Related production code: `EasyGLGraphicsBackend::ApplySamplerState`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:2072-2109`, filter-to-GL-mapping
  switch); `plans/plan_graphics.md` rows 298/925/926 (Vulkan/Bgfx-specific mip-aware `Point` behavior).

## Purpose

Builds a real 8-level, 128×128 mipmapped `Texture2D` with a hand-authored per-level color split
(levels 0-2 solid Red, levels 3-7 solid Green), draws it at a forced-tiny 8×8 on-screen size (a
16:1 minification ratio, `log2(128/8)=4`, solidly inside the Green range), and checks two filter
modes: `LinearMipPoint` (expected to select the correct high mip level → Green) and `Point` (expected
to stay at level 0 → Red, an intentionally-documented current EasyGL limitation, not a bug this test
is trying to catch fresh).

## Executive Verdict

**Healthy** — both expectations were independently checked against the real GL filter-mapping switch
and found accurate; the file's own claim that `Point` "never mip-selects" on this backend today is
current (not stale, unlike the sibling `easygl_texture_anisotropic_effect_test.cpp`'s comment — this
audit specifically cross-checked whether the same kind of staleness applied here and found it does
not, per `plans/plan_graphics.md` rows 925/926 explicitly stating EasyGL's flat `Point` mapping "is
EasyGL-specific ... a separate still-open limitation").

## Checklist Results

### Behavioral correctness
- Mip-level construction (`Initialize()`, lines 82-90): `Texture2D(device, 128, 128, mipMap=true,
  SurfaceFormat::Color)` produces 8 levels (`128,64,32,16,8,4,2,1` — `log2(128)+1=8`), matching the
  header comment exactly. Each level is filled via `SetData(level, nullptr, px.data(), 0,
  px.size())` with `dim = 128 >> level` pixels of the level-appropriate solid color — correct use of
  the per-level `SetData` overload, and (per this audit's own cross-file check for the sibling
  Vulkan/Bgfx test rows) this specific overload is the one Task 924/925/926 fixed to actually reach
  the GPU on every backend, not just EasyGL — relevant context, not itself exercised by this file.
- `LinearMipPoint` maps to `ApplySamplerState` case `3`: `minF=LinearMipmapNearest, magF=Linear` — a
  genuinely mip-aware minification filter (nearest single mip level selected via the real GPU
  derivative-based LOD calculation, no cross-level blend) — correctly matches the header comment's
  description ("mip=point: a single mip level is selected, not blended across levels").
- `Point` maps to the `case 1:` branch: `minF=Nearest, magF=Nearest` — **no** `_MIPMAP_` suffix,
  i.e. never mip-aware regardless of minification amount — independently confirmed directly in the
  switch statement, matching the header comment's claim precisely ("CNA's current EasyGL/Vulkan
  backends deliberately map `TextureFilter::Point` ... to a *non-mip-aware* GPU filter").
- `IsRed`/`IsGreen` (lines 65-66) use a wide `>=200`/`<=40` threshold pair with a large dead zone,
  appropriate for a flat solid-color-per-level texture with no blending expected in either case.

### Logic
`DrawTinyQuadAndSample` (lines 96-130) clears to blue before each of the two sub-draws (line 99,
"neither expected result") — a deliberate choice that would surface a failed/skipped draw as a
third, distinct (and clearly-failing) color rather than accidentally matching a stale
previous-frame's Red/Green from the other filter's draw. This is a real, meaningful defensive design
choice, not incidental.

The 8×8 quad size and forced LOD (`log2(128/8)=4`) sits solidly inside the Green range `[3,7]`,
matching the file's own stated "wide safety margin on both sides against driver-specific derivative
rounding" — this audit did not find a scenario where legitimate GPU-driver LOD-calculation jitter
(±1 level around the ideal derivative estimate) could push the sampled level outside `[3,7]` given
the stated 4-level margin on the low side and 3-level margin on the high side.

### Cross-file consistency
Confirmed this file is intentionally **not** reused verbatim across backends — unlike the
anisotropic and Point-vs-Linear tests in this same shard —
`cmake/Tests/VulkanTests.cmake:81-90`'s own comment states a Vulkan-specific adaptation
(`vulkan_texture_mip_filter_effect_test.cpp`) exists instead, because Vulkan's `Point` filter has
"always been mip-aware," so a Vulkan copy of this exact file would need the opposite expectation for
its second check. This is architecturally correct and avoids a false-negative/false-positive if the
EasyGL-specific version were blindly reused.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — The documented EasyGL/Vulkan/Bgfx `Point`-filter mip-awareness gap is real and still open

- Severity: INFO (documentation/tracking note, not a defect in this test file)
- Confidence: HIGH
- Category: FNA parity (context, not a finding against this file itself)
- Location/symbol: header comment lines 15-27
- Evidence: real XNA/D3D9 `TextureFilter::Point` semantics select the nearest mip level based on
  computed LOD (point filtering on all three axes, including mip selection) — CNA's EasyGL mapping
  (`Nearest`/`Nearest`, no `_MIPMAP_` suffix) deviates from this by design, for the stated reason
  (avoiding GL-incomplete-texture black screens on the common non-mipmapped case, prior to Task 924's
  `GL_TEXTURE_MAX_LEVEL` fix). `plans/plan_graphics.md` row 925 independently confirms Vulkan's `Point` has
  "ALWAYS" been mip-aware (`VK_SAMPLER_MIPMAP_MODE_NEAREST`) — i.e. it's specifically EasyGL/Bgfx that
  diverge from real XNA semantics here, and this test correctly targets and documents the EasyGL case
  only, without overclaiming universality.
- Why it matters: flagged here so the cross-cutting FNA-parity finding is anchored to a concrete,
  currently-accurate test rather than lost — this is exactly the kind of "documented, not yet fixed"
  deviation the checklist's Behavioral-correctness section asks to distinguish from a silent bug.
- Related files: `plans/plan_graphics.md` rows 298/925/926.
- Suggested future action: none from this audit — already correctly tracked as open in the project's
  own planning docs (consulted here only as secondary context per `AUDIT_SCOPE.md`).

## Cross-File Observations

- Shares the `RasterizerState::CullNone` (Task 896) winding-fix idiom with its sibling tests in this
  shard, applied consistently (line 123).
- Correctly avoids the anisotropic test's stale-documentation problem (see
  `easygl_texture_anisotropic_effect_test.cpp.audit.md` in this same batch) by describing a
  limitation that is still genuinely open, cross-checked against both the live switch statement and
  `plans/plan_graphics.md`'s own tracking rows.

## Missing or Weak Tests

None found for this file's stated, narrow scope. A possible (not required) addition would be a third
check at a *less* extreme minification (near the LOD 3/7 boundary) to probe the "safety margin"
claim's actual tightness, but the current 16:1/LOD-4 choice is a reasonable, well-justified default.

## Positive Findings

- Both filter-mapping expectations were independently verified against the real
  `ApplySamplerState` switch statement, not merely trusted from the comment.
- Clean, deliberate "clear to a third, unambiguous color between sub-draws" design choice prevents a
  false pass from stale framebuffer content.
- Correctly does NOT reuse this exact source for Vulkan (a backend-specific fork exists instead),
  avoiding a wrong expectation on a backend where the underlying filter behavior genuinely differs.

## Final Assessment

A well-constructed, accurately self-documenting test of a real, still-open, and correctly-scoped
`TextureFilter::Point` mip-awareness limitation on this specific backend; no correctness or staleness
issues found.
