# Audit: examples/easygl_rendertargetcube_depthformat_test.cpp

## Metadata

- Source file: `examples/easygl_rendertargetcube_depthformat_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `RenderTargetCube` depth-buffer honoring integration test
- File type: C++ example/integration-test executable (`RenderTargetCubeDepthFormatTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::RenderTargetCube` (`RenderTargetCube.cpp`),
  `CNA::Internal::Backends::EasyGL::EasyGLRenderTargetCubeBackend` (`EasyGLGraphicsBackend.cpp`, `CreateResources`,
  lines ~731-819, and `MapDepthFormat`, lines 516-538)
- XNA/FNA relevance: `RenderTargetCube(device, size, mipMap, format, depthFormat)`,
  `DepthStencilState::Default`/`None`, `EnvironmentMapEffect` are real XNA 4.0 surface; the depth-test-gates-draws
  semantic is core `GraphicsDevice`/`DepthStencilState` behavior, judged against FNA's `DepthStencilState.cs`
  (`LessEqual` default comparison function) and general depth-buffer XNA semantics.
- Main related tests: this file (Task 877); explicitly mirrors `rendertarget2d_depth_test.cpp`'s near/far-quad
  methodology and reuses `easygl_rendertargetcube_sample_test.cpp`'s cube-sampling-via-`EnvironmentMapEffect`
  approach.

## Purpose

Verifies that `EasyGLGraphicsBackend::CreateRenderTargetCube()` genuinely honors the caller's requested
`DepthFormat` per-instance, rather than the pre-fix behavior of unconditionally allocating a real depth buffer
(`hasDepth=true` hardcoded) regardless of what was requested. Uses a differential near/far-quad methodology: draw a
near GREEN quad then a far RED quad at the same screen position with depth-test+write enabled; if a real depth
buffer exists, GREEN (nearer) wins; if `DepthFormat::None` was honored (no depth buffer), the far RED quad (drawn
last, no depth gate) overwrites GREEN. Correctly placed per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — this is a rigorous, correctly-designed regression test whose depth semantics, differential control
structure, and backend code paths were all independently verified; it is the strongest kind of test in this batch
because it has a genuine positive control (`Depth24Stencil8` must show GREEN) proving the harness can actually
detect "depth test is working," not just asserting one hardcoded outcome.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetCube(dev, kCubeSize, false, SurfaceFormat::Color, depthFormat)` (line 108) is the real 5-argument XNA
constructor overload. `dev.SetRenderTarget(&rtc, face)` (line 124) matches FNA's
`GraphicsDevice.SetRenderTarget(RenderTargetCube, CubeMapFace)` overload signature. `DepthStencilState::Default`
(line 128) and its `ClearOptions::Target | ClearOptions::DepthBuffer` companion (line 125) are real XNA members;
`DepthStencilState::Default`'s actual comparison function (`LessEqual`, matching FNA's `DepthStencilState.Default`
declaration) is exactly what makes "nearer z wins" the correct expected semantic here — independently confirmed via
`DepthStencilState.cpp`'s `Default` static member matching FNA's `LessEqual`/`DepthBufferWriteEnable=true`.

