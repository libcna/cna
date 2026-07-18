# Audit: examples/easygl_sample_moving_quad3d_test.cpp

## Metadata

- Source file: `examples/easygl_sample_moving_quad3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` per-frame World-translation sample/test
- File type: C++ example/integration-test executable (`MovingQuad3DSample : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect` (`World`/`View`/`Projection`
  defaults, `BasicEffect.hpp:40-45`; `VertexColorEnabled`, line 48)
- XNA/FNA relevance: `BasicEffect.World`/`.View`/`.Projection` defaults, `VertexColorEnabled`/`LightingEnabled`
  defaults — judged against FNA's `BasicEffect.cs` (`world=view=projection=Matrix.Identity`, lines 43-45;
  `lightingEnabled` defaults false).
- Main related tests: this file (Task 498, sample 1/4); `easygl_sample_keyboard_cube3d_test.cpp` (Task 498 sample
  3/4) is the input-gated variant directly modeled on this file's structure and constants.

## Purpose

A classic "moving 3D object" demo: a small unlit, vertex-colored `BasicEffect` quad translated along World-space X
by a fixed `+0.3`/`Update()` amount over 4 real frames, proving the full `Update()`-state → World matrix →
`BasicEffect` → EasyGL draw → backbuffer-readback pipeline for genuine 3D rendering (as opposed to `SpriteBatch`
2D). Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy** — the motion math, default-matrix assumptions, and screen-space mapping were all independently
verified against the real `BasicEffect` source and hold up exactly; shares one minor misleading-comment finding
(F1) with its input-gated sibling.

## Checklist Results

### API / XNA / FNA parity
`effect_->VertexColorEnabled = true` (line 105) sets the real public `BasicEffect::VertexColorEnabled` field
(default `false`, `BasicEffect.hpp:48`) directly — confirmed this is the class's own established convention (a
public field, not a `getX`/`setX` pair, matching FNA's own `BasicEffect.VertexColorEnabled` public-property style
closely enough that the direct-field-write here is the correct, intended usage, not a CLAUDE.md-convention
violation on this file's part).

### Behavioral correctness
`BasicEffect`'s `World`/`View`/`Projection` default to `Matrix::getIdentityProperty()` (`BasicEffect.hpp:41,43,45`),
confirmed to exactly match FNA's `BasicEffect.cs` (`world = view = projection = Matrix.Identity`) — validating the
header's stated assumption (lines 9-12) that leaving View/Projection untouched makes World-space X coincide with
NDC X, so the same `(x+1)/2*width` screen mapping (`ndcToScreenX()`, line 54) used elsewhere in this shard applies
directly here without any camera setup.

Motion: `x_` starts at `kStartX=-0.6f`, `Update()` unconditionally adds `kStepX=0.3f` every call (line 111,
contrast with the keyboard-gated sibling's conditional add) over `kFrameCount=4` calls → final `x_=0.6`, matching
the header's stated `start ~13, end ~51` screen-X expectation for a 64-wide backbuffer
(`ndcToScreenX(-0.6)=(0.4)*0.5*64=12.8→12`, `ndcToScreenX(0.6)=(1.6)*0.5*64=51.2→51` — both match the header's
"~13"/"~51" approximation).

`Quad` is `0.3x0.3` NDC units (`kHalf=0.15f`), solid Red, `VertexColorEnabled=true`/`LightingEnabled=false`
(`BasicEffect`'s own real defaults, confirmed `lightingEnabled_ = false` at `BasicEffect.hpp:367`) — so the
rendered pixel should be the exact vertex color with no lighting/texture modulation, matching the header's claim.

### Logic
Final-frame checks (lines 134-139) verify both (1) the quad is genuinely Red at the *end* position, and (2) the
*start* position is no longer Red — together proving actual per-frame motion rather than a static quad that
happens to visually cover both positions (e.g. if the quad were wide enough or motion were a no-op due to a World
matrix bug that silently left translation at zero). This two-sided check is a meaningfully stronger test than
checking only the end position.

### Memory/resource lifetime
`effect_` (`std::unique_ptr<BasicEffect>`), standard ownership, no dangling-pointer risk.

### C++ correctness
No exact-float-equality risk here (unlike the keyboard-gated sibling's `x_ == expectedX` check) — this file only
checks rendered pixel color thresholds, not exact float positions.

### Performance
N/A — 4-frame, 64x64-backbuffer test.

### Robustness
No malformed-input path; retry loop present, see F1.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings.

### F1 — Retry-loop comment claims "extra present cycles" but the loop never calls `Present()`

- Severity: LOW
- Confidence: MEDIUM
- Category: maintainability / misleading comment
- Location/symbol: `Draw()`'s retry loop (lines 128-133): `for (int i = 0; i < 10; ++i) { endPx = readAt(...); if
  (endPx.getRProperty() > 0) break; drawQuad(dev); }`; comment: `// retry: some drivers need a couple of extra
  present cycles`
- Evidence: identical mechanism and evidence to the same finding recorded in full in
  `easygl_sample_keyboard_cube3d_test.cpp.audit.md`'s F1 — `Game::Tick()` (`Game.cpp:357-449`) confirms `Present()`
  (via `EndDraw()`) is only ever called once, after the entire user `Draw()` override (loop included) returns; the
  retry loop's repeated `drawQuad(dev)` calls re-clear/re-render the same off-screen framebuffer without ever
  presenting in between.
- Why it matters: same as the sibling file — a documentation-accuracy issue that could mislead a future maintainer
  into "fixing" this by adding a real `Present()` call between retries, changing this `Draw()`'s presentation
  semantics unintentionally.
- FNA/XNA comparison: N/A.
- Related files: `easygl_sample_keyboard_cube3d_test.cpp` (identical comment/pattern, recorded there in full).
- Suggested future action: correct the comment, or confirm/eliminate whatever race it's actually working around.

## Cross-File Observations

- Nearly identical structure, constants (`kSize=64`, `kFrameCount=4`, `kStartX=-0.6f`, `kStepX=0.3f`,
  `kHalf=0.15f`), and `ndcToScreenX()` helper to `easygl_sample_keyboard_cube3d_test.cpp` — that file is this one's
  direct input-gated variant (Task 498 sample 3/4 modeled on sample 1/4). The two should be kept in sync if either's
  motion/mapping constants ever change.
- This is the one file of the two "moving quad" samples that unconditionally advances every `Update()` call (no
  input gate) — the intentional "baseline" this shard's other sample builds on.

## Missing or Weak Tests

- No case exercises a direction reversal mid-run (motion is monotonic +X only across all 4 frames) — would more
  thoroughly exercise `Matrix::CreateTranslation` being re-applied correctly each frame rather than accumulated
  incorrectly (e.g. a bug that summed translations instead of replacing them would still pass this monotonic test).
- No intermediate-frame pixel check (only start/end positions are verified) — a bug that moved the quad to the
  wrong intermediate position on frames 2-3 (but correctly by frame 4, e.g. due to an off-by-one frame-counter
  bug that happens to net out by the end) would not be caught.

## Positive Findings

- The two-sided "end position is Red AND start position is not Red" check is a genuinely stronger correctness
  proof than an end-position-only check, correctly ruling out a "static quad wide enough to cover both" false
  positive.
- Confirmed `BasicEffect`'s default matrix/lighting/vertex-color state exactly matches FNA's own defaults,
  validating this test's entire screen-space-mapping assumption.

## Final Assessment

A correctly-derived, well-targeted 3D-motion regression test; its only issue is a misleading retry-loop comment
(F1) shared verbatim with its input-gated sibling test.
