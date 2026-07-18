# Audit: examples/easygl_blendstate_alphablend_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_alphablend_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 113
- Registered as: `cna_test_easygl_blendstate_alphablend` (`cmake/Tests/EasyGLTests.cmake:1344-1348`, CTest name
  `EasyGL_BlendState_AlphaBlend`). **Also cross-compiled for Vulkan** (`cna_test_vulkan_blendstate_alphablend`,
  `cmake/Tests/VulkanTests.cmake:106-110`, CTest name `Vulkan_BlendState_AlphaBlend`) **and for D3D9/D3D11**
  (`cna_d3d9_test`/`cna_d3d11_test` targets in `cmake/Tests/D3D9Tests.cmake:121`/`D3D11Tests.cmake:54`) — this is
  the single most widely-reused file in this batch, compiled for four distinct backends from one source file.
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend` (`BlendState.cpp:7`),
  `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`).
- XNA/FNA relevance: exercises `BlendState::AlphaBlend`; judged against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/BlendState.cs`.

## Purpose

Task 304: verifies `BlendState::AlphaBlend` implements the **premultiplied**-alpha equation literally
(`colorSourceBlend=alphaSourceBlend=One`, `colorDestinationBlend=alphaDestinationBlend=InverseSourceAlpha`) — i.e.
it must NOT multiply the source colour by alpha itself, since the caller is expected to have already premultiplied
it. Draws a correctly-premultiplied 50%-alpha red (`Color(128,0,0,128)`, i.e. raw red 255 scaled by ~0.5) over a
green background and checks the result lands near `(128,127,0)`, specifically ruling out `(≈64,...)`, which would
indicate the shader mistakenly applied `NonPremultiplied`'s `SourceAlpha` equation (double-multiplying by alpha)
instead.

## Executive Verdict

**Healthy.** This is the strongest-designed test in the batch: it doesn't just check "some blending happened," it
specifically discriminates between the two blend presets (`AlphaBlend` vs `NonPremultiplied`) that are most likely
to be confused during a port, via the `notDoubleMultiplied` check. Assertions are correctly derived from the real
preset values and correctly account for integer rounding. No stale Vulkan-bug comment issue (unlike five sibling
files) — this file makes no claim about Vulkan's current correctness.

## Checklist Results

### API / XNA / FNA parity
`BlendState::AlphaBlend` = `{colorSourceBlend=One, alphaSourceBlend=One, colorDestinationBlend=InverseSourceAlpha,
alphaDestinationBlend=InverseSourceAlpha}` (`BlendState.cpp:7`) — confirmed identical to FNA's
`Blend.One, Blend.One, Blend.InverseSourceAlpha, Blend.InverseSourceAlpha` (`BlendState.cs`). `Blend::InverseSourceAlpha`'s
semantics (`{1-As,...}`) are documented identically in both the FNA source (`Blend.cs`) and the CNA header
(`Blend.hpp`) — confirmed via direct read of both.

### Behavioral correctness
Math check: source `(128,0,0,128)`, dest `(0,255,0,255)`. `AlphaBlend` equation:
`out = src*1 + dst*(1 - srcA/255)`. `srcA/255 = 128/255 ≈ 0.502`, so `1 - 0.502 ≈ 0.498`.
R = `128*1 + 0*0.498 = 128`. G = `0*1 + 255*0.498 ≈ 127`. The file's own comment states exactly this, and the test
checks `rInBand`/`gInBand` as `[110,145]` — a band wide enough to tolerate GPU/driver rounding (per the project's
own documented `~98`-file tolerance convention) while narrow enough to still separate 128 from the ~64 a
double-multiply bug would produce. `notDoubleMultiplied` (`R>=100`, line 83) is a second, more direct guard against
the exact double-multiply failure mode the test is designed to catch — a deliberate, sound redundancy, not dead
logic, since `rInBand`'s lower bound (110) already implies `R>=100`, but making the specific failure condition
explicit as its own named boolean makes the intent unambiguous to a future reader (confirmed by reading the
FAIL-branch message, which explains precisely this).

