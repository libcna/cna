# Audit: examples/sdlgpu_rendertarget2d_msaa_test.cpp

## Metadata

- Source file: `examples/sdlgpu_rendertarget2d_msaa_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `RenderTarget2D` MSAA proof for the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`SdlGpu_RenderTarget2DMSAA`, `cmake/Tests/SdlGpuTests.cmake:56-58`, `TIMEOUT 60 LABELS
  "SdlGpu"`)
- XNA/FNA relevance: direct — `RenderTarget2D` constructor's `preferredMultiSampleCount` parameter
  and `RenderTarget2D.MultiSampleCount` property (documented in FNA to reflect the real
  device-clamped value, not the raw request).
- FNA reference: `Graphics/RenderTarget2D.cs`.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`SdlGpuRenderTargetBackend` ctor lines 4255-4334, `ClampSampleCount`/`SampleCountToInt` lines
  264-293, `RenderToTarget`'s `FillColorTargetInfo` MSAA resolve lines 787-806),
  `src/Microsoft/Xna/Framework/Graphics/RenderTarget2D.cpp` (`getMultiSampleCountProperty`, line
  71, and its ctor's `rtBackend_->GetMultiSampleCount()` overwrite, line 66).

## Purpose

Six-check, 60-frame test proving `SDL_GPUSampleCount` texture creation plus
`SDL_GPUColorTargetInfo.resolve_texture` automatic resolve-on-render-pass-end for
`RenderTarget2D` (the `RenderTargetCube` leg of the same mechanism, SDLGPU-36, was proved
separately). Check A verifies `MultiSampleCount` property fidelity (a clamped, device-queried
value, not the raw request); Checks B/C draw a plain colored quad and a depth-tested pair of
overlapping quads into MSAA targets with no exception; Checks D/E — added, per the header comment
(lines 23-28), specifically because "SDL_gpu does not hard-crash" on a wrong-sample-count pipeline
— read back the resolved MSAA color/depth targets via `RenderTarget2D::GetData()` to prove the
resolved *content* is correct, not merely that rendering didn't throw.

## Executive Verdict

**Needs attention** — the test itself is correctly constructed and its 6 checks all pass for the
right structural reasons (real MSAA texture creation, real clamped `MultiSampleCount`, real
resolve-and-readback), but the file's own header comment's central claim about *why* Checks D/E
matter is contradicted by this project's own git-log-documented verification attempt: `plans/plan_sdlgpu.md`'s
SDLGPU-38 row states explicitly that reverting the actual pipeline-sample-count fix did **not**
change Checks D/E's pass/fail outcome on this project's own dev environment — meaning the
"only a real pixel readback... can [catch the bug]" claim (lines 25-28) is not just aspirational
but was tried and found not to hold, and this file's own comment does not disclose that. See F1.

## Checklist Results

### API / XNA / FNA parity

`RenderTarget2D(dev, w, h, mipMap, format, depthFormat, preferredMultiSampleCount, usage)` (lines
156-159) matches FNA's constructor argument order. `getMultiSampleCountProperty()` (line 190)
correctly reflects XNA's documented "returns the actual applied value" contract — confirmed via
`RenderTarget2D.cpp` line 66 (`multiSampleCount_ = rtBackend_->GetMultiSampleCount()` overwrites
the raw constructor argument immediately after backend construction) and
`SdlGpuRenderTargetBackend`'s ctor (line 4268: `multiSampleCount_ = SampleCountToInt(sampleCount)`,
where `sampleCount` is the *clamped* result of `ClampSampleCount(device, kFormat, multiSampleCount)`,
line 4267) — Check A (line 191, `applied > 1`) is asserting a genuinely device-queried value, not
an echo of the constructor argument.

### Behavioral correctness

- Check A: `ClampSampleCount` (lines 264-282) walks candidate sample counts down from the request
  (`8→4→2→1` for `requested>=8`, etc.) querying `SDL_GPUTextureSupportsSampleCount`, correctly
  tolerant of a driver that doesn't support 4x (would still report `>1` at 2x, matching the
  assertion's own `>1` rather than `==4`).
- Checks B/C: `RenderAndSample` (lines 104-148) draws into `rtMsaa_` (color-only) and
  `rtMsaaDepth_` (`DepthFormat::Depth24Stencil8`) — confirmed via the ctor (lines 4289-4333) that
  the depth texture's own `sample_count` is set to match the color attachment's MSAA sample count
  (line 4325: `depthInfo.sample_count = sampleCount`), a detail easy to get wrong (a mismatched
  depth/color sample count is invalid for most graphics APIs) and correctly handled here.
- Checks D/E: traced `RenderToTarget`'s `FillColorTargetInfo` (lines 792-806) — for an MSAA target,
  `out.texture = state.msaaTexture` (the real multisampled attachment) with
  `out.resolve_texture = state.colorTexture` and `out.store_op = SDL_GPU_STOREOP_RESOLVE` (lines
  797-801), matching the documented "automatic resolve at render-pass end" mechanism; `GetData()`
  (`SdlGpuRenderTargetBackend::GetData`, not shown in this file but consulted from this shard's
  cross-file reading) always downloads from `colorTexture_`, the single-sample resolve target, so
  the two checks genuinely read post-resolve content.

### Logic

`RenderAndSample` clears the depth-tested target with `Clear(Color(0,0,0,255), 1.0f)` (line 120)
then draws a farther Red quad (`z=0.5`) before a nearer Green quad (`z=-0.5`, lines 168-180) with
`SetDepthTestEnabled(true)`/`SetDepthWriteEnabled(true)` (lines 121-122) — correct draw order to
make "nearer wins" a genuine depth-test outcome rather than an incidental last-draw-wins result
(if depth testing were silently disabled, the *last-drawn* Green quad would still win, masking a
depth-test regression — see Missing or Weak Tests).

### Testing

Six checks appropriately layered: property fidelity (A), no-throw smoke coverage (B/C), and real
readback (D/E) — plus the ongoing 60-frame repeat (`RenderAndSample` in the `else` branch, lines
227-230) proving this isn't a one-shot-only path. The one substantive issue is not with what the
checks *do* (they are real, structurally sound assertions) but with the header comment's
overclaimed justification for Checks D/E — see F1.

## Detailed Findings

### F1 — The header comment's claim that "only a real pixel readback... can [catch the sample-count-mismatch bug]" is contradicted by this project's own documented verification attempt, and the file does not disclose this

- Severity: MEDIUM
- Confidence: HIGH (directly quoting `plans/plan_sdlgpu.md`'s own SDLGPU-38 row, which this audit
  consulted as required secondary context per the audit's cross-checking instructions)
- Category: test-coverage / documentation-accuracy
- Location/symbol: header comment lines 23-28 ("Checks D/E ... this is the actual discriminator
  for the adversarial-review finding that every pipeline hardcoded SDL_GPU_SAMPLECOUNT_1 ... only
  a real pixel readback proving the resolved content is correct can [catch it]"); Draw() Checks
  D/E (lines 202-218)
- Evidence: `plans/plan_sdlgpu.md`'s SDLGPU-38 row states, verbatim: *"Honest verification note: unlike
  this session's other pipeline-state bugs, a git-stash-style revert of just this fix did **not**
  reproduce any visible pixel-readback difference on this dev environment's Vulkan driver
  (`SdlGpu_RenderTarget2DMSAA`'s new Checks D/E, added specifically to try to catch this, still
  read back the correct resolved color with the fix reverted) — matching the adversarial review's
  own observation that SDL_gpu/this driver silently tolerates a pipeline/render-pass sample-count
  mismatch rather than hard-crashing or visibly corrupting output. This fix's correctness rests on
  matching SDL_gpu's own documented pipeline/render-pass contract ..., not on a reproduced visual
  failure — flagged here rather than silently claimed as empirically proven, since it wasn't."*
  This is a direct, first-party admission that Checks D/E — despite being added *specifically* to
  discriminate this bug — did not actually discriminate it when tried. This file's own header
  comment, however, still asserts (present tense, unqualified) that a pixel readback "can" catch
  the bug, without the plan document's own caveat.
- Why it matters: a future reader of this test file alone (without independently consulting
  `plans/plan_sdlgpu.md`, which is `EXEMPT`/not itself audited and easy to miss) would reasonably
  conclude Checks D/E provide real regression protection against a reintroduced hardcoded
  `SDL_GPU_SAMPLECOUNT_1` pipeline bug. Per the project's own documented experiment, they do not —
  a regression that reintroduced that exact bug would still pass this file's Checks D/E on this
  driver, silently. The actual protection against that regression is the `PipelineCacheKey()`
  hash including `sampleCount` (source-code-level correctness, not behavior verifiable by this
  test), which this project itself frames as resting on "matching SDL_gpu's own documented
  contract," not on empirical pixel proof.
- FNA/XNA comparison: N/A — this is a test-authoring/documentation-accuracy question, not an
  XNA/FNA behavioral question; the underlying `MultiSampleCount`/resolve behavior itself was
  independently confirmed correct in the Behavioral Correctness section above.
- Related files: `plans/plan_sdlgpu.md` (SDLGPU-38 row, `EXEMPT` `planning-tracking-doc`, not itself
  audited, but load-bearing context this file's own claim should have echoed).
- Suggested future action (not implemented by this audit): soften the header comment's claim to
  match the plan document's own honesty (e.g. "intended to discriminate this bug, though this
  project's own attempted git-stash-revert of the fix did not reproduce a visible difference on
  this driver — the fix's correctness rests on matching SDL_gpu's documented pipeline contract,
  not on empirical pixel proof"), so a future reader of the test file alone gets the same accurate
  picture the plan document already has.

## Cross-File Observations

- Directly contrasts with `sdlgpu_mrt_test.cpp`'s own Checks D/E in the same shard/batch, whose
  analogous "this is the real discriminator, not just didn't-throw" claim **was** independently
  git-stash-verified per `plans/plan_sdlgpu.md`'s SDLGPU-37 row (reverting to 1 attachment reproduced the
  predicted failure exactly). The two files make structurally identical rhetorical claims about
  their own Checks D/E, but only one of the two claims is actually backed by a successful
  falsification experiment — this is exactly the kind of claim this audit's brief calls for
  independently re-verifying rather than taking at face value, and the two files turned out to
  differ.
- Fog and skinned-normal-transform cross-cutting bugs are **not applicable**: this file exercises
  only `BasicEffect`+`VertexColorEnabled` (`colored3d.vert/frag.glsl`, no fog term, no skinning).
- `RenderTarget2D`'s MSAA-vs-mip mutual exclusion (`mipMap_ = mipMap && multiSampleCount_ == 0`,
  same rationale as `SdlGpuRenderTargetCubeBackend`'s own MSAA support per that file's own
  in-source comment) is not exercised by this file — this file never requests `mipMap=true` on an
  MSAA target — but this is a reasonable, narrow scope choice given a dedicated
  `SdlGpuRenderTargetCubeBackend`-side cube MSAA+mip test already exists elsewhere in this shard.

## Missing or Weak Tests

- See F1 — the file's own justification for Checks D/E overclaims their discriminating power for
  the specific bug (pipeline sample-count mismatch) they were introduced to catch, per the
  project's own documented experiment.
- Check C's depth-test proof (farther Red drawn first, nearer Green drawn second, nearer wins)
  would also pass if depth testing were silently disabled and the *last* draw simply overwrote the
  color buffer unconditionally — since Green is drawn last regardless of depth-test correctness,
  this specific check cannot distinguish "correct depth test" from "no depth test at all, last
  draw wins." A stronger version would draw Green (near) first, Red (far) second, and assert Green
  still wins only when depth testing is genuinely enabled and correctly configured.

## Positive Findings

- `MultiSampleCount` property fidelity (Check A) is a genuinely device-queried, clamped value, not
  an echo of the constructor request — confirmed by tracing both the C++ property and the backend
  ctor.
- The MSAA depth texture's sample count is correctly matched to the color attachment's, a detail
  that is easy to get wrong and was independently verified correct.
- The project's own plan document is unusually candid about a verification attempt that did *not*
  succeed (SDLGPU-38's honesty note) — a good engineering practice this specific test file's own
  comment simply didn't inherit; flagging the gap (F1) is meant to help close that specific
  documentation loop, not to imply any live behavioral defect.

## Final Assessment

The test itself is structurally sound and every check maps to real, independently-traced backend
behavior. The one substantive issue (F1) is a documentation-accuracy gap: this file's header
overclaims Checks D/E's discriminating power for the exact bug they were built to catch, a claim
this project's own git-log-documented verification effort already disproved but did not propagate
into this file's own comment.
