# Audit: examples/vulkan_basiceffect_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect` linear-fog pixel integration test
  (Task 888), stride-32 `lit_textured3d` pipeline
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_vulkan_basiceffect_fog` /
  `Vulkan_BasicEffect_Fog`, `cmake/Tests/VulkanTests.cmake:546-548`)
- XNA/FNA relevance: direct — `BasicEffect.FogEnabled`/`.FogColor`/`.FogStart`/`.FogEnd`
- FNA reference: `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetFogVector()` (lines 117-142),
  `FNA/src/Graphics/Effect/StockEffects/HLSL/Common.fxh::ComputeFogFactor()`/`ApplyFog()` (lines 9-18)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp:135-140`, fog fields forwarded unmodified), `src/CNA/Internal/Backends/Vulkan/shaders/
  lit_textured3d.vert.glsl` (lines 41-63, fog-factor computation) and `lit_textured3d.frag.glsl` (lines 27-30,
  78-79, fog blend), `VulkanGraphicsBackend.cpp` `EnsureLit*`/UBO-population code around line 3611-3652 and
  7549-7575.

## Purpose

3-check pixel test for `BasicEffect`'s linear fog on the stride-32 (`VertexPositionNormalTexture`) lit-textured
Vulkan pipeline, with `LightingEnabled=false` so only the fog term is exercised: (a) fog disabled → pure blue;
(b) `Z=0.5`, `FogStart=0`, `FogEnd=1` → 50% mix with `FogColor`=red, expecting `(128,0,128)`; (c) `Z=0.9`,
`FogEnd=0.5` (beyond the far plane) → fully fogged, expecting pure red. The file's own header (lines 1-21)
documents that fog was a "total GPU no-op" fixed here for `lit_textured3d` under Task 888, with
colored3d/textured3d/colored_textured3d/dual_texture3d deferred to Task 899 (now closed — see
`examples/vulkan_basiceffect_textured3d_fog_test.cpp`, audited alongside this file).

## Executive Verdict

**Significant correctness risk** — the shader's fog-factor formula (`(FogEnd - Z)/(FogEnd - FogStart)`, raw
object-space `Z`) is provably **not** the real XNA/FNA fog formula, and is the *exact* formula pattern this
project's own `fix(Task 1111)` commit explicitly identified as broken in the sibling EasyGL backend ("never
actually equivalent to FNA even for `FogStart<FogEnd`") — a fix that was never propagated to Vulkan. The three
checks in this file pass only because their expected values were derived from the same (wrong) formula the
shader implements, not independently from FNA. This is not a hypothetical edge case: it reproduces for this
test's own "ordinary" `FogStart=0` parameterization.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` are real
`IEffectFog` members, used with the correct `Vector3` type for `FogColor` (line 47-50, 92, 103). `FogColor`
is passed through unmodified by `BasicEffect::FillGpuDrawParams()` (`BasicEffect.cpp:136-140`) into the GPU UBO
— API surface itself is correct. The defect is in the shader math consuming these values, detailed below.

### Behavioral correctness — independent re-derivation against FNA
Traced `EffectHelpers.SetFogVector()`: with `World=View=Identity` (as this test sets, lines 95-97),
`worldView=Identity`, so `scale=1/(FogStart-FogEnd)`, `FogVector=(0,0,scale,(0+FogStart)*scale)`.
`Common.fxh::ComputeFogFactor(position)=saturate(dot(position,FogVector))=saturate(scale*(z+FogStart))`, and
`ApplyFog` does `color=lerp(sceneColor,FogColor*alpha,fnaFactor)` (`fnaFactor=1`→full fog).

Plugging in this test's own case (b) values (`FogStart=0, FogEnd=1, Z=0.5`): `scale=1/(0-1)=-1`,
`fnaFactor=saturate(-1*(0.5+0))=saturate(-0.5)=0` → **fully unfogged, pure blue** is what real FNA/XNA would
render here. Case (c) (`FogStart=0, FogEnd=0.5, Z=0.9`): `scale=-2`, `fnaFactor=saturate(-2*0.9)=saturate(-1.8)
=0` → **also fully unfogged, pure blue** under real FNA math. Both are the *opposite* of what this test asserts
and what the current shader renders (50% purple / pure red, respectively).

Now trace what the shader actually computes (`lit_textured3d.vert.glsl:61-63`):
`fragFogFactor=clamp((FogEnd-Z)/(FogEnd-FogStart),0,1)`, blended via
`mix(FogColor,color,fragFogFactor)` (`lit_textured3d.frag.glsl:79`, `fragFogFactor=1`→keep original color).
Converting to the same "CNA convention" as FNA for direct comparison (`CNA_factor = 1-fnaFactor`, matching the
sibling EasyGL derivation): algebraically, `CNA_factor(z) ≡ 1-fnaFactor(-z)` — i.e. **the CNA/Vulkan shader
evaluates the correct FNA-equivalent quantity at the wrong (sign-flipped) `Z`**, not merely "close" or
"coincidentally different." This is provable directly, not inferred: `EasyGL_factor(z)=(z+FogEnd)/(FogEnd-
FogStart)` was the `Task 1111`-superseded, confirmed-broken EasyGL formula (see git evidence below);
`Vulkan_factor(z)=(FogEnd-z)/(FogEnd-FogStart)=EasyGL_factor(-z)` — an exact Z-mirror of that already-fixed-
elsewhere-and-called-out-as-wrong formula.

