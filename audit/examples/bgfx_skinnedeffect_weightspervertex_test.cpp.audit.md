# Audit: examples/bgfx_skinnedeffect_weightspervertex_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_weightspervertex_test.cpp` (171 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect.WeightsPerVertex` GPU-enforcement pixel
  test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_weightspervertex …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_WeightsPerVertex …)`,
  `cmake/Tests/BgfxTests.cmake:467-471`).
- XNA/FNA relevance: direct — FNA's real `Skin(vin, boneCount)` (`HLSL/SkinnedEffect.fx`) only sums
  the first `WeightsPerVertex` (1/2/4) weight/index pairs; CNA previously summed all 4
  unconditionally on every backend (Task 401's audit finding, fixed by Task 895).
- Related production code: `SkinnedEffect.cpp::FillGpuDrawParams()` line 395
  (`p.weightsPerVertex = weightsPerVertex_`), `vs_skinned3d.sc` lines 16-23 (the `weightsPerVertex
  >= 2.0`/`>= 4.0` gating), `BgfxGraphicsBackend.cpp::ReadBackbuffer()` (lines 303-357).

## Purpose

Task 895's regression test for the GPU-side `WeightsPerVertex` enforcement gap: reuses the same
2-bone `+0.5`-net-shift scenario as `bgfx_skinnedeffect_twobone_blend_test.cpp` (bone 0 =
`Translate(-0.5,0,0)`, bone 1 = `Translate(1.5,0,0)`, weights `0.5/0.5`), but adds a 3rd bone
(`Translate(100,0,0)`) addressed by deliberately-populated "garbage" weights in slots 2/3
(`w2=w3=0.5`, `i2=i3=2`), with `WeightsPerVertex=2` explicitly set. If the property is honored
(only slots 0/1 summed), the net shift is `+0.5` and the quad lands centred on-screen; if a backend
incorrectly still sums all 4 slots, the extra `0.5*100 + 0.5*100 = +100` pushes the quad
`+100.5` off-screen, and the centre sample reads background green instead of the textured quad —
an unmistakable, binary-strength discriminator (unlike a subtly-wrong pixel color).

## Executive Verdict

**Significant correctness risk** — this specific test is documented, in this project's own git
history, as a **currently-failing, confirmed pre-existing CTest failure** as of the most recent
commit that touched this area (`0cb4a591`, 2026-07-16), and it has not since been fixed or further
root-caused. This is not speculative: it is stated directly in that commit's own message and echoed
in `plans/plan_graphics.md`'s Task 1104 entry (see F1). Independently, this audit also found that this
file's own render/readback pattern diverges from every one of its close siblings in this exact
shard in a way that plausibly explains a flaky/wrong result on this specific backend (see F2).

## Checklist Results

### API / XNA / FNA parity
`fx.setWeightsPerVertexProperty(2)` and `fx.SetBoneTransforms({3 bones})` are used correctly per
the real XNA API surface; the test's own derivation of "correct net shift = +0.5 vs. buggy
net shift = +100.5" is mathematically sound and matches FNA's `Skin(vin, boneCount)` semantics
exactly (confirmed against `HLSL/SkinnedEffect.fx` and this audit's review of the sibling
`twobone_blend` test, which independently re-derives the same `+0.5` net shift for an equivalent
2-bone case).

### Behavioral correctness
The underlying arithmetic claim (own comment, lines 20-24) was independently re-verified by this
audit and is correct: with `WeightsPerVertex=2`, only slots 0/1 (weights `0.5/0.5`, bones
`Translate(-0.5,0,0)`/`Translate(1.5,0,0)`) are summed by `vs_skinned3d.sc`'s
`if (weightsPerVertex >= 2.0) skinMat += ...` gate (line 20), giving the same `+0.5` net shift as
the two-bone-blend sibling; slots 2/3 (bone 2, `Translate(100,0,0)`) are only summed if
`weightsPerVertex >= 4.0` (line 21), which is false here, so they are correctly excluded — **when
the shader-side gating works as intended**.

### Logic — see F1/F2 below.

### C++ correctness
`SkinnedGpuVertex` (52 bytes) matches production stride-52 layout.

### Robustness
See F2: this file omits the per-checkpoint full-redraw safeguard that every one of its four close
siblings in this exact shard (`identity_bones`, `translation_bone`, `twobone_blend`, `vertexcolor`)
explicitly implements and documents as necessary for this backend.

### Testing
The underlying test *design* (garbage weights on a huge, unmistakable off-screen-pushing
translation) is excellent and exactly mirrors the correct, already-verified-working design used by
this file's EasyGL/Vulkan siblings (per this file's own header comment) — the concern here is
specifically about this file's own render/readback reliability on the Bgfx backend, not the test's
conceptual design.

## Detailed Findings

### F1 — This exact CTest is a documented, confirmed pre-existing failure as of the project's own most recent commit touching this area, and remains unfixed

- Severity: HIGH
- Confidence: HIGH — sourced directly from this project's own git history, not inferred
- Category: correctness / regression / test-coverage
- Location/symbol: the whole file / `Bgfx_SkinnedEffect_WeightsPerVertex` CTest registration
- Evidence: commit `0cb4a591` ("feat(Task 1104): Bgfx real per-vertex-lit shader +
  PreferPerPixelLighting dispatch", 2026-07-16) states verbatim in its own commit message: *"Full
  regression: `ctest -R \"^Bgfx_(BasicEffect|SkinnedEffect|EnvironmentMap)\"` 32/33
  (`Bgfx_SkinnedEffect_WeightsPerVertex` is a real, confirmed **pre-existing** failure -- verified
  via `git stash` that it fails identically against the unmodified code); ... A full unfiltered
  `ctest -R \"^Bgfx_\"` sweep (this worktree's first-ever Bgfx build) surfaced several other
  pre-existing failures/timeouts, spot-checked via the same git-stash method and confirmed
  unrelated to this task."* This is corroborated verbatim in `plans/plan_graphics.md`'s own Task 1104
  entry. By contrast, Task 895's own original commit (`c0f4d981`, 2026-07-11, the commit that
  *introduced* this test) reports a clean full regression ("Full regression suites reconfirmed
  clean on all 3 backends (only known pre-existing baseline failures remain: `Vulkan_DepthBias`;
  `Bgfx_RenderTarget2D_MsaaResolve`, `Bgfx_RenderTargetCube_DepthFormat`)" — this specific test is
  *not* listed as a known failure at that time). This means the test passed when written and later
  regressed to failing, was subsequently noticed and confirmed-but-not-investigated, and has not
  been revisited since in the git history available to this audit.
