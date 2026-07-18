# Audit: examples/bgfx_blendstate_additive_test.cpp

## Metadata

- Source file: `examples/bgfx_blendstate_additive_test.cpp` (115 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BlendState.Additive` preset pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_blendstate_additive …)` /
  `cna_register_backend_test(NAME Bgfx_BlendState_Additive …)`,
  `cmake/Tests/BgfxTests.cmake:747-749`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.BlendState.Additive`.
- FNA reference: `src/Graphics/States/BlendState.cs:164-170` (`Additive = new BlendState(
  "BlendState.Additive", Blend.SourceAlpha, Blend.SourceAlpha, Blend.One, Blend.One)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp:6` (identical preset
  values), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyBlendState`, lines
  1572-1596; `XnaBlendToBgfxFactor`, lines 1538-1556).

## Purpose

Clears to a known background `(200,50,0,255)`, draws a fully-opaque quad `(255,100,0,255)` with
`BlendState::Additive`, and checks two independent numeric properties of the *additive* equation in
one pass: (a) the R channel (`255+200=455`) must **saturate/clamp to 255**, not wrap; (b) the G
channel (`100+50=150`) must be an **exact, unsaturated sum**, proving the destination is genuinely
added in full (not silently dropped, which a buggy "treat any alpha=255 draw as Opaque" fast path
could do).

## Executive Verdict

**Healthy** — `BlendState::Additive`'s 4 real factors were confirmed identical to FNA's own preset,
the expected-value arithmetic (lines 16-20) was independently re-derived and matches exactly, and the
Bgfx `ApplyBlendState` mapping this exercises was independently traced end-to-end (enum ordinal →
`XnaBlendToBgfxFactor` → `BGFX_STATE_BLEND_FUNC_SEPARATE`) and found correct.

## Checklist Results

### API / XNA / FNA parity
`BlendState::Additive` (`BlendState.cpp:6`): `ColorSourceBlend=AlphaSourceBlend=Blend::SourceAlpha`,
`ColorDestinationBlend=AlphaDestinationBlend=Blend::One` — a byte-for-byte match against
`BlendState.cs:164-170`'s constructor argument order (`name, colorSrc, alphaSrc, colorDst, alphaDst`),
independently confirmed by reading both files side-by-side. `Blend` enum ordinals
(`Blend.hpp`: `One=0, Zero=1, …, SourceAlpha=4, …`) match `XnaBlendToBgfxFactor`'s switch
(`BgfxGraphicsBackend.cpp:1538-1556`, `case 4: return BGFX_STATE_BLEND_SRC_ALPHA`) exactly.

### Behavioral correctness
Re-derived by hand: source alpha = 255 → blend-factor scale 1.0 for both `SourceAlpha` (color) and
`One` (destination) terms. `R = 255*1 + 200*1 = 455`, clamps to 255 in an 8-bit target — matches
`rSaturated` (`got.R >= 250`, line 84). `G = 100*1 + 50*1 = 150` — matches `gAddsFully`
(`got.G` in `[140,160]`, line 85). `B = 0` (not independently asserted, consistent with the other
files in this shard which only check the 1-2 channels relevant to the specific property under test).
Confirmed `ApplyBlendState`'s "Opaque fast path" (`BgfxGraphicsBackend.cpp:1576-1581`) checks all 4
factors (`colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1`), so
`Additive`'s real factors (`4,4,0,0` in ordinal terms) cannot be misclassified into the zero-blend
fast path — ruling out the specific failure mode `gAddsFully` is designed to catch (this is exactly
what the file's own `[FAIL]` message describes: *"G~100 would mean the destination was incorrectly
dropped"*).

### Logic
Single `Clear()`+`Draw()`+`GetBackBufferData()` per run (no retry loop, `done_` guard prevents
re-entry) — the header comment's claim that the Bgfx "first read per rendered frame" quirk (Task 406)
doesn't apply here is accurate: that quirk is specifically about a *second* read within the same
frame with no new submitted work in between, which cannot occur when there is only ever one read
total. Empirically corroborated by this shard's own commit history: `cf2d5eb3`
("test(Task 752-755): verify BlendState presets on Bgfx") states *"All 4 pass with exact expected
values"* for this exact single-read pattern across all 4 preset tests.

### C++ correctness
No dangling references; `verts` is a local array fully consumed synchronously by
`DrawUserPrimitives` before going out of scope. `DepthStencilState ds;` (default-constructed, no
static preset name) is valid — `ApplyDepthStencilState` only reads the boolean/enum fields, not any
name string.

### Robustness
Correctly avoids the legacy `GraphicsDevice::SetDepthTestEnabled(false)` convenience call — confirmed
this genuinely throws on Bgfx (`BgfxGraphicsBackend.cpp:2002`, `void
BgfxGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3DState(); }`), so the file's own claim that
this is "a pre-existing, deliberate stub" rather than a bug this task introduced is accurate, and the
`DepthStencilState`-based substitute (lines 56-58) is the correct, already-wired equivalent
(`ApplyDepthStencilState` is a real, non-stub method on this backend).

### Testing
Two independent numeric properties from one draw (saturation *and* non-dropped-destination) is an
efficient, genuinely discriminating design for this specific preset — a plausible regression
("silently clamp any add to Opaque-like behavior" or "compute additive without saturation, e.g. via a
wraparound integer path") would each be caught by a different one of the two checks.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Shares the `RasterizerState::CullNone` workaround (line 77, "Task 896 finding") with every other
  `BlendState` file in this batch; independently re-verified the quad's own winding is genuinely CCW
  (`tl=(-1,1), bl=(-1,-1), br=(1,-1)`, cross-product `=4>0`), so the fix is correctly needed here, not
  just copy-pasted boilerplate. Unlike `bgfx_basiceffect_vertexcolor_enabled_test.cpp` (this batch),
  this file's comment attributes the cause to "CNA's real default RasterizerState" generically rather
  than claiming it is Bgfx-unique — accurate given this file was authored 2026-07-10, after Task 896
  made the default uniform across all 3 backends (confirmed via `git log`).
- `BlendState::Additive`'s 4 factors are shared, backend-agnostic C++ constants
  (`BlendState.cpp:6`) — this test exercises the same values as its EasyGL/Vulkan siblings
  (per the file's own header note, "Bgfx-specific copy of examples/easygl_blendstate_additive_test.cpp
  (Task 306, already reused verbatim on Vulkan)"), confirmed structurally identical apart from the
  `DepthStencilState` substitution this shard's header comments consistently document.

## Missing or Weak Tests

None identified for this file's stated scope (saturation + non-dropped-destination for the
`Additive` preset specifically).

## Positive Findings

- The two checks target genuinely different, independently-failable implementation bugs (integer
  overflow/wraparound vs. destination-blend-factor mishandling) rather than being redundant with each
  other.
- Correctly distinguishes the `Additive`-specific claim ("destination is *always* added regardless of
  source alpha") from `AlphaBlend`/`NonPremultiplied`'s different destination-factor behavior, and the
  header comment explicitly calls this contrast out (lines 12-14) — an accurate, verified statement
  given the FNA preset comparison performed by this audit.

## Final Assessment

A small, precise, and correctly-derived test. No defects found in the test file or in the underlying
`BlendState::Additive`/Bgfx blend-factor mapping it exercises.