**Git evidence, not speculation**: `git log -1 74ad3bae` = `fix(Task 1111): correct EasyGL's fog formula to
genuinely match FNA, not just at z=0` (2026-07-16), whose commit message states verbatim: *"the old numerator
(FogEnd-z) was never actually equivalent to FNA's real dot-product formula, only coincidentally right at each
test's own z=0 midpoint."* `git show 74ad3bae --stat` touches only 5 `examples/easygl_*_fog_test.cpp` files plus
`EasyGLGraphicsBackend.cpp` — **not** this file or any Vulkan shader (`git show 74ad3bae -- examples/
vulkan_basiceffect_fog_test.cpp` produces zero diff). Task 888/899 (Vulkan fog, `401deaed`/`c2386302`,
2026-07-07) predate Task 1111 by 9 days and were never revisited. Today's audit date context is 2026-07-18 — this
is a live, current defect, not something since fixed.

This file's own header comment (line 13: *"matches EasyGL's already-tested formula exactly, Task 195"*) is
**stale**: it matches EasyGL's Task-195-era formula, which Task 1111 subsequently proved wrong and replaced.
The claim was true when written (2026-07-07) and is false as of the current tree.

### Logic
Three-case structure (disabled / half / full) is a reasonable shape for a fog test in the abstract, but two of
the three cases (b, c) assert values that are wrong relative to true FNA/XNA behavior for the reasons above;
only case (a) (fog disabled, formula not exercised at all) is unconditionally correct.

### C++ correctness
`matches(...,tol=30)` (lines 76-81) is loose enough that even the *correct* FNA answer (pure blue, `(0,0,255)`)
and the *asserted* wrong answer (`(128,0,128)`) would not both pass — this is not a tolerance-masking issue like
some sibling files' findings; the shader's actual (wrong) output and the test's asserted (also wrong, but
consistent with the shader) values genuinely agree with each other, just not with real FNA.

### Robustness
N/A — no malformed-input path; the defect is a formula-correctness issue, not a crash/exception path.

### Testing
This file cannot catch its own root-cause bug because its expected constants were (evidently) derived from the
same wrong shader, not from independent FNA math — the file is simultaneously "self-consistent" and "wrong
relative to the real API it claims to validate." See Detailed Findings.

## Detailed Findings

### F1 — Vulkan's fog-factor formula is not equivalent to FNA's `SetFogVector`/`ComputeFogFactor`, reproducing the exact bug Task 1111 already fixed in EasyGL but never ported to Vulkan

- Severity: HIGH
- Confidence: HIGH (independently re-derived the FNA formula from source, algebraically compared it to both the
  current Vulkan shader and the historical/Task-1111-superseded EasyGL formula, and corroborated with `git log`/
  `git show` evidence that Task 1111 postdates and does not touch the Vulkan files)
- Category: correctness / FNA-parity / stale-comment
- Location/symbol: `lit_textured3d.vert.glsl:61-63` (`fragFogFactor` computation), mirrored in
  `lit_textured3d_vertexlit.vert.glsl:60-62`, `textured3d.vert.glsl:36-38`, `colored3d.vert.glsl:39-41`,
  `colored_textured3d.vert.glsl:37-40`, `dual_texture3d.vert.glsl:40-42` (all use the identical `(FogEnd-z)/
  (FogEnd-FogStart)` pattern, confirmed by direct grep across the Vulkan shaders directory); test-side assertion
  at this file's lines 147-153; stale claim at line 13.