- Why it matters: this test exists specifically to guard against a previously-real, confirmed FNA
  divergence (`WeightsPerVertex` being a GPU no-op) on the Bgfx backend. A silently-failing
  regression test provides zero protection — either the underlying GPU-enforcement bug has
  regressed on Bgfx specifically (a real FNA-parity gap re-opened without anyone noticing via
  normal CI, since the failure was dismissed as "pre-existing" rather than triaged), or the test
  itself has a reliability bug that produces false failures (masking whether the real behavior is
  actually still correct) — either way, this is an unresolved, currently-live problem in the
  project's own regression suite for this exact file's subsystem.
- FNA/XNA comparison: N/A — this is a currently-unknown-root-cause CI/test-health issue, not a
  freshly-identified FNA-parity question (the FNA-parity question this test targets was already
  correctly identified and fixed at the production-code level by Task 895 across all 3 backends;
  what's unresolved is why *this specific test*, on *this specific backend*, no longer passes).
- Related files: `cmake/Tests/BgfxTests.cmake:467-471` (registration); `plans/plan_graphics.md` (Task
  895/1104 entries, where this status is recorded); see F2 for this audit's own candidate root-cause
  theory.
- Suggested future action (not implemented by this audit): re-run
  `ctest -R Bgfx_SkinnedEffect_WeightsPerVertex` in a working Bgfx build to reconfirm current status,
  and specifically test the F2 hypothesis (redraw before every `GetBackBufferData` call) as a
  candidate fix before assuming the underlying GPU-enforcement logic itself has regressed.

### F2 — This file is the only one of its four close siblings in this shard that reads `GetBackBufferData()` three times after a single draw, contradicting this project's own documented Bgfx readback constraint

- Severity: MEDIUM
- Confidence: MEDIUM — the mechanism is directly traced in source (not just pattern-matched), but
  this audit could not build/run the Bgfx backend in this sandbox to directly observe the failure
  mode this produces
- Category: correctness / test-reliability
- Location/symbol: `Draw()` lines 74-160 — one `device.Clear()`/`fx.Apply()`/`device.DrawPrimitives()`
  sequence (lines 84-124), followed immediately by three sequential
  `device.GetBackBufferData(&leftReg/centReg/rightReg, ...)` calls (lines 133-135) with no redraw
  between them
- Evidence: `bgfx_skinnedeffect_identity_bones_test.cpp`, `_translation_bone_test.cpp`,
  `_twobone_blend_test.cpp`, and `_vertexcolor_test.cpp` — the other four close siblings in this
  exact shard — all explicitly implement a `renderAndRead()` helper that performs a **full**
  `Clear`+`Apply`+`SetVertexBuffer`+`DrawPrimitives`+`GetBackBufferData` pass *per checkpoint*,
  with a header comment in each explaining why: *"Bgfx's readback only reliably reflects a single
  fresh `GetBackBufferData()` call per rendered frame ... so each check point gets its own full
  clear+draw+read pass rather than sharing one frame across 3 reads."* This audit traced the actual
  mechanism in `BgfxGraphicsBackend::ReadBackbuffer()` (lines 303-357): every single call requests a
  screenshot (`bgfx::requestScreenShot`) and then **advances the bgfx frame** via `bgfx::frame()`
  (up to 3 times, waiting for the async callback) before returning — meaning every
  `GetBackBufferData()` call is itself a frame-advancing operation, not a passive readback of
  whatever is currently on screen. Calling it a second and third time after only *one* draw
  submission means those second/third calls advance to frames with **nothing newly submitted**
  to view 0, whose actual rendered content is not guaranteed by anything in this backend's own
  code to still be the just-drawn quad. `docs/coverage.md`'s own Bgfx feature table independently
  corroborates the underlying fragility: *"GetBackBufferData readback | Implemented via
  requestScreenShot callback; not integration-tested"*.
- Why it matters: this is exactly the anti-pattern this shard's own sibling tests were written to
  avoid, based on a real, previously-encountered Bgfx behavior on this project. If this mechanism
  is indeed the (or a contributing) cause of the F1 failure, then 2 of this test's 3 checks
  (`centReg`/`rightReg`, both read *after* the first `GetBackBufferData` call has already
  internally advanced the frame) are reading back a frame where nothing new was drawn — which could
  produce a stale, blank, or otherwise incorrect result unrelated to whether `WeightsPerVertex` is
  actually honored by the shader, i.e., a **false failure** that would incorrectly appear to
  indict the production `WeightsPerVertex` GPU-enforcement fix (Task 895) itself.
- FNA/XNA comparison: N/A — this is a CNA-internal Bgfx-backend/test-authoring reliability
  question, not an XNA/FNA behavior question.
- Related files: `bgfx_skinnedeffect_identity_bones_test.cpp`/`_translation_bone_test.cpp`/
  `_twobone_blend_test.cpp`/`_vertexcolor_test.cpp` (the four siblings that correctly apply the
  redraw-per-checkpoint pattern); `BgfxGraphicsBackend.cpp` lines 303-357
  (`ReadBackbuffer`/`bgfx::frame()`).
- Suggested future action (not implemented by this audit): rewrite this file's `Draw()` to use the
  same per-checkpoint `renderAndRead()`-style full redraw pattern as its four siblings, then
  re-run the CTest to see whether F1's failure resolves; if it does not, the root cause lies
  elsewhere (e.g., a genuine regression in the shader-side `WeightsPerVertex` gating specifically
  on Bgfx) and F1's "pre-existing failure" note should be escalated for direct shader-level
  investigation instead.

## Cross-File Observations

- This file was authored (Task 895, commit `c0f4d981`) reusing the exact 2-bone scenario from
  `bgfx_skinnedeffect_twobone_blend_test.cpp` (confirmed identical bone transforms/weights for
  slots 0/1), but does *not* reuse that sibling's `renderAndRead()` per-checkpoint redraw helper —
  a real, git-history-confirmed regression opportunity that this audit believes is worth
  prioritizing over assuming the underlying shader fix has itself regressed.
- The equivalent EasyGL and Vulkan `WeightsPerVertex` tests (per this file's own header comment,
  "matching Task 408's own net +0.5 shift") are explicitly stated by Task 895's own commit message
  to have passed cleanly on those two backends — this reinforces that the Bgfx-specific failure is
  most plausibly a Bgfx-backend-specific issue (matching F2's own backend-specific readback-timing
  theory) rather than a shared cross-backend logic defect in `SkinnedEffect.cpp`'s
  `FillGpuDrawParams()` itself (which is common code, not backend-specific).

## Missing or Weak Tests

The test's own conceptual design (garbage weights forcing a binary on-screen/off-screen
discriminator) is sound and does not need additional coverage; what is missing is confidence that
the test *itself* reliably reports the true state of the underlying feature — see F1/F2.

## Positive Findings

- The underlying test design (reusing a known-good 2-bone scenario, adding unmistakable garbage
  data on a 3rd bone) is the same well-regarded pattern already validated and praised by this audit
  for `bgfx_skinnedeffect_twobone_blend_test.cpp`.
- The project's own engineering practice of explicitly calling out "confirmed pre-existing failure,
  verified via git stash" in commit messages (rather than silently ignoring or silently disabling a
  failing test) is a genuinely good signal of process discipline, even though the failure itself
  remains unresolved.

## Final Assessment

This file's underlying test design is sound, but it is the one file in this shard that omits an
established, documented safety pattern its own close siblings all use, and — independently of this
audit's own analysis — is already confirmed by the project's own git history to be a currently
failing CTest with an unconfirmed root cause. This warrants prompt, dedicated follow-up (starting
with F2's redraw-pattern fix as the most likely and cheapest hypothesis to test) rather than being
left as an accepted "known pre-existing failure."
