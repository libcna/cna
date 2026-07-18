# Audit: examples/d3d9_skinnedvertexcolor_test.cpp

## Metadata

- Source file: `examples/d3d9_skinnedvertexcolor_test.cpp` (250 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note)
- Subsystem: `examples-tests-d3d9` shard — CNA's own NOXNA "SkinnedVertexColor3D" custom shader
  (`Game`-subclass, CTest-registered, real device/window path).
- XNA/FNA relevance: indirect. `SkinnedVertexColor3D` is explicitly **not** a Microsoft stock
  effect — real XNA 4.0's `SkinnedEffect` has no vertex-color input at all (`Structures.fxh`'s
  `VSInputNmTxWeights` declares Position/Normal/TexCoord/Indices/Weights only). This file exercises
  `Microsoft::Xna::Framework::Graphics::SkinnedEffect`'s data model (bones, lighting fields) only
  insofar as `SetBaseParams()` fills the same `GpuDrawParams` struct `SkinnedEffect::FillGpuDrawParams()`
  would.
- Related production code read in full: `src/CNA/Internal/Backends/D3D9/D3D9SkinnedVertexColorDraw.cpp`,
  `src/CNA/Internal/Backends/D3D9/shaders/cna/SkinnedVertexColor3D.hlsl`,
  `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (`FillGpuDrawParams()`), and — for
  cross-comparison of the correct, vendored-stock-effect fog path — `src/CNA/Internal/Backends/D3D9/D3D9EffectDraw.cpp`
  (`ComputeFogVectorEXT()`) and `src/CNA/Internal/Backends/D3D9/D3D9PbrDraw.cpp`/`shaders/cna/Pbr3D.hlsl`.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only. This report is entirely
static/source-reading; no build, compile, or execution was attempted in this Linux sandbox. The
file's own header comment self-discloses the same limitation ("written and cross-compile-checked
(mingw) but has NOT been run"). All numeric/behavioral conclusions below (including the fog finding)
were derived by hand-tracing the actual HLSL/C++ source and cross-checking against the FNA reference
tree, not by execution.

## Purpose

A 4-check `Game`-subclass CTest for `D3D9GraphicsBackend::DrawSkinnedVertexColorEXT()` /
`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch of the custom stride-56 "SkinnedVertexColor3D"
shader (SkinnedEffect + a trailing vertex `Color` real XNA never has). Every light's diffuse/specular
is deliberately zeroed so the final pixel is provably `EmissiveColor * DiffuseColor * texColor *
vertexColor`, isolating the vertex-color multiply itself.

## Executive Verdict

**Needs attention** — the four checks that exist are correctly targeted and (as far as static tracing
can confirm) pass against current production code. However, cross-file inspection of the production
shader/dispatch code this file exercises (`D3D9SkinnedVertexColorDraw.cpp` /
`SkinnedVertexColor3D.hlsl`) found a real, concrete fog-computation bug — the fog factor is derived
from the raw, untransformed local vertex Z, completely ignoring `World`/`View`, unlike the correct
`ComputeFogVectorEXT()` path used by every vendored stock effect in this same backend — and this test
file cannot detect it because it never enables fog (`GpuDrawParams::fogEnabled` defaults to `false`
and `SetBaseParams()` never sets it) and always uses `Matrix::getIdentityProperty()` for World/View
(masking the bug even if it were engaged). See F1.

## Checklist Results

