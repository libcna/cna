# Audit: examples/bgfx_texture_address_mode_mirror_test.cpp

## Metadata

- Source file: `examples/bgfx_texture_address_mode_mirror_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — Task 746, `TextureAddressMode::Mirror` edge sampling on Bgfx
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_texture_address_mode_mirror …)` /
  `cna_register_backend_test(NAME Bgfx_TextureAddressMode_Mirror …)`, `cmake/Tests/BgfxTests.cmake:78-81`).
- XNA/FNA relevance: direct — `SamplerState.AddressU`/`AddressV`, `TextureAddressMode::Mirror`,
  `SpriteBatch::Begin`'s `samplerState` parameter.
- FNA reference: `Graphics/States/TextureAddressMode.cs` (`Wrap=0, Clamp=1, Mirror=2`),
  `Graphics/States/SamplerState.cs` (no `PointMirror` static preset — only `*Clamp`/`*Wrap` variants
  exist for `Point`/`Linear`/`Anisotropic`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplySamplerState`, lines 1890-1939 — the `addressU`/`addressV` → `BGFX_SAMPLER_*_MIRROR` mapping
  at lines 1933-1937).

## Purpose

Draws a 2×1 (Red|Blue) texture via `SpriteBatch` with a `sourceRectangle` twice the texture's width
(U spans `[0,2]`), samples at `U=1.6` under three different `AddressU`/`AddressV` configurations
(`Point`+`Mirror` custom-built, `SamplerState::PointWrap`, `SamplerState::PointClamp`), and asserts
all three disagree/agree in the specific pattern that only a genuinely-implemented Mirror mode
produces: `Wrap` and `Clamp` both read the *right* (Blue) texel at `U=1.6`, while `Mirror` reflects
(`2.0-1.6=0.4`) back into the *left* (Red) texel — the one sample point in `[0,2]` where all three
modes are mutually distinguishable (the file's own comment notes `U=1.25`, used by the sibling
`bgfx_texture_address_mode_test.cpp`, is unsuitable here because Mirror and Clamp coincidentally agree
there).

## Executive Verdict

**Healthy** — the `U=1.6` reflection arithmetic was independently re-derived and confirmed correct
for FNA's `Mirror` semantics, and the three-way comparison (not just "does Mirror read Red") is a
genuinely more discriminating design than a single-mode assertion, since it also proves the sampler
state actually reached the GPU (a real, previously-fixed bug class per Task 750, cited in this file's
own header) rather than every mode silently defaulting to the same GL sampler.

## Checklist Results

### API / XNA / FNA parity
`TextureAddressMode::Wrap=0, Clamp=1, Mirror=2` (confirmed in
`include/Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp`) matches FNA's enum exactly.
`SamplerState::PointWrap`/`PointClamp` are real FNA static presets
(`src/Microsoft/Xna/Framework/Graphics/SamplerState.cpp` lines 10-11); the file's own comment
correctly notes FNA/XNA has no `PointMirror` preset, so hand-building a `SamplerState` with
`Filter=Point, AddressU=AddressV=Mirror` (lines 107-110) is the only XNA-faithful way to test this
combination — not an invented shortcut.

### Behavioral correctness
Independently re-verified `ApplySamplerState`'s mirror-flag mapping
(`BgfxGraphicsBackend.cpp` lines 1933-1937): `addressU==2` sets `BGFX_SAMPLER_U_MIRROR`,
`addressV==2` sets `BGFX_SAMPLER_V_MIRROR` — matches the `TextureAddressMode` numeric encoding exactly
(`Mirror=2`). The read-back point (`W*4/5, H/2` → `U = 2*(4/5) = 1.6`, line 77) is correctly derived:
at `U=1.6` inside a doubled-width source rectangle, `Wrap` gives `fract(1.6)=0.6` (right/Blue texel),
`Clamp` saturates to the last texel (right/Blue), and `Mirror` reflects `2.0-1.6=0.4` (left/Red
texel) — all three independently re-derived by this audit and matching the file's own header-comment
table exactly.

### Logic
`RunCheck` (lines 59-84) correctly follows this shard's established per-check-fresh-frame pattern for
Bgfx's `GetBackBufferData` "first read per rendered frame" quirk (Task 406), redrawing the full scene
(`Clear` + `DepthStencilState` reset + `Begin/Draw/End`) on every retry rather than reusing state from
a prior iteration.

### C++ correctness
Uses `const_cast<SamplerState*>(&SamplerState::PointWrap)`/`PointClamp` (lines 113-114) to pass the
`static const` XNA preset objects through `SpriteBatch::Begin`'s non-const `SamplerState*` parameter.
This is the same idiom `SpriteBatch::Begin`'s own parameter type forces on every caller in this
codebase (the parameter isn't `const SamplerState*`); `Begin`/the sampler-application path only reads
from the pointee (confirmed no sibling test or the `SpriteBatch`/backend code path mutates a passed-in
`SamplerState*`), so this is safe in practice, though it is a mild API-shape wart (a `const
SamplerState*` parameter would let call sites like this one avoid the cast entirely) — not something
this test file can fix on its own.

### Robustness
The three-way comparison design is more robust than a single assertion: `wrapIsBlue`/`clampIsBlue`
are printed as "context" checks (still counted toward `passCount`), so a regression that broke *all*
three modes identically (e.g., the samplerFlags_ update path stops reaching the GPU at all) would
show up as 0/3 or a uniform failure pattern rather than being silently absorbed into a single
Mirror-only assertion.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — DepthStencilState reset happens once per RunCheck iteration but is redundant given `Clear`'s own state

- Severity: INFO
- Confidence: MEDIUM
- Category: maintainability
- Location/symbol: lines 65-67 (`DepthStencilState ds; ds.setDepthBufferEnableProperty(false);
  dev.setDepthStencilStateProperty(ds);`), inside the `for` retry loop
- Evidence: this 3-line reset is re-executed on every one of up to 20 retry iterations per `RunCheck`
  call (up to 3 calls × 20 = 60 times worst case), even though the `DepthStencilState` value itself
  never changes between iterations.
- Why it matters: purely a micro-inefficiency in a test executable (not a hot path in the sense the
  checklist cares about for production code) — noted for completeness, not an actual defect. The same
  pattern appears consistently across this entire test shard, so it is a shared idiom rather than a
  one-off oversight.
- Suggested future action: none — this is standard, harmless boilerplate for a short-lived CTest
  executable.

## Cross-File Observations

- Directly cross-references `bgfx_texture_address_mode_test.cpp` (this same batch) in its own header
  comment, correctly explaining why `U=1.25` (used there) would not discriminate Mirror from Clamp and
  choosing `U=1.6` instead — a genuine, verified design decision, not a copy-paste artifact.
- Shares the `ApplySamplerState` production code path with `bgfx_texturefilter_split_minmag_test.cpp`
  and `bgfx_texture_anisotropic_effect_test.cpp` (both in this batch); this file exercises the
  address-mode half of that same switch/if-chain while the others exercise the filter half.
- The file's own comment about `GraphicsDevice::SetDepthTestEnabled(false)` throwing on Bgfx was
  independently confirmed: `BgfxGraphicsBackend::SetDepthTestEnabled` (`BgfxGraphicsBackend.cpp` line
  2002) unconditionally calls `ThrowNo3DState()` — a real, deliberate, documented stub, not a stale
  claim.

## Missing or Weak Tests

None found for this file's stated scope. The three-way comparison already provides strong
discriminating power for the specific address-mode confusion this test targets.

## Positive Findings

- The `U=1.6` sample-point choice and its three-way disagreement table were independently re-derived
  by this audit and confirmed mathematically correct for Wrap/Clamp/Mirror semantics.
- Correctly identifies and works around a real FNA API gap (no `PointMirror` preset) by hand-building
  the `SamplerState` rather than inventing a non-existent XNA constant.
- The `ApplySamplerState` mirror-flag mapping this test exercises was independently confirmed correct
  against the `TextureAddressMode` enum's actual numeric values, not merely assumed from the switch
  statement's comments.

## Final Assessment

A carefully-designed discriminating test for `TextureAddressMode::Mirror` on Bgfx; the sample point
choice, the reflection-math derivation, and the three-way comparison were all independently
re-verified and found correct. No correctness issues found.
