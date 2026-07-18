# Audit: examples/easygl_msaa_test.cpp

## Metadata

- Source file: `examples/easygl_msaa_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — MSAA render+resolve pixel-readback test
- File type: `Game`-derived executable, CTest-registered as `cna_test_easygl_msaa` /
  `EasyGL_MSAA_4x_Readback` (`cmake/Tests/EasyGLTests.cmake:767-770`, itself preceded by the comment
  `# Task 146: MSAA 4× — multisampled renderbuffer + resolve on Present`)
- XNA/FNA relevance: direct — `GraphicsDeviceManager.PreferMultiSampling`,
  `PresentationParameters.MultiSampleCount`, `SpriteBatch`
- Production sources cross-checked: `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`
  (`PrepareDeviceSettings`), `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp`

## Purpose

Per its own header comment, verifies that `PresentationParameters.MultiSampleCount=4` is honored end
to end on EasyGL: a 4×-multisampled renderbuffer pair is created, rendering goes to that MSAA FBO,
`Present()`/`GetBackBufferData` resolves it via `glBlitFramebuffer` to the default framebuffer, and a
centre-pixel readback of a solid red full-screen quad confirms the resolved colour.

## Executive Verdict

**Needs attention.** The test's own stated purpose — verifying **4×** MSAA specifically — does not
match what the code actually configures or verifies. Nothing in this file ever sets
`MultiSampleCount` to `4`; it only sets `GraphicsDeviceManager.PreferMultiSampling = true`, which (per
`GraphicsDeviceManager.cpp:494-497`) defaults an unset `MultiSampleCount` to **8**, not 4 — a
discrepancy the file's own constructor comment half-admits ("GraphicsDeviceManager caps
MultiSampleCount at 8") while its top-of-file header comment and even its own CTest registration name
(`EasyGL_MSAA_4x_Readback`) still claim "4×". Separately, the actual scene drawn (a solid,
edge-to-edge full-viewport red fill) provides no way to distinguish "the MSAA pipeline genuinely
resolved" from "MSAA was silently never engaged and a plain single-sample render occurred" — both
produce an identical centre pixel, so the test's assertion cannot actually fail in the way its
header comment describes it verifying.

## Checklist Results

### API / XNA / FNA parity
`GraphicsDeviceManager.PreferMultiSampling`/`PresentationParameters` naming matches FNA. No parity
issue in the API surface used — the issue is a mismatch between this file's own stated intent and
its own configuration, not an XNA/FNA behavioral divergence.

### Behavioral correctness
Traced `GraphicsDeviceManager::PrepareDeviceSettings` (`GraphicsDeviceManager.cpp:490-498`):

```cpp
if (!preferMultiSampling_) { pp.setMultiSampleCountProperty(0); }
else if (pp.getMultiSampleCountProperty() == 0) { pp.setMultiSampleCountProperty(8); }
```

This file's constructor (lines 96-104) only calls
`gdm_->setPreferMultiSamplingProperty(true);` — it never calls
`pp.setMultiSampleCountProperty(4)` or any equivalent, and `PresentationParameters.MultiSampleCount`
starts at its own default (`0`, confirmed via the sibling `easygl_msaa_change_test.cpp`'s own
"GDM default MultiSampleCount=0" assertion, corroborated separately in this audit batch). So the
`else if` branch fires and the *actual* requested sample count is **8**, subsequently clamped only by
`GL_MAX_SAMPLES` at the backend level (`EasyGLGraphicsBackend.cpp` clamp logic near line 1352) — not
by anything in this file. The file's constructor comment (lines 97-99) is aware of this
("GraphicsDeviceManager caps MultiSampleCount at 8; the GL backend clamps to GL_MAX_SAMPLES at
runtime") — directly contradicting the file's own top-of-file header ("Task 146: EasyGL MSAA 4×
integration test", "Verifies that PresentationParameters.MultiSampleCount=4 is respected", lines 2-4)
and the CTest registration's own name/comment (`EasyGL_MSAA_4x_Readback`, "Task 146: MSAA 4×",
`cmake/Tests/EasyGLTests.cmake:766-770`). This is an internal, three-way, independently-verifiable
inconsistency (header vs. constructor comment vs. actual runtime value), not merely a stray one-line
typo.

Separately: the rendered content is `sb_->Draw(*redTex_, Rectangle(0,0,W,H), Rectangle(0,0,1,1),
Color::White)` — a single 1×1 solid-red texture stretched to fill the entire viewport, with no
internal edges or geometry. Every pixel of both the MSAA-resolved framebuffer *and* a hypothetical
single-sample fallback would be identical solid red — multisample anti-aliasing only visibly differs
from single-sampling at polygon edges/silhouettes, none of which exist in a full-viewport axis-aligned
quad. The assertion `centPx.getRProperty() >= 200 && G<=50 && B<=50` (lines 78-80) would pass
identically whether or not the MSAA FBO/resolve-blit path the header describes ever actually ran.

### Robustness
`result_` defaults to `1` (fail-by-default, line 41) — correct posture; not the source of the issue
here.