- Evidence: see the full derivation above. Concretely, for this file's own case (b) (`FogStart=0, FogEnd=1,
  Z=0.5`), the real FNA formula evaluates to fully-unfogged pure blue, while this test (and the current shader)
  assert/render a 50% red/blue mix `(128,0,128)`; for case (c) (`FogStart=0, FogEnd=0.5, Z=0.9`), FNA evaluates
  to pure blue again, while this test asserts pure red.
- Why it matters: this is not a boundary case — it affects the test's own "typical" `FogStart=0` parameterization
  in both non-trivial checks it has. Any real game using `BasicEffect` fog on the Vulkan backend with a
  `FogStart` at or near 0 (an extremely common convention, since `FogStart` is usually measured as a distance
  from the camera starting near 0) would render fog that is either backwards or entirely absent relative to
  real XNA/FNA behavior, silently diverging from the reference platform this project exists to replicate.
- FNA/XNA comparison: direct — `EffectHelpers.cs::SetFogVector()` (lines 117-142), `Common.fxh::
  ComputeFogFactor()`/`ApplyFog()` (lines 9-18), full derivation above.
- Related files: `docs/xna-4-api-coverage.md` (Vulkan row) and `docs/graphics-backend-feature-matrix.md` both
  currently describe Vulkan fog as fully closed/pixel-verified (Task 899) with no caveat — this finding shows
  that characterization is materially incomplete.
- Suggested future action (not implemented by this audit): port Task 1111's corrected formula
  (`saturate((z+FogEnd)/(FogEnd-FogStart))`, in EasyGL's own inverted-mix convention) to every affected Vulkan
  vertex shader, and re-derive this test's own expected constants from the corrected formula (which, per the
  derivation above, would make cases (b) and (c) both render pure blue for the currently-chosen `FogStart=0`
  parameters — the test would need new `FogStart`/`FogEnd`/`Z` values, e.g. straddling zero as the EasyGL test
  does, to exercise a genuine partial-fog case at all).

### F2 — Even a corrected formula would still ignore `World`/`View` (object-space `Z` only), an already-documented-for-EasyGL, undocumented-for-Vulkan limitation

- Severity: MEDIUM
- Confidence: HIGH
- Category: architecture / FNA-parity / documentation-gap
- Location/symbol: same shader sites as F1; `docs/easygl_bugs.md:48` ("fog shaders — **diverges** — Fog is
  computed on `aPos.z`... not on view-space depth. XNA fog is linear in view-space distance. Results diverge at
  oblique viewing angles.")
- Evidence: FNA's real formula dots the *object-space* vertex position against a `FogVector` that itself encodes
  the full `World*View` matrix's Z-column (`worldView.M13/M23/M33/M43` in `SetFogVector`), so the true fog
  quantity is genuine view-space depth. The Vulkan shaders here use `inPos.z` directly, with no `World`/`View`
  transform applied to it anywhere in the fog term (confirmed by reading `lit_textured3d.vert.glsl` in full: the
  only per-vertex transform involving `World` is `fragWorldPos`/`fragNormal`, computed separately and never fed
  into `fragFogFactor`). This is architecturally identical to EasyGL's own documented "diverges" limitation, but
  `docs/easygl_bugs.md` has no Vulkan counterpart file recording it for this backend.
- Why it matters: this test (and its `textured3d_fog_test.cpp` sibling) exclusively uses `World=View=Identity`,
  so neither test can or does exercise this limitation — any real scene with a moving camera or a translated/
  rotated object would get fog that ignores the object's actual distance from the camera entirely.
- FNA/XNA comparison: `EffectHelpers.SetFogVector()`, general (non-identity) case.
- Suggested future action: note this limitation in a `docs/vulkan_bugs.md` (or equivalent) analogous to
  `docs/easygl_bugs.md`, and/or add a fog+non-identity-View test case to surface it explicitly rather than
  leaving it implicit.

## Cross-File Observations

- `examples/vulkan_basiceffect_textured3d_fog_test.cpp` (audited alongside this file) shares the identical
  formula, the identical stale "matches EasyGL... Task 195" claim, and the identical F1/F2 findings — see that
  report for the `textured3d.vert.glsl` (stride-20) instance of the same bug.
- `examples/easygl_basiceffect_fog_test.cpp.audit.md` (prior batch) independently verified the *current*
  (post-Task-1111) EasyGL formula as algebraically correct — this file's own stale comment references that
  correctness by name without reflecting that the underlying formula it claims to match has since changed.
- The retry-until-non-black loop (`renderQuad`, lines 121-132) is a defensive pattern shared with every sibling
  file in this batch; not itself a concern.

## Missing or Weak Tests

- No case exercises a non-identity `View` or `World` (see F2) — the entire fog-formula defect class (F1+F2) is
  structurally invisible to this file's own assertions.
- No case tests `FogStart==FogEnd` (FNA's documented degenerate "force fully fogged" case,
  `EffectHelpers.cs:119-122`), matching the same gap already noted in the EasyGL sibling's audit.

## Positive Findings

- The file's own header is transparent about scope (explicitly limits itself to the `lit_textured3d` pipeline,
  defers colored3d/textured3d/etc. to Task 899/the sibling file) — this cross-reference is accurate and
  verifiable (confirmed `vulkan_basiceffect_textured3d_fog_test.cpp` exists and covers exactly that gap).
- The `RasterizerState::CullNone` workaround comment (lines 106-108, "Task 896 finding") is accurate and
  current: confirmed via `RasterizerState.cpp` (default `CullMode::CullCounterClockwiseFace`, matching FNA's own
  default) and `VulkanGraphicsBackend.cpp`'s pipeline cull-mode mapping that this quad's winding would indeed be
  culled without it.

## Final Assessment

The test is internally consistent and its mechanics (retry loop, tolerance, RasterizerState workaround) are all
sound, but its three expected values are certified against a shader formula that is provably not equivalent to
real XNA/FNA fog math — the exact bug pattern this project already found and fixed in the sibling EasyGL backend
under Task 1111, dated after but never applied to Vulkan. This is a genuine, currently-live rendering-correctness
defect that this test's own passing status actively obscures rather than catches.