### Logic
`RasterizerState::CullNone` correctly applied for the same CCW-winding reason as all other files in this batch
(verified against FNA's `RasterizerState()` default `CullMode=CullCounterClockwiseFace`).

### Memory/resource lifetime
Same pattern as sibling files: stack-constructed `BasicEffect`, `done_`/`result_` single-run latch. No issues.

### C++ correctness
No casts, no UB. Consistent `Color got(0,0,0,0)` pre-initialization before the out-param readback call.

### Performance
N/A — single-shot correctness test.

### Thread safety
N/A — single-threaded example executable.

### Architecture
Hand-rolled `Game` subclass, consistent with the rest of the pre-Task-461 population in this batch. No
architectural concerns.

### Maintainability
Clean, single-purpose, no stale/misleading comments found (checked specifically for a Task-868/Vulkan reference,
as five sibling files in this batch carry one — this file does not).

### Portability
No platform-conditional code in the source itself; portability is entirely a function of which CMake target
compiles it (EasyGL/Vulkan/D3D9/D3D11, see Metadata).

### Robustness
No preflight GPU/display check (same accepted limitation as all hand-rolled-`Game` files in this batch).

### Testing
This is itself a test. Its design is a positive example for this batch: rather than merely checking "the result is
in some blended range," it specifically isolates the ONE most-likely porting bug (accidentally implementing the
sibling preset's equation) via a targeted lower-bound check tied to a documented failure value (~64).

### Cross-file consistency
Its expected values are numerically similar to, but conceptually the inverse test of, `easygl_blendstate_nonpremultiplied_test.cpp`
(Task 305) — confirmed by reading both: `AlphaBlend` here uses an already-premultiplied source `(128,0,0,128)` and
expects `~128`; `NonPremultiplied` uses a raw source `(255,0,0,128)` and also expects `~128` (because it multiplies
by alpha itself) — together the two tests correctly prove *which* side (caller vs. GPU pipeline) performs the
alpha multiplication, exactly as this file's own header comment claims.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One LOW-severity, low-confidence observation:

### F1 — B-channel is never asserted, only documented as "≈0" in the comment

- Severity: LOW
- Confidence: LOW
- Category: test-coverage
- Location/symbol: `pass = rInBand && gInBand && notDoubleMultiplied;` (line 85) — no B-channel check.
- Evidence: the header comment computes `B ≈ 0` but the runtime assertion never checks
  `got.getBProperty()`, unlike `easygl_blendstate_opaque_test.cpp`'s analogous check, which does assert all three
  channels via a single compound condition.
- Why it matters: minor — R and G alone already fully discriminate the three failure modes this test cares about
  (correct blend, double-multiply bug, wrong-preset-entirely), so a wrong B value alone would need to be a very
  specific, currently-hard-to-imagine bug (e.g. a channel-swizzle error) to slip through undetected here; still, a
  one-line addition would close this gap cheaply and match the project's own convention in the Opaque test.
- FNA/XNA comparison: N/A.
- Suggested future action: add a `bOk = got.getBProperty() <= 15` (or similar) check for parity with the
  `Opaque`/`SeparateFactors` sibling tests' full-channel-coverage style, if this file is touched again.

## Cross-File Observations

- This file is the most widely cross-compiled in the batch (4 backends: EasyGL, Vulkan, D3D9, D3D11) — a change to
  its assertions or comments has the widest blast radius of any file in this shard; worth flagging for the
  `examples-tests-d3d9`/`examples-tests-d3d11`/`examples-tests-vulkan` shard audits that they will also encounter
  this exact source.
- Notably does NOT carry the stale Task-868-Vulkan-bug narrative that five of its siblings in this batch do,
  despite being cross-compiled to Vulkan itself — consistent with the fact that `AlphaBlend`'s colour factors
  (`One`/`InverseSourceAlpha`) do not coincidentally match Vulkan's old hardcoded equation
  (`SourceAlpha`/`InverseSourceAlpha`), so this specific test genuinely failed pre-fix and genuinely passes
  post-fix, with no "coincidental pass" caveat ever needed.

## Missing or Weak Tests

See Finding F1 (B-channel not asserted). No other coverage gap found — this test is otherwise a good template for
"does the test discriminate the *specific* bug it claims to, not just ‘blending happened'."

## Positive Findings

- Best-designed discriminating check in this batch: explicitly rules out the specific wrong-equation failure mode
  (`notDoubleMultiplied`), not just "close to expected."
- Correct, verified-against-FNA preset values and blend-equation math.
- No stale/misleading Vulkan commentary, unlike five sibling files.
- Clear FAIL-branch diagnostic message explaining exactly what a specific wrong value would mean.

## Final Assessment

A well-designed, currently-accurate test with no correctness or documentation defects found. The only gap (missing
B-channel assertion) is cosmetic given the test's other two channels already fully discriminate its target bug.