### Testing
This is exactly the "compiles and doesn't crash, but doesn't validate the behavior its name/comments
claim" pattern the audit brief calls out explicitly: the file's stated purpose (verify 4× MSAA
specifically; verify the MSAA-FBO-create → render → resolve-blit → readback pipeline) is not actually
exercised in a way that could fail if that specific pipeline were broken or bypassed. A test that
(a) explicitly reads back `device.getPresentationParametersProperty().getMultiSampleCountProperty()`
and asserts it matches whatever was actually requested (whether that's 4 or 8), and (b) draws
geometry with an actual edge (e.g. a rotated or non-axis-aligned quad, or a checkerboard) and samples
along that edge to detect anti-aliased blend values distinct from a hard single-sample edge, would
close both gaps and make this a genuine MSAA-pipeline test rather than a same-as-non-MSAA solid-fill
smoke test.

## Detailed Findings

### F1 — Test/CTest name and header comment claim "4×" MSAA; actual configured sample count is 8

- Severity: HIGH
- Confidence: HIGH
- Category: test-coverage / correctness (documentation-vs-behavior mismatch)
- Location/symbol: file header (lines 2, 4); constructor (lines 96-104, esp. comment lines 97-99);
  `GraphicsDeviceManager::PrepareDeviceSettings` (`GraphicsDeviceManager.cpp:494-497`); CTest
  registration (`cmake/Tests/EasyGLTests.cmake:766-770`)
- Evidence: the file never sets `MultiSampleCount` to 4 anywhere; `preferMultiSampling_=true` with an
  unset (`0`) `MultiSampleCount` deterministically becomes `8` per `GraphicsDeviceManager.cpp:496-497`.
  The file's own constructor comment already admits this ("caps MultiSampleCount at 8"), while the
  header two-line summary and the CTest target name both still say "4×" — a genuine, traceable
  self-contradiction within the same file/registration, not a matter of interpretation.
- Why it matters: anyone reading this test's name/header to understand "does CNA's 4× MSAA path
  work" is being told something the code does not actually check; if 8× MSAA silently broke while 4×
  MSAA (were it ever actually requested) still worked, or vice versa, this test's result would be
  uninformative either way, since it doesn't assert the sample count at all (see F2).
- FNA/XNA comparison: N/A (sample-count defaulting is a CNA-internal
  `GraphicsDeviceManager`/`PresentationParameters` simplification, not an XNA behavior).
- Related files: `easygl_msaa_change_test.cpp` (same batch) independently confirms the `8`-default
  behavior from the state-assertion side.
- Suggested future action (not implemented by this audit): either rename the file/CTest target and
  header to accurately describe "default/8×" MSAA, or make the file actually request `4` explicitly
  via `PresentationParameters.MultiSampleCount` before first `ApplyChanges()`/construction (matching
  what "vulkan_msaa_test.cpp" is described elsewhere as doing for a backend that supports pre-first-
  construction sample-count requests).

### F2 — Test provides no discriminating signal that the MSAA pipeline was actually exercised

- Severity: HIGH
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `Draw()` (lines 54-92), specifically the solid full-viewport red quad (lines 67-72)
  and the single centre-pixel assertion (lines 74-80)
- Evidence: a full-viewport, axis-aligned, single-solid-color draw is pixel-identical whether or not
  multisampling/resolve genuinely ran — no edge/silhouette exists in the scene for MSAA to visibly
  affect, and no assertion reads back the actually-applied `MultiSampleCount` to at least confirm a
  non-zero value was engaged at the state level.
- Why it matters: a regression that silently disabled the MSAA FBO path entirely (e.g. an
  accidentally-always-false `if (multiSampleCount_ > 0)` guard in `EasyGLGraphicsBackend.cpp`) would
  not be caught by this test — it would still print `[PASS]` with the exact same reasoning.
- FNA/XNA comparison: N/A.
- Related files: `EasyGLGraphicsBackend.cpp`'s MSAA FBO creation/blit-resolve logic (lines ~590-670,
  ~1400, ~1517-1552) is the actual code this test's header comment claims to be validating.
- Suggested future action (not implemented by this audit): assert
  `device.getPresentationParametersProperty().getMultiSampleCountProperty() > 0` (or the specific
  expected value) alongside the pixel check, and/or draw geometry with a real edge and check for
  anti-aliased (non-binary) blend values at a boundary pixel to actually exercise resolve-quality
  behavior distinct from a non-MSAA render.

## Cross-File Observations

- This file and `easygl_msaa_change_test.cpp` cover complementary but non-overlapping ground: the
  latter is a rigorous, fully-verified *state* test (does `PresentationParameters` honestly reflect
  reality); this file is meant to be the *rendering* test (does the actual MSAA FBO/resolve pipeline
  produce correct pixels), but as written only re-tests "does a solid-color `SpriteBatch` draw show
  up," which the non-MSAA EasyGL readback test (referenced in this file's own header, line 15) already
  covers.

## Missing or Weak Tests

- No assertion on the actually-applied `MultiSampleCount` (see F2).
- No geometry with an edge/silhouette to give MSAA resolve something visually distinguishing to get
  right or wrong (see F2).
- No test in this shard (so far as checked in this batch) that requests a *specific* sample count
  (e.g. 4) before first construction and confirms it round-trips, which is what this file's own
  name/header claims to be doing.

## Positive Findings

- `result_` defaults to fail (line 41), a correct default-to-fail posture.
- The constructor comment (lines 97-99), though it contradicts the file's own header/CTest name (see
  F1), is itself accurate about the real `GraphicsDeviceManager` behavior — a useful, honest detail
  that a careful reader can use to spot the inconsistency, as this audit did.

## Final Assessment

The file compiles, runs, and passes for a scene that would pass identically with MSAA fully disabled;
its stated purpose (verify 4× MSAA render+resolve) is not actually what it configures (8×, not 4×)
nor what it can detect (no edge geometry, no sample-count assertion). This is a genuine test-adequacy
gap in a file whose entire reason for existing is to validate a specific rendering pipeline path.