### Behavioral correctness — verified against production code
Traced `MapDepthFormat()` (`EasyGLGraphicsBackend.cpp:516-538`): returns `false` for `DepthFormat::None` (the
`default:`/explicit `case DepthFormat::None:` fallthrough, lines 534-536), and `true` with the correct GL internal
format for `Depth16`/`Depth24`/`Depth24Stencil8`. `EasyGLRenderTargetCubeBackend::CreateResources()`
(lines 731-819) only calls `depthRbo_.create()`/`set_storage`/`fbo_.attach_renderbuffer` **inside**
`if (MapDepthFormat(depthFormat_, ...))` (lines 802-816) — i.e. for `DepthFormat::None`, no depth renderbuffer is
created or attached to the FBO at all, so any depth test against that FBO is against a nonexistent depth buffer
(behaves as always-pass, matching the test's expected "far quad wins" outcome). This is exactly the Task 877 fix the
file's header comment describes, and the code genuinely implements it (not merely claimed).

`Clear(ClearOptions::Target | ClearOptions::DepthBuffer, ..., 1.0f, 0)` (line 125) is called even when there's no
depth buffer to clear — this is harmless (a GL `glClear(GL_DEPTH_BUFFER_BIT)` with no depth attachment is a
well-defined no-op, not an error) and doesn't invalidate the "no depth buffer" test case.

The test renders all 6 faces identically (lines 122-132) specifically so the result is independent of which face the
`EnvironmentMapEffect` reflection vector happens to sample (matches
`easygl_rendertargetcube_sample_test.cpp`'s identical design choice, confirmed consistent between the two files) —
this means the test is exercising "does depth-testing work when rendering into a cube face" uniformly across all 6
faces at once, not just one; a per-face-specific depth bug (e.g. one face's FBO attachment wired to the wrong
renderbuffer) would still likely be caught since a wrong face would then also fail its own local depth test and the
reflection vector landing on it would show the wrong color, but the test cannot attribute *which* face failed if only
one did — acceptable for a boolean-outcome regression test.

### Logic
The depth-state restore before the outer sampling quad (`dev.setDepthStencilStateProperty(DepthStencilState::None);`,
line 155, with an explicit and accurate comment at lines 152-154 explaining why: the backbuffer's own depth buffer
was never cleared this frame, so leaving `DepthStencilState::Default` active could incorrectly reject the sampling
quad) — this is a real, correctly-reasoned defensive step, not incidental; without it, the test could fail for an
unrelated reason (backbuffer depth-buffer garbage) and produce a false FAIL rather than exercising the actual feature
under test.

### Memory/resource lifetime
`RenderTargetCube rtc` and `Texture2D whiteTex` (lines 108, 136) are function-local stack objects inside
`RenderAndSampleFace`, destroyed at the end of each call — two independent instances (`DepthFormat::None` and
`Depth24Stencil8`) are created and destroyed cleanly with no shared/leaked state between them.
`envFx.setEnvironmentMapProperty(&rtc)` (line 161) takes a raw pointer to the stack-local `rtc`, valid for the
duration of the call since `envFx.Apply()`/`DrawUserPrimitives` execute before `rtc` goes out of scope at the end of
the same function — no dangling-pointer risk.

### C++ correctness
`closeTo`/`matches` (lines 93-99) use a fixed `±40` per-channel tolerance, consistent with the tolerance convention
used throughout this shard's other pixel-comparison tests (e.g. `easygl_rendertargetcube_sample_test.cpp`'s
`colourMatch`). `std::abs(a - b)` on `int` parameters — no signed/unsigned mismatch risk since `getRProperty()`
etc. return an unsigned byte type explicitly widened via the `int` parameter type at the call site.

### Performance
N/A — one-shot integration test; the per-face loop (6 iterations × 2 depth-format cases = 12 total FBO
binds/draws) is trivially cheap for a 32×32 cube target.

### Thread safety
N/A.

### Architecture
Correct XNA-facing-API-only usage; the depth-buffer-honoring behavior under test is exercised entirely through
`RenderTargetCube`'s public constructor and `GraphicsDevice::SetRenderTarget`, never reaching into
`CNA::Internal::Backends` directly.

### Maintainability
Header comment (lines 1-21) precisely describes both the bug (`hasDepth=true` hardcoded) and the fix, and both were
independently confirmed accurate against `MapDepthFormat`/`CreateResources`. `DrawFullQuad` (lines 57-68) is a small,
reusable free-function helper avoiding duplicated vertex-data boilerplate between the near and far quad draws.

### Portability
N/A — EasyGL-specific.

### Robustness
`pass_`/`fail_` counters and per-check PASS/FAIL printing (lines 77-91) give clear, greppable diagnostics; `check()`
prints the actual RGB triple on both pass and fail, aiding debugging of a near-miss tolerance failure.

### Cross-file consistency
Shares the "Task 896: `RasterizerState::CullNone` needed" fix comment and pattern with
`easygl_rendertargetcube_sample_test.cpp` and `easygl_rendertarget2d_msaa_test.cpp` (line 169 here) — consistent
cross-file application of the same underlying default-rasterizer-state change. Explicitly and correctly distinguishes
its own scope from `easygl_rendertarget2d_properties_test.cpp`'s "Task 335 already covers RenderTarget2D" note (line
3) — accurate cross-referencing, not a false claim (confirmed `RenderTarget2D`'s own depth-honoring path is separate
production code from `RenderTargetCube`'s, so this file's coverage is not redundant with a 2D-only test).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this is one of the more carefully constructed tests in the batch.

