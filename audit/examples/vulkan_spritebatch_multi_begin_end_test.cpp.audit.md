# Audit: examples/vulkan_spritebatch_multi_begin_end_test.cpp

## Metadata

- Source file: `examples/vulkan_spritebatch_multi_begin_end_test.cpp` (146 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `SpriteBatch` multi-Begin/End-per-frame regression test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_spritebatch_multi_begin_end …)` /
  `cna_register_backend_test(NAME Vulkan_SpriteBatch_MultiBeginEnd …)`, `cmake/Tests/VulkanTests.cmake:449-451`).
- XNA/FNA relevance: direct — `SpriteBatch.Begin()/Draw()/End()` reentrancy within one frame (an XNA-legal
  pattern with no FNA-documented restriction against calling `Begin`/`End` more than once per `Draw`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` —
  `VulkanSpriteBatchBackend::Begin()`/`End()` (lines 783-838, `BatchSnapshot` construction), and the
  `drawSpritesFor` lambda inside `RecordCommandBuffer()` (lines 6259-6340, accumulating `vbOff`/`ibOff`
  across `activeBatches_`).
- git corroboration: `90789f09`/`b9094009` "fix(Task 664): Vulkan SpriteBatch multi-Begin/End-per-frame
  data loss" (authored 2026-07-07 07:52), matching this file's own header attribution.

## Purpose

Regression test for a real, previously-shipped Vulkan-backend bug: a single `SpriteBatch` object running
two independent `Begin()/Draw()/End()` cycles inside one `Draw(GameTime)` call used to lose the first
cycle's geometry entirely, because `Begin()` cleared the very `vertices_`/`indices_`/`draws_` vectors the
prior `End()` had just populated, and the old `activeBatches_` tracked a raw pointer to that single mutable
object rather than an independent per-cycle snapshot. This file draws a red quad (left half of a 200×100
window) in cycle 1 and a blue quad (right half) in cycle 2 on the same `sb_` instance, then reads back one
pixel from each half and asserts both colors survive.

## Executive Verdict

**Healthy** — this is a well-targeted, correctly-designed regression test whose production-code fix was
independently traced end-to-end (see Behavioral correctness) and does exercise the exact bug scenario from
the file's own header. One real robustness gap: unlike several sibling Vulkan tests authored around the
same time, this file has no retry-on-blank-frame safety net for the documented AMD/RADV first-post-present
readback flake (see F1).

## Checklist Results

### API / XNA / FNA parity — N/A/PASS
`sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, samplerState, nullptr, nullptr, nullptr,
Matrix::getIdentityProperty())` (lines 94-97, 102-105) matches the 7-parameter
`Begin(SortMode, BlendState, SamplerState*, DepthStencilState*, RasterizerState*, Effect*, Matrix)`
overload declared in `SpriteBatch.hpp:150-157` verbatim, an XNA-compatible overload. No FNA/XNA parity
question is raised by the multi-cycle-per-frame pattern itself; FNA's `SpriteBatch` has no such restriction.

### Behavioral correctness — PASS
Independently traced the fix this test targets:
- `VulkanSpriteBatchBackend::End()` (lines 812-838) now moves `vertices_`/`indices_`/`draws_` into a
  freshly-allocated `BatchSnapshot` and pushes `{snapshot, activeRT_}` onto `backend_->activeBatches_`
  **at End() time**, so a second `Begin()` (line 786: `vertices_.clear()`) can never touch a prior cycle's
  already-harvested data — confirmed by reading both `Begin()` and `End()` in full.
- `RecordCommandBuffer()`'s `drawSpritesFor` lambda (lines 6259-6340) iterates `activeBatches_` and
  memcpy's each snapshot into the shared per-frame `spriteVB_`/`spriteIB_` ring buffers at an
  **accumulating** `vbOff`/`ibOff` (lines 6265-6338), rather than a hardcoded offset 0 — so two snapshots
  targeting the same render target (here, both `nullptr` = backbuffer, since neither cycle calls
  `SetRenderTarget`) compose additively instead of overwriting each other.
