# Audit: examples/d3d9_drawex_test.cpp

## Metadata

- Source file: `examples/d3d9_drawex_test.cpp` (719 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — real effect-aware `DrawPrimitivesEx`/
  `DrawIndexedPrimitivesEx` dispatch for all 5 XNA Stock Effects (`plans/plan_dx9.md` D9-8, D9-82b/c/d/e/f).
- File type: `Game`-subclass executable (`D3D9DrawExTest : public Game`), 16 checks (A-J, M-R, plus
  4 sub-checks under "Check F"), CTest-registered as `D3D9_DrawEx`
  (`cmake/Tests/D3D9Tests.cmake:60-63`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
  `EnvironmentMapEffect`, `SkinnedEffect` pixel formulas, all hand-derived from the vendored
  `BasicEffect.fx`/`AlphaTestEffect.fx`/`DualTextureEffect.fx`/`EnvironmentMapEffect.fx`/
  `Lighting.fxh` (exempt from this audit's own scope per D-5 — vendored verbatim — but consulted
  here as the oracle for the test's own hand-derived expected pixel values).
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9EffectDraw.cpp` (1163 lines, read
  in full), `D3D9ShaderDispatch.cpp`, `D3D9GraphicsBackend.hpp`/`.cpp` — all consulted directly.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only. No build or execution was
attempted in this Linux sandbox. Every numeric claim below was independently re-derived by hand
from the actual production C++ (`D3D9EffectDraw.cpp`) and, for the vendored shaders' own formulas,
from the test file's own documented derivation (per D-5, the `.fx` files themselves are out of this
audit's scope; only the CNA-side constant-upload/dispatch C++ consumer code was verified).

## Purpose

The largest and most substantive file in this batch: 16 lettered checks (A, B, C, D, E, G, H, I, J,
M, N, O, P, Q, R, plus 4 sub-checks under "Check F") proving `D3D9GraphicsBackend::
DrawPrimitivesExImpl()`'s full effect-aware dispatch cascade — `BasicEffect` (unlit/textured,
unlit+vertex-color+textured, two lighting buckets, fog, `PreferPerPixelLighting`),
`AlphaTestEffect` (Less-compare pass/fail, Equal-compare with vertex-color bucket),
`DualTextureEffect` (the doubling-blend formula, D3D9-only stride-28 layout), `EnvironmentMapEffect`
(two lighting buckets), `SkinnedEffect` (two lighting buckets, trivial-Identity-bone no-op
skinning), a render-target-sampled-as-texture regression check (Check Q), and 4 "honest gap"
throw checks for unsupported flag/stride combinations.

## Executive Verdict

**Needs attention** — every pixel-value check (A, B, C, D, E, G, H, I, J, M, N, O, P, Q, R) was
independently re-derived against the current `D3D9EffectDraw.cpp` and is correct. However, this
file's own top-of-file overview comment is **stale and self-contradicting relative to the file's
own Check O/P** (F1 — a real, confirmed documentation-rot defect, not merely a stylistic nit), and
3 of "Check F"'s 4 sub-checks assert an exception is thrown for a reason **different from the one
their own inline comments claim**, meaning they provide no actual coverage of the specific
vertex-layout-mismatch guard they say they are proving (F2).

## Checklist Results

### API / XNA / FNA parity
Confirmed correct dispatch priority: `params.pbr` > `alphaTest` > `dualTexture` > `envMapping` >
`skinned` > `BasicEffect` fallback (`D3D9EffectDraw.cpp` lines 561-605), matching
`D3D11GraphicsBackend::DrawPrimitivesExImpl`'s own established cascade per this file's own comment
(line 4). `GpuDrawParams` defaults (`vertexColorEnabled=true`, `textureEnabled=false`,
`lightingEnabled=false`, `weightsPerVertex=4`, `fogEnabled=false`, `specularColor={1,1,1}`,
confirmed directly in `IGraphicsBackend.hpp`) are correctly accounted for in every check's
comments (e.g. "`vertexColorEnabled = false; // GpuDrawParams defaults this to true`" appears
correctly and consistently wherever the test needs to override the default).

### Behavioral correctness — pixel-value checks (all independently re-derived and confirmed correct)
- **Check A** (lines 216-234, unlit+textured): `texture(200,120,40)*DiffuseColor(1,1,1,1) =
  (200,120,40)` exactly. Confirmed against `DrawBasicEffectEXT`'s stride-20 `comboOk` branch
  (`D3D9EffectDraw.cpp` line 632: `!lightingEnabled && !vertexColorEnabled && textureEnabled`) and
  its `DiffuseColor` upload (no lighting term to fold in for the unlit path).
- **Check B** (lines 236-257, unlit+vertex-color+textured, opaque white vertex color): same result
  as A since white is a no-op multiplier, but genuinely exercises the stride-24 `VSBasicTxVc`
  pair — confirmed against the stride-24 `comboOk` branch (line 633).
- **Check C** (lines 259-287, `VertexLighting` bucket, TWO lights): `diffuse sum = 1*0.5+1*0.25 =
  0.75` → `texture*0.75 = (150,90,30)`. Re-derived by hand: `dotL0 = dot(-lightDir0, N) =
  dot((0,0,1),(0,0,1)) = 1`, so light0's contribution is `1*0.5=0.5`; light1 likewise
  contributes `1*0.25=0.25`; sum `0.75`; `200*0.75=150`, `120*0.75=90`, `40*0.75=30` — exact match.
  `ComputeOneLightEXT` (line 179-188) correctly resolves `false` here since `light1Diffuse` is
  non-zero, so `ComputeBasicEffectShaderIndex(...,oneLight=false)` selects the two-light
  (`ShaderIndex 12/13`) bucket, not the one-light bucket — confirmed the two checks (C/D) are
  genuinely exercising different compiled shaders, not the same one with different inputs.
- **Check D** (lines 289-314, `OneLight` bucket, ONE light): `diffuse sum = 1*0.4=0.4` →
  `(80,48,16)` exactly — light1/light2 left at their zero defaults, `ComputeOneLightEXT` correctly
  resolves `true`. Different numeric result from Check C (150,90,30 vs. 80,48,16) genuinely proves
  distinct bucket selection, not a coincidental match.
- **Check E** (lines 316-341, fog, forced fully-fogged): `fogStart=1.0`, `fogEnd=0.0`, geometry
  Z=0, `World=View=Identity`. Re-derived `ComputeFogVectorEXT` (`D3D9EffectDraw.cpp` lines 149-169)
  by hand: `scale=1/(1-0)=1`; `worldView=Identity` ⇒ `M13=0,M23=0,M33=1,M43=0`; `fogVector=
  (0,0,1,1)`. **This formula is byte-for-byte identical to FNA's real
  `EffectHelpers.SetFogVector`** (`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/
  StockEffects/EffectHelpers.cs` lines 117-141: `scale=1/(fogStart-fogEnd)`;
  `fogVector.X=worldView.M13*scale`; `.Y=worldView.M23*scale`; `.Z=worldView.M33*scale`;
  `.W=(worldView.M43+fogStart)*scale`) — independently confirmed by direct comparison against the
  FNA reference tree, not merely asserted. With geometry at `(x,y,0)`, `dot(fogVector,(x,y,0,1)) =
  0+0+0+1 = 1` (fully fogged, `fogFactor` saturates to 1), so `lerp(color, FogColor, 1) =
  FogColor = (40,80,120)` exactly, matching the check's expected value. **This is a positive,
  cross-cutting-relevant finding**: unlike the confirmed-buggy mirror-image fog formula found in
  Bgfx/Vulkan (`AUDIT_CROSS_CUTTING_FINDINGS.md`), D3D9's `ComputeFogVectorEXT` uses the CORRECT
  real FNA formula. This backend does not share that defect.
- **Checks G/H/I** (`AlphaTestEffect`, lines 343-421): Check G (Less, alpha 64/255=0.251 <
  refVal 0.5, passes) and Check H (Less, alpha 64/255=0.251 > refVal 0.1, the "<" test is false,
  discards) both correctly exercise `AlphaTest.z`/`.w` selection (confirmed the `alphaTest[]`
  array values match the documented `clip((cmp)?z:w)` contract in the test's own comments and the
  `needsAlphaTest = (alphaTest[3]<0 || alphaTest[2]<0)` dispatch gate at
  `D3D9EffectDraw.cpp` line 569). Check I (Equal, exact-value tolerance match, vertex-color
  bucket) uses `tolerance=0.1>0` specifically to signal the Equal/NotEqual bucket per the test's
  own comment — a real, deliberate use of the `isEqNe` signal this project's D9-81 established.
- **Check J** (`DualTextureEffect`, lines 423-450): `PSDualTexture`'s real formula
  (`color=tex0; color.rgb*=2; color*=tex1*Diffuse`) re-derived: `(1,1,1,1)*2 *
  (100/255,60/255,20/255,1) * (0.5,0.5,0.5,1) = (100/255,60/255,20/255,1)` → `(100,60,20,255)`
  exactly, matching the check's expected value and the production `DrawDualTextureEffectEXT`'s
  comment (line 908-911) describing the identical "classic XNA lightmap-doubling blend."
- **Checks M/N** (`EnvironmentMapEffect`, lines 452-514): re-derived `lerp(texColor, envmap,
  envMapAmount)` for both the 2-light (M: `diffuse sum=0.5`→`texColor=(100,60,20)`→
  `lerp(...,0.5)=(100,130,35)`) and 1-light (N: `diffuse sum=0.4`→`texColor=(80,48,16)`→
  `lerp(...,0.5)=(90,124,33)`) buckets — both match the check's expected constants exactly.
  Confirmed `DrawEnvironmentMapEffectEXT` correctly uploads `WorldInverseTranspose` (line 967) in
  addition to `World`/`WorldViewProj` — see the WorldInverseTranspose cross-cutting note below.
- **Checks O/P** (`SkinnedEffect`, lines 516-583): Bone 0 = Identity, 100% weight, so skinning is a
  documented no-op; re-derived the same lit-textured formula BasicEffect's own Check C/D use:
  Check O (2 lights) → `(100,60,20)`; Check P (1 light) → `(80,48,16)` — both match. Confirmed
  `DrawSkinnedEffectEXT` (`D3D9EffectDraw.cpp` lines 1034-1162) uploads `World`/
  `WorldInverseTranspose` (lines 1066-1068) exactly like `EnvironmentMapEffect`'s path, and
  `UploadBonesVS` (lines 519-540) correctly transposes/packs the caller's row-major
  `boneTransforms` into the fixed 72-bone (216-register) `Bones[]` array with unused trailing
  slots left zero — matching this file's own Identity-bone-at-slot-0 setup exactly (`boneCount=1`,
  `weightsPerVertex=1`).
- **Check Q** (lines 585-612, render-target-as-texture regression): re-read
  `ResolveD3D9TextureEXT()` (`D3D9EffectDraw.cpp` lines 95-104) — confirmed it performs a real,
  two-concrete-type `dynamic_cast<const D3D9TextureBackend*>` then
  `dynamic_cast<const D3D9RenderTargetBackend*>` fallback, correctly resolving a
  `D3D9RenderTargetBackend*` (what `params.texture0` actually points to in this check) to its own
  `IDirect3DTexture9*` rather than the previously-live bug of an unconditional
  `static_cast<const D3D9TextureBackend*>` (undefined behavior reading a garbage pointer from the
  wrong class's memory layout). The check's exact-match readback of the render target's own
  cleared color (`200,120,40,255`, same as Check A's texture) is a real, discriminating proof the
  fix works, not merely "didn't crash."
- **Check R** (lines 614-644, `PreferPerPixelLighting=true`): same numeric setup and expected value
  as Check D (`(80,48,16)`) — the test's own comment correctly acknowledges a flat single-normal
  triangle cannot itself distinguish vertex-lit from pixel-lit output (`dot(light,N)` is linear in
  a constant `N`), so this check's real, disclosed purpose is narrower than its name suggests: it
  proves the pixel-lighting bucket's *dispatch and constant-upload plumbing* work at all (both VS
  and PS stages receive `DiffuseColor`/lighting registers), not that the pixel-lit formula itself
  differs from the vertex-lit one at this specific pixel. Confirmed `DrawBasicEffectEXT` uploads
  `DiffuseColor`/`SpecularColor`/`SpecularPower`/`DirLight0-2*`/`EyePosition` to BOTH VS and PS via
  the soft `TryUploadPixelShaderConstantEXT` (lines 669-676 and surrounding code) specifically for
  this bucket — matching the real bug-and-fix this comment documents (FNA's `ComputeLights()` runs
  in the pixel shader only for this one bucket).

### Logic — F1: stale, self-contradicting top-of-file overview comment

- Severity: **MEDIUM**
- Confidence: **HIGH** (directly confirmed by git history)
- Category: documentation-rot / maintainability
- Location: file header, lines 4-7: *"BasicEffect (D9-82b), AlphaTestEffect (D9-82c),
  DualTextureEffect (D9-82d), EnvironmentMapEffect (D9-82e). **SkinnedEffect is D9-82f, not yet
  implemented**; Check F confirms it throws a named not-yet-implemented rather than silently
  drawing the wrong thing..."*
- Evidence: this claim is **directly contradicted by the same file's own Checks O and P**
  (lines 516-583), which perform real `SkinnedEffect` draws with `params.skinned=true` and a valid
  stride-52 buffer, and assert *exact, non-throwing, successful pixel readbacks*
  (`(100,60,20)`/`(80,48,16)`) — not an exception. Confirmed via `git log -p --follow -- 
  examples/d3d9_drawex_test.cpp`: the "not yet implemented" sentence was introduced in commit
  `3e775653` ("close D9-82e -- real EnvironmentMapEffect draw dispatch"), when SkinnedEffect
  genuinely was still unimplemented. Commit `aad51ac4` ("close D9-82f -- real SkinnedEffect draw
  dispatch (all 5 Stock Effects now real)") subsequently added Checks O/P (real, successful
  SkinnedEffect draws) but did **not** update the earlier sentence, which still reads as if
  SkinnedEffect is a stub. Production code confirms O/P are correct: `D3D9GraphicsBackend::
  DrawSkinnedEffectEXT` (`D3D9EffectDraw.cpp` line 1034 onward) is a full, real implementation
  (constant uploads, shader-index dispatch, bone-matrix upload, draw call) — not a throw stub.
  `D3D9GraphicsBackend.hpp`'s own class-level doc comment (lines 29-46) has an analogous stale
  claim ("full effect-aware DrawPrimitivesEx dispatch and SpriteBatch still throw
  NotYetImplemented()... D9-82b/c, D9-90") which is outside this file and thus outside this
  specific report's fix scope, but is worth flagging as the same documentation-rot pattern
  recurring in a second, closely-related file (see Cross-File Observations).
- Why it matters: a future reader (including a future audit pass, or a developer deciding what
  work remains) who reads only this file's own header would incorrectly conclude SkinnedEffect
  drawing is unimplemented in D3D9, when it has in fact been complete since `aad51ac4`. This
  matches a documented, recurring pattern in this codebase
  (`AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Recurring testing gaps" section: "header comments
  describing 'known bugs'/'current limitations' are not revisited once the underlying code is
  fixed," previously confirmed in the EasyGL/SdlRenderer/Bgfx/Vulkan example-test shards) — this
  finding extends that pattern to the D3D9 shard for the first time.
- Suggested future action (not implemented by this audit): update the header sentence to state
  SkinnedEffect dispatch is real (D9-82f closed), and clarify that Check F's SkinnedEffect
  sub-check specifically covers the *unsupported-stride* honest-gap case (see F2), not "SkinnedEffect
  itself is unimplemented."

### Logic — F2: 3 of Check F's 4 sub-checks throw for a different reason than their own comment claims, providing no coverage of the vertex-layout-mismatch guard they say they prove

- Severity: **MEDIUM**
- Confidence: **HIGH** (directly traced against the exact guard-clause order in production code)
- Category: test-coverage / correctness-of-test
- Location: lines 646-695 ("Check F: honest-gap / not-yet-implemented reporting"), specifically
  the DualTextureEffect (lines 663-674), EnvironmentMapEffect (676-684), and SkinnedEffect
  (686-694) sub-checks.
- Evidence: each of these 3 sub-checks constructs a `GpuDrawParams` that sets only the relevant
  effect-selector flag (`dualTexture`/`envMapping`/`skinned` = `true`) and reuses `vb` (the
  stride-20 `kTriTx` buffer from earlier in `Draw()`) — but never sets `texture0`/`texture1`/
  `envMap`. Tracing the actual production dispatch:
  - `DrawDualTextureEffectEXT` (`D3D9EffectDraw.cpp` lines 868-873): `if (!params.texture0 ||
    !params.texture1) throw ...` runs **before** the stride/vertexColor check (`if (stride != 28
    || params.vertexColorEnabled) throw ...`, lines 879-883). Since `dualTexBadCombo` never sets
    `texture0`/`texture1` (both default `nullptr`), the FIRST guard throws — the test's own
    comment ("No matching CNA vertex layout: DualTextureEffect with vertexColorEnabled=true...")
    is never actually reached or exercised.
  - `DrawEnvironmentMapEffectEXT` (lines 928-933): identical shape — `if (!params.texture0 ||
    !params.envMap) throw ...` precedes `if (stride != 32) throw ...` (lines 938-941).
    `envMapParams` never sets `texture0`/`envMap`, so again the null-texture guard fires first.
  - `DrawSkinnedEffectEXT` (lines 1039-1042): `if (!params.texture0) throw ...` precedes
    `if (stride != 52) throw ...` (lines 1046-1049). `skinnedParams` never sets `texture0`, so
    again the null-texture guard fires first.
  In all 3 cases `check(threw, ...)` still passes (an exception genuinely is thrown), but the
  test provides **zero actual coverage** that the stride/vertex-layout mismatch guard for
  `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` is correct — only that the
  independent, earlier "missing required texture" guard is correct. By contrast, the first
  sub-check (`badCombo`, BasicEffect, lines 651-661) is genuinely correct as written:
  `DrawBasicEffectEXT` has no upfront null-texture guard at all (its very first check IS the
  stride/flag `comboOk` switch, `D3D9EffectDraw.cpp` lines 628-645), so that sub-check really does
  exercise the layout-mismatch path it claims to.
- Why it matters: if a future change broke the stride check specifically for
  `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` (e.g. accidentally inverted the
  condition, or widened it to accept any stride), this test would keep passing (100% green)
  because the earlier, unrelated null-texture guard would still throw first, masking the
  regression entirely. The check's own inline comments ("No matching CNA vertex layout:
  DualTextureEffect requires...", etc.) assert a causal mechanism that is not what is actually
  being exercised.
- FNA/XNA comparison: N/A — this is a test-authoring issue in CNA-internal honest-gap reporting,
  not an XNA/FNA behavior question. The underlying production guards (both the null-texture check
  and the stride check) were independently confirmed present and correctly ordered by this audit;
  only the *test's own claim about which one it exercises* is wrong.
- Related files: none outside this test file — the production code itself is correct; only the
  test's setup (which `GpuDrawParams` fields are populated before the 3 affected sub-checks) needs
  adjustment.
- Suggested future action (not implemented by this audit): populate `texture0`/`texture1`/
  `envMap`/`texture0` (respectively) with valid, non-null textures in the DualTextureEffect/
  EnvironmentMapEffect/SkinnedEffect sub-checks, so the throw is forced to originate from the
  stride/layout guard specifically (as `badCombo`'s BasicEffect sub-check already correctly does),
  giving genuine coverage of the claimed code path.

### C++ correctness
`Make1x1Texture`/`Make1x1CubeTexture` (lines 166-184) correctly build a 1×1 `ImageData`/cube
texture with an explicit alpha default parameter (`= 255`). `ReadCenterPixel` (lines 186-193)
reads a genuine 1×1 region rather than an averaged block (unlike `d3d9_draw_test.cpp`'s 4×4
regions) — an appropriate choice here since this file's scenes are all flat-color single-triangle
fills with no rasterization-edge ambiguity at the exact center pixel.

### Memory/resource lifetime
Vertex buffers, textures, and the `D3D9RenderTargetBackend` (Check Q) are all held in local
`unique_ptr`s with per-check scope — no cross-check accumulation or leak risk observed.

### Thread safety, Performance
N/A / theoretical only (one-shot diagnostic, per-check buffer/texture reallocation).

### Architecture
Correctly exercises `DrawPrimitivesEx` (the real public-facing-adjacent effect dispatch entry
point) through `GpuDrawParams`, the same common-backend struct every other backend's
`DrawPrimitivesEx` test in this project's audit uses — good cross-backend consistency.

### Maintainability
Aside from F1/F2, this file is well-organized (one check per named letter, matching its own header
summary almost line-for-line) and unusually rich in derivation comments for each expected pixel
value — a real strength once the two documentation issues above are fixed.

### Robustness
Checks G/H's Less-compare pass/fail pair and Check F's 4 honest-gap throws are the right shape for
proving discard/error-reporting behavior; F2 above is specifically about 3 of those 4 not
targeting the guard they claim to, not about the underlying robustness being absent (the
null-texture guards ARE real and DO work, just not for the stated reason in those 3 cases).

### Testing
Comprehensive pixel-level coverage of all 5 Stock Effects' D3D9 dispatch, correctly distinguishing
lighting-bucket selection (Checks C/D, M/N, O/P each prove 2-vs-1-light bucket selection via a
different numeric result) — a real strength. F1/F2 are this file's only defects.

### Cross-file consistency
Cross-checked against `D3D9EffectDraw.cpp` end to end (read in full, 1163 lines) and against the
real FNA `EffectHelpers.SetFogVector` (Check E) — both traced to the actual current production
implementation, not assumed from the test's own comments. The `WorldInverseTranspose` normal-matrix
composition (`EnvironmentMapEffect`/`SkinnedEffect`, both correctly upload it per
`D3D9EffectDraw.cpp` lines 966-967/1067-1068) is a positive, cross-cutting-relevant finding: unlike
the confirmed EasyGL/WebGPU/Vulkan/Bgfx skinned-normal-transform bug (custom-shader backends that
never compose `WorldInverseTranspose` with the bone-skin matrix, per
`AUDIT_CROSS_CUTTING_FINDINGS.md`), D3D9 uses the real, vendored, byte-identical Microsoft
`SkinnedEffect.fx`/`EnvironmentMapEffect.fx` shaders (out of this audit's scope per D-5) and its
C++ consumer code correctly uploads the `WorldInverseTranspose` register those real shaders expect
— this audit found no equivalent defect in the D3D9 consumer code for either the fog-formula or the
skinned/env-map normal-transform cross-cutting concerns this batch was specifically asked to check.

## Detailed Findings

### F1 — Top-of-file header comment falsely claims SkinnedEffect is unimplemented, contradicted by this file's own Checks O/P

See full writeup under Logic above.
- Severity: MEDIUM · Confidence: HIGH · Category: documentation-rot / maintainability

### F2 — Check F's DualTextureEffect/EnvironmentMapEffect/SkinnedEffect sub-checks throw for an unclaimed reason, giving no real coverage of the stride/layout guard they name

See full writeup under Logic above.
- Severity: MEDIUM · Confidence: HIGH · Category: test-coverage / correctness-of-test

## Cross-File Observations

- `D3D9GraphicsBackend.hpp`'s own class-level doc comment (lines 29-46) contains the same
  documentation-rot pattern as F1 ("full effect-aware DrawPrimitivesEx dispatch and SpriteBatch
  still throw NotYetImplemented()... D9-82b/c, D9-90"), independently confirmed stale by direct
  reading of `D3D9EffectDraw.cpp` (full dispatch is real) and `D3D9SpriteBatch.cpp` (exists, is not
  a stub). That header file is not itself part of this batch's assigned 5 files, so it is not
  separately reported here, but is flagged for whichever shard/pass covers
  `include/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp`.
- This file's Check E independently confirms `ComputeFogVectorEXT` matches real FNA
  `EffectHelpers.SetFogVector` exactly — relevant to `AUDIT_CROSS_CUTTING_FINDINGS.md`'s
  "Systematic FNA parity gaps" fog-formula entry, which as of this audit's own prior state noted
  D3D9 as "not yet checked for the same formula." This batch's finding: **D3D9 does NOT share the
  Bgfx/Vulkan mirror-image fog bug.**
- This file's Checks M/N/O/P (EnvironmentMapEffect/SkinnedEffect both correctly upload
  `WorldInverseTranspose`) are relevant to the same document's skinned/env-map normal-transform
  entry, which likewise listed D3D9 as unchecked. This batch's finding: **D3D9's C++ consumer code
  does NOT share that defect either** (it correctly uploads the real register the vendored,
  out-of-scope Microsoft shader expects) — though this audit could not verify the vendored
  shader's own internal math (out of scope per D-5), only that CNA's own upload code sends the
  right constant to the right register name.

## Missing or Weak Tests

- See F2 above — the practical fix (populate valid textures in the 3 affected sub-checks) would
  close this file's only real test-coverage gap.
- No check exercises `DrawIndexedPrimitivesEx` specifically (only the non-indexed
  `DrawPrimitivesEx` overload is used throughout this file) — `DrawIndexedPrimitivesEx` shares the
  same `DrawPrimitivesExImpl` implementation per `D3D9EffectDraw.cpp` lines 608-621, so this is a
  low-risk gap (the indexed/non-indexed distinction is handled generically inside
  `DrawPrimitivesExImpl`'s own `if (ib)` branch, already exercised for the *colored* path by
  `d3d9_draw_test.cpp`'s own Check B), but a fully rigorous suite would still include at least one
  indexed effect-aware draw.

## Positive Findings

- Every pixel-value check (A, B, C, D, E, G, H, I, J, M, N, O, P, Q, R — 15 of this file's 16
  checks) was independently re-derived by this audit against the real FNA formulas and the current
  production C++, and all match exactly.
- Check E's fog formula was independently verified byte-for-byte against the real FNA
  `EffectHelpers.SetFogVector` source, not merely against this project's own prior commit history —
  a genuine, positive cross-cutting result for this batch (D3D9 does not share the Bgfx/Vulkan fog
  bug).
- Checks C/D, M/N, and O/P each use a deliberate "two different numeric results prove two different
  compiled shaders were genuinely selected" design (not just "the number is non-zero") — a strong,
  repeated test-engineering pattern across this file.
- Check Q is a genuine regression test for a real, previously-live, now-fixed type-confusion bug
  (`static_cast` vs. `dynamic_cast` across two unrelated `ITextureBackend` sibling classes),
  independently confirmed against the current `ResolveD3D9TextureEXT` implementation.
- Check R's own comment is honest about its real discriminating power (a flat-normal triangle
  cannot distinguish vertex-lit from pixel-lit math), rather than overclaiming — a good example of
  the same "engineering honesty" this audit's template report (the EasyGL specular test) praised.

## Final Assessment

Strong effect-dispatch coverage let down by two documentation/test-design issues, both MEDIUM
severity: a stale top-of-file claim that flatly contradicts the file's own later-added Checks O/P
(F1), and 3 of 4 "honest-gap" sub-checks in Check F that pass for a different reason than their own
comments claim, leaving the stride/vertex-layout guard for `DualTextureEffect`/
`EnvironmentMapEffect`/`SkinnedEffect` genuinely untested by this file despite appearing to be
(F2). The underlying production dispatch code itself (`D3D9EffectDraw.cpp`) was independently
verified correct in every respect this audit checked, including the two cross-cutting concerns
this batch was specifically asked to investigate (fog formula, skinned/env-map normal transform) —
neither defect is present in D3D9's consumer code.