### F1 — All-faces-uniform rendering means a single-face-specific regression cannot be localized (informational)

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `RenderAndSampleFace` (lines 106-177), the `for (CubeMapFace face : faces)` loop (line 122)
- Evidence: identical GREEN-then-RED content is rendered into all 6 faces; the final readback only samples whichever
  face the reflection vector for the fixed outward-facing quad happens to hit.
- Why it matters: a hypothetical bug affecting only one face's FBO attachment (e.g. an off-by-one in the
  `TextureCubeMapPositiveX + face` arithmetic, `EasyGLGraphicsBackend.cpp:825-826`/`845-846`) would only be caught if
  the reflection vector happens to sample that specific face — this test's fixed forward-facing quad likely always
  samples the same 1-2 faces every run, so a defect isolated to a different face could go undetected by this test
  alone.
- Why it matters (less): since all 6 faces receive identical draw commands and the depth-honoring behavior under
  test (`MapDepthFormat`/depth attachment creation) is per-cube-instance, not per-face, a `DepthFormat`-honoring
  regression would affect all faces uniformly and would still be caught regardless of which face is sampled.
- FNA/XNA comparison: N/A.
- Related files: `easygl_rendertargetcube_sample_test.cpp` has the identical structural limitation for the same
  underlying reason.
- Suggested future action (not implemented by this audit): a per-face-targeted test that samples each face
  individually (e.g. via `Texture2D`-style `GetData` per face, if/when that API path exists for
  `RenderTargetCube`) would close this gap; not necessary for the specific depth-honoring bug this file targets.

## Cross-File Observations

- This file's per-face loop and `easygl_rendertargetcube_sample_test.cpp`'s per-face loop use the exact same 6-face
  enumeration order and `SetRenderTarget(&rtc, face)`/`Clear` pattern — a consistent, shared idiom across this
  shard's `RenderTargetCube` tests worth keeping in mind for any future shared-helper refactor.

## Missing or Weak Tests

- See F1 — no per-face-isolated readback exists anywhere in this shard for `RenderTargetCube`.
- No test combines a non-`None`/non-`Depth24Stencil8` format (`Depth16`, `Depth24`) with this same near/far
  methodology — only the two extremes (`None` vs `Depth24Stencil8`) are exercised for the actual depth-test-gating
  behavior; `Depth16`/`Depth24`'s `MapDepthFormat` branches are covered by
  `easygl_rendertargetcube_properties_test.cpp`'s property-only assertions but not by a behavioral depth-test-gating
  check.

## Positive Findings

- Genuine positive control (`Depth24Stencil8` must show GREEN) is present and was verified to actually validate the
  test's own detection capability, not just assert one expected outcome in isolation — this is the single strongest
  design element across this whole batch of files.
- The depth-state-restore-before-sampling step (line 155) reflects real, non-obvious domain understanding of a
  potential false-failure source and correctly defends against it.
- Accurate, specific header comment naming the exact prior bug (`hasDepth=true` hardcoded) and confirmed true against
  the actual pre-understood code path.

## Final Assessment

One of the best-constructed tests in this batch: a real differential methodology with a genuine positive control,
verified line-by-line against the actual depth-format-honoring fix it targets. Only a low-severity, informational
test-coverage note (F1, shared with its sibling sample test) keeps this from being flawless.