- `activeBatches_.clear()` (line 6809) runs once per `RecordCommandBuffer()` call, confirming snapshots do
  not leak across frames.
- Traced the `GetBackBufferData` → `ReadBackbuffer` (`VulkanGraphicsBackend.cpp:6982`) path: the first of
  the test's two `GetBackBufferData` calls (checking `(50,50)`) has `hasNewWork = true` (both
  `activeBatches_` entries still present), triggers `SubmitFrame(true)` (deferred copy held before
  present), then `activeBatches_.clear()` runs inside that same `RecordCommandBuffer()`. The **second**
  `GetBackBufferData` call (checking `(150,50)`) then has `hasNewWork = false` and
  `readbackStagingValid_ = true`, so it is served from the *same* frame's cached staging buffer rather than
  re-submitting (which the code's own comment says would "destroy the content of all but the first read").
  This means both pixel checks correctly observe the **same** single composited frame containing both
  cycles' quads — exactly what the test needs to prove.
- The two sample points (`50,50` inside the red region `(0,0,100,100)`; `150,50` inside the blue region
  `(100,0,100,100)`) are well clear of the shared boundary at `x=100`, so no edge-antialiasing ambiguity.

### Logic — PASS
`colourMatch(got, want, tol=60)` (lines 48-53) checks each of R/G/B independently with `tol=60`; loose
enough to tolerate ordinary blend/format rounding but still discriminating (e.g. a half-transparent
render at ~128 would fail `|128-255|=127 > 60`). `Color::White` tint (line 98/106) and `BlendState::Opaque`
(explicitly set at both the device level, line 91, and per-`Begin()` call) rule out any alpha-composited
ambiguity affecting the readback.

### C++ correctness — PASS
RAII throughout (`unique_ptr` for `gdm_`/`sb_`/`redTex_`/`blueTex_`); no raw ownership. `done_` guard
(line 86) correctly prevents re-entrant `Draw()` bodies from re-running the two cycles on subsequent frame
callbacks before `Exit()` takes effect.

### Robustness — WEAK (see F1)
No retry loop around either `GetBackBufferData` call, unlike sibling Vulkan tests authored in the same
window (see F1).

### Testing — PASS for its own stated scope
Both checks are genuine pixel-color assertions tied to a real, previously-broken code path (not merely
"compiles and runs"); a `printf`-based PASS/FAIL trace is emitted per check and the process exit code
reflects overall `result_`.

### Cross-file consistency — PASS with one gap
Consistent with `SpriteBatch.hpp`'s public `Begin`/`Draw`/`End` API and with the Vulkan backend's
`BatchSnapshot` design also exercised by `vulkan_scissor_test.cpp`/`vulkan_viewport_subregion_test.cpp`'s
shared `RecordCommandBuffer()` path; see F1 for the one place it diverges from sibling test robustness
conventions.

## Detailed Findings

### F1 — No blank-frame retry loop around `GetBackBufferData`, despite the underlying flake being documented in this same codebase before this file was authored

- Severity: MEDIUM
- Confidence: HIGH (the exact failure code path was read directly in production source, and the mitigating
  pattern already existed in a sibling test committed earlier)
- Category: test-robustness / flakiness-risk
- Location/symbol: `Draw(const GameTime&)` lines 84-128 (both `dev.GetBackBufferData(&reg, &px, 0, 1)`
  calls, lines 119, in the `for (const auto& c : checks)` loop, have no retry wrapper)
- Evidence: `VulkanGraphicsBackend::ReadBackbuffer()` (`VulkanGraphicsBackend.cpp:6982-7011`) explicitly
  documents and handles a case where `SubmitFrame(true)` fails because the swapchain is "out-of-date
  (common on first frame under Wayland/RADV)", in which case it **zeroes the output pixel buffer** and
  expects the caller to detect the all-zero/blank result and retry. `vulkan_scissor_test.cpp` (Task 329,
  authored 2026-06-29, i.e. *before* this file's Task 664 commit of 2026-07-07) and
  `vulkan_viewport_subregion_test.cpp` (Task 880, same day but 6 hours later) both wrap their
  `GetBackBufferData` calls in an explicit `for (int i = 0; i < 20; ++i) { ...; if (!isBlack(...)) break; }`
  retry loop with a comment citing "the intermittent AMD/RADV driver flake that returns the clear colour on
  the first `GetBackBufferData` call after a swapchain present" — and `NEXT.md` independently confirms the
  actual dev/CI hardware is a real `AMD Radeon 780M (RADV PHOENIX)`, not a hypothetical. This file has
  neither the retry loop nor any other mitigation, despite being authored chronologically *after* the
  scissor test already established the pattern for the identical `GetBackBufferData`-after-Vulkan-present
  scenario.
- Why it matters: on the actual documented flaky hardware/driver combination, this test's very first
  `GetBackBufferData` call could return an all-zero pixel (from `ReadBackbuffer`'s own explicit "swapchain
  out-of-date, zero the output" branch) even though both `SpriteBatch` cycles rendered correctly — which
  `colourMatch(Color(0,0,0,0), kRed, 60)` would correctly (for the wrong reason) report as `FAIL`, causing
  a spurious CTest failure unrelated to the Task 664 fix this file exists to protect.
- FNA/XNA comparison: N/A — this is a CNA/Vulkan-backend-specific test-infrastructure robustness gap, not
  an XNA behavior question.
- Related files: `examples/vulkan_scissor_test.cpp`, `examples/vulkan_viewport_subregion_test.cpp` (both
  have the mitigation this file lacks); `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp:6982-7011`
  (the code whose own comment documents the exact failure mode).
- Suggested future action (not implemented by this audit): wrap the per-check `GetBackBufferData` call (or
  the whole two-cycle Draw+readback sequence) in the same bounded retry-until-non-blank loop already used
  by `vulkan_scissor_test.cpp`/`vulkan_viewport_subregion_test.cpp`.

## Cross-File Observations

- This file, `vulkan_texture_address_mode_test.cpp`, `vulkan_texture_srgb_test.cpp`, and
  `vulkan_texture_mip_filter_effect_test.cpp` (all audited in this same batch) share the identical F1 gap;
  `vulkan_viewport_subregion_test.cpp` is the only file in this batch that already has the mitigation.
  `vulkan_vertex_format_test.cpp` also lacks it but was authored on 2026-06-27, *before* the scissor test
  established the pattern, so its omission is chronologically more excusable even though the underlying
  risk is identical.
- The `BatchSnapshot`/`activeBatches_` design this test protects is shared production code also relied on
  by every other Vulkan `SpriteBatch`-drawing test in this suite; a regression here would very likely be
  caught by several other tests too, somewhat lowering (but not eliminating) the value of any one specific
  test in this area.

## Missing or Weak Tests

- No coverage of 3+ Begin/End cycles in one frame (only tests exactly 2), nor of interleaving a
  `SpriteBatch` cycle with a 3D `BasicEffect` draw between the two `SpriteBatch` cycles (a plausible
  real-world pattern that would additionally exercise `pending3D_`/`activeBatches_` co-existing in the same
  frame). Not required for this specific regression, but would strengthen confidence in the general
  accumulating-offset design.
- See F1 for the retry-loop gap.

## Positive Findings

- The test is a precise, minimal reproduction of the exact bug scenario in the file's own header comment,
  and this audit independently confirmed (by reading `Begin()`/`End()`/`RecordCommandBuffer()` line-by-line)
  that the current production code's fix genuinely addresses that scenario — this is not a case of a test
  merely re-asserting its own narrative without verification.
- The two-call `GetBackBufferData` cache-reuse behavior (first call submits+harvests, second call reads the
  same cached frame) was traced and confirmed to correctly serve both pixel checks from one composited
  frame, which is a subtle correctness property this test implicitly, though not explicitly, relies on.

## Final Assessment

A genuine, well-aimed regression test that correctly exercises and validates the real Task 664 fix in
`VulkanSpriteBatchBackend`. Its only shortcoming is process-robustness, not correctness: it omits a
blank-frame retry safety net that sibling Vulkan tests in this exact suite already adopted for the same
documented real-hardware flake, leaving it exposed to spurious failures unrelated to the behavior it exists
to protect.
