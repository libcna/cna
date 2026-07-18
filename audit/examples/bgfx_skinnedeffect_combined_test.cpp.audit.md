# Audit: examples/bgfx_skinnedeffect_combined_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_combined_test.cpp` (178 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` cross-backend capstone (identity /
  single-bone / two-bone-blend)
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_SkinnedEffect_Combined`
  (`cmake/Tests/BgfxTests.cmake:255-258`)
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms()`, `WeightsPerVertex`,
  `EnableDefaultLighting()`.
- FNA reference: `src/Graphics/Effect/StockEffects/SkinnedEffect.cs` (constructor defaults,
  `WeightsPerVertex` semantics, `IEffectLights.LightingEnabled`'s hard `NotSupportedException` on
  `false`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (constructor
  lines 36-50, `FillGpuDrawParams()` lines ~320-380); Bgfx shader
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_skinned3d_vertexlit.sc` (skinning-matrix blend, lines
  33-48).

## Purpose

Draws 3 quads side-by-side in NDC space, each skinned by a different bone configuration against a
1×1 red texture, with `WeightsPerVertex=2` and `EnableDefaultLighting()` active:
- Quad A: weight `(1,0,0,0)`, index `0` (identity bone) → should render unmoved, at NDC x ∈ [-1, -0.5].
- Quad B: weight `(1,0,0,0)`, index `1` (bone 1 = `CreateTranslation(0.75,0,0)`) → shifted to
  x ∈ [-0.25, 0.25].
- Quad C: weight `(0.5,0.5,0,0)`, indices `(2,3)` (bones 2/3 = translations `1.0`/`2.0`) → a genuine
  two-bone blend, expected to land at x ∈ [0.5, 1.0] (average of the two bone translations applied to
  the authored -1..-0.5 span).

Three fixed sample points (`W/8`, `W/2`, `7W/8`, at the vertical centre) are each read via their own
independent `renderAndRead()` pass (Task 406 first-read-only Bgfx quirk), and the test asserts each
sampled pixel is "red-dominant" (`R > G && R > 50`) rather than an exact colour match — a deliberately
loose check, appropriate given `EnableDefaultLighting()` introduces real ambient+diffuse shading on top
of the base red texture.

## Executive Verdict

**Healthy** — the skinning-weight semantics this test exercises (`WeightsPerVertex=2` summing only the
first two weight/index pairs) were independently verified against both FNA's real `Skin()` contract and
the actual Bgfx vertex shader, and match exactly. No `GraphicsDeviceManager` is used (F1, informational
only — an established, cross-backend convention for this exact test family, not a Bgfx-specific gap).

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms(bones)` / `fx.setWeightsPerVertexProperty(2)` / `fx.EnableDefaultLighting()`
(lines 125-127) match FNA's `SkinnedEffect` surface. Confirmed against FNA's actual source
(`StockEffects/SkinnedEffect.cs:380-396`) that `SkinnedEffect`'s constructor unconditionally sets
`DirectionalLight0.Enabled = true` and that `IEffectLights.LightingEnabled`'s setter throws
`NotSupportedException` on `false` (line 365-368 of that file) — i.e. SkinnedEffect's lighting cannot be
disabled at all, unlike `BasicEffect`. This test correctly relies on `EnableDefaultLighting()` to give
DirectionalLight0 a real, non-degenerate `Direction`/`DiffuseColor` (verified in CNA's own
`SkinnedEffect.cpp:224-238`, which sets `Direction=(-0.5265,-0.5736,-0.6275)` and matching diffuse/
specular colours, matching FNA's own `EffectHelpers.EnableDefaultLighting` constants) — see the
companion finding in `bgfx_skinnedeffect_fog_test.cpp`'s report for what happens when this call is
*not* made.

### Behavioral correctness
Independently re-verified the `WeightsPerVertex` semantics this test's quad C depends on:
`vs_skinned3d_vertexlit.sc:37-41` —
```
float weightsPerVertex = u_weightsPerVertex.x;
mat4 skinMat = u_bones[int(a_indices.x)] * a_weight.x;
if (weightsPerVertex >= 2.0) skinMat += u_bones[int(a_indices.y)] * a_weight.y;
if (weightsPerVertex >= 4.0) skinMat += ...
```
With `WeightsPerVertex=2`, quad C's `(w0=0.5, i0=2, w1=0.5, i1=3)` produces
`skinMat = 0.5*bones[2] + 0.5*bones[3]` — a genuine two-bone blend, matching FNA's documented `Skin()`
contract ("sums the first `WeightsPerVertex` (1, 2, or 4) weight/index pairs," per this same shader
file's own comment, cross-checked against `SkinnedEffect.cpp`'s own identical comment at line 340-343 in
the C++ `FillGpuDrawParams()`). Bones 2 and 3 are `CreateTranslation(1.0,0,0)` and
`CreateTranslation(2.0,0,0)` respectively (lines 122-123) — averaging their translations gives `+1.5` on
X, moving the authored `[-1.0,-0.5]` span to `[0.5, 1.0]`, landing squarely on the `cReg` sample point
at `7W/8` (NDC x ≈ +0.75, line 140). The arithmetic is internally consistent and matches the shader's
real blend formula, not just the test author's intent.

### Logic
`SkinnedGpuVertex`'s manually-authored 52-byte layout (`px,py,pz,nx,ny,nz,u,v,w0..w3,i0..i3`, lines
40-48) is asserted via `static_assert(sizeof(SkinnedGpuVertex) == 52, ...)` (line 48) — a good defensive
check against silent padding/alignment changes, and the comment correctly cites this as "Task 123's own
convention," matching the layout also used by the sibling fog test in this same shard.

### C++ correctness
`VertexBuffer vb(device, verts.size()); vb.SetDataRaw(verts.data(), ..., sizeof(SkinnedGpuVertex))`
(lines 134-135) is a raw-bytes upload path — correctly sized and correctly paired with the
`static_assert`-checked struct size, so there's no risk of a silent stride mismatch between the CPU-side
struct and the GPU-side vertex declaration this relies on.

### Testing
The "red-dominant" loose check (`R > G && R > 50`, lines 146-148) is an appropriate choice given real
directional lighting is active — an exact-colour check would be fragile against legitimate ambient/
diffuse lighting-formula changes that don't affect the underlying skinning-correctness question this
test targets. This is a deliberate trade of precision for lighting-formula independence, not laxness.

## Detailed Findings

No MEDIUM/HIGH/CRITICAL findings. One LOW/INFO observation, shared with the sibling mip-chain file in
this same shard:

### F1 — No explicit `GraphicsDeviceManager`; relies on `Game`'s default-constructed 800×480 device
- Severity: LOW
- Confidence: HIGH
- Category: maintainability / consistency
- Location/symbol: `class BgfxSkinnedEffectCombinedTest : public Game` (lines 65-171) — no `gdm_`
  member, no constructor
- Evidence: same mechanism independently traced in this batch's `bgfx_rendertargetcube_mip_test.cpp`
  report (`Game::getGraphicsDeviceProperty()` falling back to a default-constructed `GraphicsDevice_`,
  `GraphicsDeviceManager::DefaultBackBufferWidth/Height` = 800×480). Confirmed the EasyGL sibling port
  (`easygl_skinnedeffect_combined_test.cpp`) uses the identical convention, so this is an established,
  intentional, cross-backend pattern for this specific test family, not a Bgfx-specific gap.
- Why it matters: purely cosmetic — the test reads `device.getViewportProperty()` for its 3 sample
  points (lines 109-111, 138-140), so it is fully agnostic to the actual window size used.
- Suggested follow-up: none required.

## Cross-File Observations

- Directly complements `bgfx_skinnedeffect_fog_test.cpp` (audited separately in this batch): this file
  calls `EnableDefaultLighting()` (giving `DirectionalLight0` a real, non-zero `Direction`), while the
  fog test deliberately does *not*, relying instead on `DirectionalLight0.Enabled=true`'s default
  zero-`Direction`/zero-`DiffuseColor` combination to make lighting a no-op — see that file's report for
  a latent NaN-propagation risk (`normalize(vec3(0,0,0))`) this file's own use of
  `EnableDefaultLighting()` correctly avoids.
- Shares the exact `SkinnedGpuVertex` 52-byte layout and `WeightsPerVertex` shader logic with
  `bgfx_skinnedeffect_fog_test.cpp` — verified the shared shader code once here (via quad C's two-bone
  blend) and treated the fog test's identical layout claim as already corroborated.

## Missing or Weak Tests

None found beyond the already-discussed, well-justified loose colour-match tolerance.

## Positive Findings

- Genuinely exercises all three of FNA's documented `WeightsPerVertex` regimes' *boundary* (identity /
  single-bone / real multi-bone blend) in one file, rather than only testing the trivial identity case.
- The `static_assert` on the manually-packed GPU vertex struct is good defensive practice against a
  class of bug (silent stride mismatch) that is otherwise easy to introduce accidentally when hand-
  authoring GPU-facing structs.

## Final Assessment

No defects found. The skinning-blend math this test's most interesting case (quad C) depends on was
independently re-derived against both the real Bgfx shader source and FNA's documented `Skin()`
contract, and matches.
