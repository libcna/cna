# Audit: examples/easygl_environmentmapeffect_golden_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect`, golden-image variant
  (`examples-tests-easygl` shard)
- File type: C++ example/integration test, using the shared `CNA::Examples::PixelTestGame` base
  (`examples/common/PixelTestGame.hpp`) rather than a hand-rolled `Game` subclass.
- Related production code: `EnvironmentMapEffect::FillGpuDrawParams()`
  (`src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp:398-467`), EasyGL's
  `EnsureEnvMapped3DProgram()` fragment shader (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3197-3241`)
- Golden fixture: `examples/golden/easygl_environmentmapeffect_golden_test.png` (present on disk, 87 bytes —
  a tiny 8x8 solid/near-solid PNG, consistent with an 8x8 flat-color region; binary asset, out of this
  audit's per-file scope per `AUDIT_SCOPE.md` rule 9, referenced here only for existence).
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_Golden` (`cmake/Tests/EasyGLTests.cmake:123-125`).

## Purpose

Task 469 test. Reuses the combined-scene setup from the (out-of-batch) `easygl_environmentmapeffect_combined_test.cpp`
(Task 399) but validates it through `PixelTestGame::CompareGoldenImage()` (a whole 8x8 region diffed against
a checked-in reference PNG) in addition to the single-center-pixel `ExpectPixel()` check the older test used.
Exercises a *non-identity* `World=CreateScale(2,1,1)` and a real perspective camera together with texture,
emissive color, env-map amount, specular, and Fresnel all simultaneously — the batch's one "everything at
once" capstone-style scene, versus the other 7 files' single-variable-isolation approach.

## Executive Verdict

**Healthy**, with one process/reproducibility caveat worth flagging (F1: the golden PNG's actual pixel
content cannot be independently re-derived from this audit without running the binary, so its correctness
rests on `ExpectPixel`'s independent numeric check plus trust that `CNA_UPDATE_GOLDEN` was invoked correctly
when the PNG was created).

## Checklist Results

### API / XNA / FNA parity
`PASS`. `setFresnelFactorProperty(1.0f)` correctly annotated in-line (line 94) as "real FNA default --
Fresnel enabled", matching `EnvironmentMapEffect.cs:370`/`EnvironmentMapEffect.cpp:43`.

### Behavioral correctness
`PASS`, re-derived independently:
  - `EmissiveColor=(0.5,0.5,0.5)`, default `DiffuseColor=(1,1,1)`, `alpha=1`: `FillGpuDrawParams()`
    (`EnvironmentMapEffect.cpp:424-426`) computes `emissiveColor = (emissive + ambient*diffuse)*alpha =
    0.5` per channel (ambient defaults to `(0,0,0)`, per `EnvironmentMapEffect.hpp:389`). With
    `DirectionalLight0` enabled but its own diffuse defaulting to `(0,0,0)` (same implicit-default pattern
    noted in several sibling reports), `litRGB = 0*diffuseColor + 0.5 = 0.5` per channel.
  - `baseColor = litRGB * texColor = 0.5 * (200,100,50)/255 = (100,50,25)` after `*255` rounding — matches
    the file's own header-comment derivation ("baseColor=EmissiveColor*Texture=(100,50,25)") exactly.
  - `EnvironmentMapSpecular=(0.4,0.4,0.4)`, cube alpha `=128/255≈0.502`, `combinedAlpha=diffuseColor.a*
    texColor.a=1*1=1`: additive specular term `= 0.4*0.502*1 ≈ 0.2 → 51/255` per channel, matching
    `rgb ~= (100,50,25)+(51,51,51)=(151,101,76)` — exactly the `ExpectPixel` call's literal
    `Color(151,101,76,255)` at line 104. This is the same math independently verified in the
    `_specular_test.cpp` report for a `(0,0,0)`-textured-cube case, here reused with a non-identity `World`
    and a real camera, and the near-head-on camera geometry (`eye=(0,0,3)` looking at the origin) makes the
    Fresnel-suppression term negligible as the comment claims (consistent with the `_fresnel_test.cpp`
    finding that head-on incidence drives `viewAngle→1`, suppressing the *reflection RGB* lerp term — note
    this does *not* suppress the specular/alpha term, which FNA's own `PSEnvMapSpecular` correctly excludes
    from Fresnel weighting per its `EnvironmentMapSpecular is not affected by the Fresnel setting` XML doc
    comment, `EnvironmentMapEffect.cs:319-321` — confirmed the CNA shader's structure matches: `vFresnel`
    only gates the `mix(baseColor,envSample.rgb...)` term, never the `+uEnvMapSpecular*envSample.a*
    combinedAlpha` term, at `EasyGLGraphicsBackend.cpp:3236`).
  - The non-identity `World=CreateScale(2,1,1)` only affects vertex *positions* (stretching the quad
    horizontally) and, via `WorldInverseTranspose`, the normal — but since the quad's normal `(0,0,1)` lies
    along the *unscaled* Z axis, a `Scale(2,1,1)` (X-only) leaves it undistorted; the center pixel sampled
    is therefore unaffected by the scale, consistent with the test reusing Task 399's flat-scale-invariant
    derivation without needing to account for normal skew here (unlike `_worldtransform_test.cpp`, which
    deliberately uses a normal *not* aligned with the unscaled axes to force this distinction to matter).

### Logic
`PASS`. `ExpectPixel(...)` (numeric, independent-of-PNG) and `CompareGoldenImage(...)` (PNG-diff) are
correctly run as two independent checks against the same rendered frame — a genuinely useful redundancy: if
the checked-in PNG were ever accidentally stale/corrupted, `ExpectPixel` would still catch a real rendering
regression, and vice versa a bug only in the *derivation comment* (not the code) would still be caught by
the PNG diff. The two checks are not merely duplicative.

### Memory/resource lifetime
`PASS`. Consistent RAII ownership pattern (`std::unique_ptr<TextureCube>`, local `Texture2D tex`) matching
every sibling file in this batch.

### Architecture
`PASS`. Correctly migrated to the shared `PixelTestGame` base (see `examples/common/PixelTestGame.hpp`)
rather than hand-rolling the `Game` subclass/`main()`/`pass_`/`fail_` boilerplate every other file in this
batch still uses — the newest and most maintainable structural pattern in this 8-file batch.

### Testing
This is itself a test file. See "Missing or Weak Tests."

### Cross-file consistency
`PASS`. `RunTest()` override correctly replaces the hand-rolled `Draw()` override every sibling file in this
batch uses; `main()` correctly reduces to a single `CNA::Examples::RunPixelTest<EnvironmentMapEffectGoldenTest>()`
call (line 121-123) rather than manual `game.Run(); return game.getResult();` — matching `PixelTestGame.hpp`'s
own documented usage example precisely.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Golden PNG's actual pixel content is not independently verifiable from source alone; correctness depends on `CNA_UPDATE_GOLDEN` having been invoked against genuinely-correct rendering, not just "whatever rendered at commit time"

- Severity: LOW
- Confidence: MEDIUM
- Category: test-robustness / process
- Location/symbol: `CompareGoldenImage("environmentmapeffect-combined-scene", ..., "examples/golden/easygl_environmentmapeffect_golden_test.png", 20)`, line 105-108
- Evidence: `PixelTestGame.hpp`'s own documentation (lines 39-40) describes the golden-image workflow as
  "run once with `CNA_UPDATE_GOLDEN=1`... review the written PNG, then commit it" — a process step that,
  unlike `ExpectPixel`'s hand-derived numeric expectation, cannot be verified from the `.cpp` source text
  alone; this audit confirmed the PNG file exists (87 bytes, consistent with a tiny near-solid-color 8x8
  image) but did not decode/re-render it (out of this file's static-review scope; the binary PNG itself is
  `EXEMPT` per `AUDIT_SCOPE.md`).
- Why it matters: this specific file mitigates the risk well by *also* running `ExpectPixel` against an
  independently-derived numeric value (see Behavioral correctness above) — so even a wrong/stale golden PNG
  would not silently mask a regression here, only weaken the *region*-level (8x8, includes some
  anti-aliased/blended edge pixels beyond the flat center) coverage `CompareGoldenImage` uniquely provides.
  The residual risk is narrow: a regression that changes only the non-center pixels of the 8x8 region while
  leaving the exact center pixel correct would be caught by `CompareGoldenImage` but not `ExpectPixel` —
  meaning if the PNG *is* stale, that specific narrow class of regression could go undetected without this
  audit being able to confirm or refute it from source alone.
- FNA/XNA comparison: N/A (test-infrastructure concern).
- Suggested future action (not implemented by this audit): none required given the `ExpectPixel` safety net
  already in place; noted for the record as the boundary of what a static, non-executing audit pass can
  verify about golden-image-based tests in general.

## Cross-File Observations

- The only file in this 8-file batch using `PixelTestGame`/`CompareGoldenImage` rather than the hand-rolled
  `Game`/`pass_`/`fail_`/`printf` pattern every other sibling uses — a legitimate, more modern convention
  (per `PixelTestGame.hpp`'s own stated rationale of being additive, opt-in infrastructure for *new* tests
  without retrofitting the ~330 existing ones), not an inconsistency needing fixing.
- Directly reuses the numeric derivation from the (out-of-batch) Task 399 `_combined_test.cpp` and
  cross-checks it against the same `EnvironmentMapSpecular`-scaled-by-cube-alpha formula independently
  verified in this batch's own `_specular_test.cpp` report — internally consistent across 3 separate files.

## Missing or Weak Tests

- Only checks one 8x8 region centered on the quad; does not include a region straddling the quad's edge
  against the background clear color, which would exercise anti-aliasing/blending behavior the golden-image
  mechanism is specifically suited for (per `PixelTestGame.hpp`'s own comment about MSAA edge blending being
  a primary motivating use case for `CompareGoldenImage`).

## Positive Findings

- Genuinely redundant, complementary verification (independently-derived numeric expectation +
  checked-in-image diff) against the same render, rather than two checks that would fail together.
- Clean adoption of the newer shared test-infrastructure base class, reducing boilerplate versus every
  sibling file in this batch.

## Final Assessment

A well-constructed golden-image test that correctly pairs an independently-verifiable numeric assertion with
a broader region-diff check, mitigating the one structural limitation (F1, LOW) that golden-image tests
inherently have when reviewed statically. No correctness defects found in the derivation or code.