### API / XNA / FNA parity
N/A directly (`SkinnedVertexColor3D` is an explicit `NOXNA` CNA extension, not part of the XNA 4.0
API surface). Indirectly relevant: the `GpuDrawParams` fields this file populates
(`diffuseColor`/`emissiveColor`/`light0..2Diffuse`/`light0..2Specular`/`specularColor`/`specularPower`/
`weightsPerVertex`/`boneCount`/`boneTransforms`) are the same fields
`SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp` lines 320-399) populates for the real stock
effect, and the comment at `SetBaseParams()` (line 112) correctly notes `emissiveColor` is
"pre-folded ambient" — matching `SkinnedEffect.cpp` line 336-338's own
`emissiveColor_ + ambientLightColor_ * diffuseColor_` fold exactly (so the earlier cross-cutting
concern about a Vulkan-specific "ambientColor never set" gap does not apply to this D3D9 shader —
the field is legitimately pre-folded here, both by the production `SkinnedEffect.cpp` and by this
test's own `SetBaseParams()` helper).

### Behavioral correctness
- **Check A** (`VertexColorEnabled=true`, red vertex color → exact red readback): traced against
  `PSSkinnedVertexColor3D` (`SkinnedVertexColor3D.hlsl` lines 141-187). With all light diffuse/specular
  zeroed, `litRGB = EmissiveColor(1,1,1) * DiffuseColor.rgb(1,1,1) = (1,1,1)`; `specularRGB = 0`;
  `texColor = (1,1,1,1)` (opaque white texture); `outColor.rgb = litRGB*texColor.rgb = (1,1,1)`,
  `outColor.rgb += specularRGB*outColor.a` (no-op, 0); then `outColor.rgb *= vc.rgb` where
  `vc = pin.Color = (1,0,0,1)` since `VertexColorEnabledPad.x>0.5` → final `(1,0,0)` = exact red.
  Matches the check's own expectation and the header comment's derivation.
- **Check B** (`VertexColorEnabled=false`, same red vertex data → exact white): `vc =
  float4(1,1,1,1)` when the flag is off (line 169: `(VertexColorEnabledPad.x > 0.5) ? pin.Color :
  float4(1,1,1,1)`), so `outColor.rgb *= (1,1,1)` is a no-op → white. Correctly proves the gate reads
  the real flag, not a hardcoded `true`.
- **Check C** (identity bone, indexed draw): `Bones[0]` uploaded as identity
  (`D3D9SkinnedVertexColorDraw.cpp` `SetBaseParams` in the test itself; production upload via
  `UploadBonesVS()`, `D3D9SkinnedVertexColorDraw.cpp` lines 87-103) — `skinning = Bones[0]*1.0`,
  identity, so `skinnedPos=vin.Position`, `skinnedNormal=vin.Normal`, no distortion. Re-derives to the
  same red as Check A. Sound.
- **Check D** (missing `texture0` throws): confirmed against `DrawSkinnedVertexColorEXT()`
  (`D3D9SkinnedVertexColorDraw.cpp` lines 113-116): `if (!params.texture0) throw
  std::runtime_error(...)` — a real, named, early guard before any GPU state is touched. Matches.

### Logic / Cross-file consistency — F1 (see Detailed Findings)
The vertex shader's `FogFactor` computation (`SkinnedVertexColor3D.hlsl` line 102-106) uses
`vin.Position.z` — the **raw, pre-skin, pre-World, pre-View local vertex Z** — as if it were already
view-space depth. Every other lit/fogged effect in this same backend (`D3D9EffectDraw.cpp`'s
`ComputeFogVectorEXT()`, used for the real, vendored `BasicEffect`/`AlphaTestEffect`/
`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` stock shaders) correctly folds `World*View`
into a `FogVector` uploaded from the CPU side, exactly matching FNA's own
`EffectHelpers.SetFogVector`/`Common.fxh ComputeFogFactor` (`dot(position, FogVector)`, where
`FogVector` bakes in the world-view transform's Z row). `Pbr3D.hlsl`/`PbrSkinned3D.hlsl` share the
identical `vin.Position.z`-based formula (`D3D9PbrDraw.cpp` lines 232-246), so this is a **repeated
pattern across all 3 of this project's own custom D3D9 NOXNA shaders**, not a one-off in this file.

### C++ correctness
`Vec4Pad Pad3(const float v3[3])` (`D3D9SkinnedVertexColorDraw.cpp` line 76) reads exactly 3 floats
and zero-pads the 4th; every call site passes a `params.lightNDiffuse`/`specularColor`/etc. array
that is genuinely 3 floats (`GpuDrawParams`), so no over-read. `UploadBonesVS()`'s `boneCount =
params.boneCount < 72 ? params.boneCount : 72` correctly clamps against the shader's `Bones[72]`
array before indexing `params.boneTransforms + i*16` — no OOB read for the test's own
`boneCount=1`.

### Memory/resource lifetime
Lazy vertex/pixel shader creation (`skinnedVertexColorVS_`/`skinnedVertexColorPS_`, lines 118-133) is
a `ComPtr`-cached member on `D3D9GraphicsBackend`, created once and reused — no leak, no
double-create (guarded by `if (!skinnedVertexColorVS_)`).

### Performance
N/A / theoretical only — a one-shot smoke-style CTest, not a hot path under audit here.

### Architecture
Correctly scoped: a `NOXNA` custom shader kept entirely out of the vendored `shaders/xna/` stock-effect
tree (matches D-5's exemption boundary — this file and its production counterpart are the CNA-side
consumer code the audit scope explicitly calls "fully in scope" even though the vendored stock
effects are not).

### Robustness
Check D's missing-texture guard is real and correctly placed before any device-state mutation. No
check exercises a missing/degenerate bone weight (`weightsPerVertex` beyond 1) or a boneIndex out of
`[0,72)` — an out-of-range `BoneIndices` component would index `Bones[]` out of bounds in the shader
itself (undefined per HLSL, not a CNA-catchable condition), consistent with this being genuine
vertex-data responsibility, not a gap in this specific file.

### Testing
This file only exercises the `vertexColorEnabled`/bone-identity/missing-texture axes. It does **not**
exercise: fog (never enabled — see F1), a non-identity `World`/`View` (every check uses
`Matrix::getIdentityProperty()` for all three matrices), multi-bone weighting
(`weightsPerVertex >= 2`), or any directional light actually contributing (all three lights are
deliberately zeroed by design, appropriately, to isolate the vertex-color term — but this also means
no check in this file could ever have caught F1 even if fog had been engaged, since World=View=Identity
happens to make the buggy formula numerically coincide with the correct one in that special case only).

## Detailed Findings

### F1 — `SkinnedVertexColor3D.hlsl`'s fog factor uses raw local-space vertex Z, ignoring World/View — a real deviation from this same backend's own correct fog convention, untested by this file

- Severity: HIGH
- Confidence: HIGH (independently re-derived both formulas by hand against the FNA reference source
  and the current production code, not merely pattern-matched)
- Category: correctness / FNA-parity (production bug, not a test-authoring issue)
- Location/symbol: `SkinnedVertexColor3D.hlsl` `VSSkinnedVertexColor3D()` lines 102-106
  (`saturate((vin.Position.z + FogParams.z) / (FogParams.z - FogParams.y))`);
  `D3D9SkinnedVertexColorDraw.cpp` lines 144-147 (`FogParams` upload: raw
  `fogEnabled`/`fogStart`/`fogEnd`/`weightsPerVertex` scalars, no `World`/`View`-derived vector at all).
- Evidence: FNA's real fog (`EffectHelpers.SetFogVector`, `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs`
  lines 117-140) computes `FogVector` from `World*View`'s own third row
  (`worldView.M13/M23/M33 * scale`, `(worldView.M43+FogStart)*scale`), then the shader does
  `saturate(dot(rawLocalPosition, FogVector))` (`Common.fxh` line 9-12) — i.e. the *shader* consumes
  the untransformed local position, but the *CPU-uploaded vector itself* is where World/View enters
  the computation. This project's own `D3D9EffectDraw.cpp::ComputeFogVectorEXT()` (lines 149-168)
  correctly reproduces this exactly, matrix-multiplying `world*view` and deriving the same
  `M13/M23/M33/M43`-based vector, used by every vendored stock-effect draw in this backend
  (`D3D9EffectDraw.cpp` line 678-679). `SkinnedVertexColor3D.hlsl`/`D3D9SkinnedVertexColorDraw.cpp`
  instead upload the raw scalar `FogStart`/`FogEnd` and let the *shader* do
  `(vin.Position.z + FogEnd)/(FogEnd - FogStart)` directly against the untransformed local Z — this
  is only numerically equivalent to FNA's real formula in the special case `World == View ==
  Identity` (independently verified algebraically: with identity transforms, FNA's
  `scale=1/(FogStart-FogEnd)`, `FogVector.z=scale`, `FogVector.w=FogStart*scale`, giving
  `dot=z*scale+FogStart*scale=(z+FogStart)/(FogStart-FogEnd)`, which is algebraically identical to
  this shader's `(z+FogEnd)/(FogEnd-FogStart)` only because both reduce to the same ratio when the
  transform is the identity — the two diverge the moment `World`/`View` are not the identity, since
  FNA's `FogVector` bakes in the full 3x3 rotation/translation of `World*View` while this shader's `z`
  never does). The same pattern (raw `vin.Position.z`, no matrix-derived fog vector) is also present
  in `Pbr3D.hlsl`/`PbrSkinned3D.hlsl` (`D3D9PbrDraw.cpp` lines 232-246), confirming this is a
  systemic gap across this project's 3 custom D3D9 NOXNA shaders, not isolated to this one file. This
  independently corroborates (rather than duplicates) this audit's separately-tracked EasyGL
  "object-space-only fog" finding — the identical architectural mistake (using local mesh-space Z as
  a stand-in for view-space depth, with no World/View composition at all) has been carried into
  D3D9's own bespoke shaders.
- Why it matters: any real scene combining fog (`FogEnabled=true`) with a translated, rotated, or
  animated (skinned) mesh — an extremely common combination for a 3D game using vertex-colored
  skinned meshes — will compute the wrong fog blend, because the "Z" the formula treats as
  view-space depth is actually still in the object's own local/bind-pose coordinate space, before
  either skinning, World, or View is applied. A character standing far from the camera, or simply
  translated away from the world origin, would show the SAME fog density it would show sitting at the
  origin facing the camera head-on — fog would only ever respond to the mesh's own internal vertex
  coordinates, never to its actual position or the camera's.
- FNA/XNA comparison: FNA's `SkinnedEffect` (the real stock effect, dispatched by this same backend's
  own `DrawSkinnedEffectEXT`/`D3D9EffectDraw.cpp`, not this custom shader) computes fog correctly via
  `ComputeFogVectorEXT()`. This custom `SkinnedVertexColor3D` shader is the one shader in this file's
  own family that gets it wrong.
- Related files: `src/CNA/Internal/Backends/D3D9/shaders/cna/Pbr3D.hlsl`,
  `src/CNA/Internal/Backends/D3D9/shaders/cna/PbrSkinned3D.hlsl`,
  `src/CNA/Internal/Backends/D3D9/D3D9PbrDraw.cpp` (identical pattern, not fixed by this audit).
- Suggested future action (not implemented by this audit): compute a proper `World*View`-derived fog
  vector on the CPU side (mirroring `ComputeFogVectorEXT()`) and upload it in place of the raw
  `FogStart`/`FogEnd` scalars, dotting it against the shader's own local `vin.Position`/`skinnedPos`
  exactly as `Common.fxh`'s `ComputeFogFactor()` does; add a test to this file (or a sibling) that
  sets `fogEnabled=true` with a non-identity `World`/`View` and a genuinely distinguishing camera
  distance, since no existing check in this file (or, per the grep above, in `Pbr3D`'s own sibling
  tests) can currently detect this class of bug.

## Missing or Weak Tests

- See F1 — fog is never engaged by this file at all (`GpuDrawParams::fogEnabled` defaults `false`,
  `SetBaseParams()` never sets it), so even a much more broken fog implementation than the one found
  would go undetected here.
- Multi-bone blending (`weightsPerVertex >= 2`, `>= 4`) is untested by this file — only a single
  100%-weighted bone is exercised (Check C). The shader's own conditional accumulation
  (`if (weightsPerVertex >= 2.0) ... if (weightsPerVertex >= 4.0) ...`,
  `SkinnedVertexColor3D.hlsl` lines 87-91) has no corresponding multi-bone check in this file, unlike
  `SkinnedEffect`'s own Task 895 fix this shader's header comment references.
- The documented `(float3x3)World`-instead-of-`WorldInverseTranspose` normal-transform simplification
  (`SkinnedVertexColor3D.hlsl` header comment lines 34-50) is self-disclosed as a known limitation
  under non-uniform scale, consistent with `PbrSkinned3D`'s identical choice — not re-litigated here
  since it is honestly documented in-source rather than silently wrong, but no test in this file (or,
  as far as this audit traced, any sibling D3D9 test) exercises a non-uniform-scale `World` to confirm
  the documented limitation's actual visible effect is as small as claimed.

## Positive Findings

- Checks A-D are precise, correctly isolate the one property this custom shader adds over the real
  `SkinnedEffect` (the vertex-color multiply applied after the specular add, matching
  `EnsureSkinnedProgram()`'s own established EasyGL fix), and were independently re-derived by this
  audit to match current production code exactly.
- `SkinnedEffect::FillGpuDrawParams()`'s ambient pre-fold into `emissiveColor` is correctly consumed
  and explicitly commented on by the production dispatch code — no repeat of the Vulkan-specific
  "ambientColor never set" gap this audit's cross-cutting findings flag elsewhere.
- The missing-texture guard (Check D) is a real, early, named-exception guard, not a deferred crash.

## Final Assessment

The four checks that exist in this file are sound and correctly verify the vertex-color-modulation
behavior this shader was purpose-built to add. However, auditing the production shader/dispatch code
this file exercises (as instructed, to check for the class of shader-consumer bug already confirmed
elsewhere in this project) surfaced a genuine, unrelated defect — an object-space-only fog
computation that silently ignores `World`/`View` — that this file's own scope cannot detect because it
never enables fog. This is a real production bug in currently-shipped D3D9 NOXNA shader code, not a
hypothetical one, and it recurs identically in two sibling custom shaders in the same backend.
