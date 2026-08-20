# Graphics compatibility report — computed from real test runs

> **Status update, 2026-07-11: all 5 "confirmed bugs" below are now fixed.** This report's own
> central gate (§3, §6) was "5 confirmed bugs + 5 BLOCKED tasks" as of 2026-07-09. As of this
> update: `IndexElementSize` (Task 921), Vulkan `BlendState` (Task 868), EasyGL anisotropic
> filtering (Task 918), `Model` root-bone default (Task 916), and `SpriteBatch::Draw`'s optional
> source rectangle (Task 922) are **all closed** — 5/5. Of the original 5 BLOCKED tasks, Vulkan
> `OcclusionQuery` (447) is also closed (2026-07-10); the other 4 (686/687, 725, 732) still need a
> project-owner architecture decision. **Do not treat the percentages and bug counts in this report
> as current** — they are a dated snapshot of Task 500's 2026-07-09 milestone declaration, kept
> below for its methodology (how the numbers were computed), not as a live status. For current
> status, see `NEXT.md` §5 (the actively maintained bug list) and
> `docs/graphics-renderer-feature-matrix.md`.

**Written for Task 499 (`plans/plan_graphics.md` Phase 55).** This project has historically described
Graphics maturity with informal, hand-derived percentages (e.g. `docs/xna-4-api-coverage.md`'s
older §8 table — "~98%", "~85%" — acknowledged in Task 490's own "Release checklist" section as
color commentary, not a rigorous measurement). This report replaces that with numbers computed
directly from real `ctest` runs and the real per-class table (Task 483), each shown with its
arithmetic. It does not re-derive the qualitative detail in `docs/xna-4-api-coverage.md` (Tasks
482–485), `docs/graphics-renderer-feature-matrix.md` (Task 451), or `docs/migration-guide.md`
(Tasks 486–489) — see those for the "why", this report is the "how many."

All figures below are pulled from `plans/plan_graphics.md`'s own Task 493–498 rows (re-read directly,
not from memory, before writing this report).

## 1. Per-renderer test pass rate

Two different numbers per renderer, since Task 497 showed they can diverge meaningfully:
**"integration-only"** (the `examples/`-sourced, `<Renderer>_`-prefixed `add_test()` set — real
window/GPU pixel-readback tests) vs. **"full suite"** (integration tests plus the ~4357
renderer-agnostic `tests/` unit pool, which includes generic 3D-object-construction tests with no
per-renderer awareness).

| Renderer | Integration-only | Full suite | Source |
|---|---|---|---|
| EasyGL | 166/168 = **98.81%** | 4522/4525 = **99.93%** (4527/4530 = **99.93%** after Task 498's 5 new tests) | Tasks 493/494/498 |
| Vulkan | 87/93 = **93.55%** | 4369/4378 = **99.79%** | Task 495, cross-checked against Task 911's baseline |
| Bgfx | 54/56 = **96.43%** | 4413/4414 = **99.98%** baseline, but see correction below | Task 496, cross-checked against Task 448's baseline |
| SDL_Renderer | 67/67 = **100.00%** (2D-only, by design) | 4410/4421 = **99.75%** (11 failures, all by-design "3D not supported" throws, not bugs) | Task 497 |

**Correction found while cross-checking (2026-07-09):** `docs/xna-4-api-coverage.md`'s per-renderer
table (Task 484) cited Bgfx's known baseline as "1" pre-existing failure
(`Bgfx_RenderTarget2D_MsaaResolve`), but Task 496's own integration-only rerun found 2 —
`Bgfx_RenderTarget2D_MipChain` is *also* an already-documented pre-existing flake (Tasks
455/877/906/912, ~1-in-8 rate), just never folded into that row's own count. Fixed inline in
`docs/xna-4-api-coverage.md`'s per-renderer table as part of this task. The corrected Bgfx
full-suite baseline is **4412/4414 = 99.95%**, not 99.98% — a small but real correction, exactly
the kind of drift this task exists to catch.

**Vulkan is the clear low point** among the 4 renderers at 93.55% integration-only, entirely
attributable to 2 already-tracked, named issues (Task 868's fake `BlendState`, 5 of the 6 failures;
1 isolated `RasterizerState.DepthBias` sub-case) — not a diffuse reliability problem.

## 2. Per-class API-surface coverage (Task 483's table, re-tallied)

Task 483's per-class table rates 26 major Graphics classes (`Model`/`ModelMesh`/`ModelBone` counted
as one combined row) against 4 axes: Present, Implemented, Tested, FNA-compatible. Re-counted
directly from the table's own ✅/⚠️/❌/N/A marks (not from memory):

| Axis | ✅ | ⚠️ | ❌ | N/A | ✅ rate |
|---|---|---|---|---|---|
| Present | 26 | 0 | 0 | 0 | 26/26 = **100%** |
| Implemented | 22 | 4 (`Texture2D`, `ShaderEffect`, `BlendState`, `OcclusionQuery`) | 0 | 0 | 22/26 = **84.6%** fully clean, 4/26 = 15.4% partial |
| Tested | 26 | 0 | 0 | 0 | 26/26 = **100%** (tested on *at least one* renderer — see caveat below) |
| FNA-compatible | 12 | 11 | 2 (`BlendState`, `IndexBuffer`) | 1 (`ShaderEffect`, CNAEXT) | 12/26 = **46.2%** fully clean, 11/26 = 42.3% partial, 2/26 = 7.7% confirmed bug |

**Important honest caveat, not glossed over**: the "Tested" axis in Task 483's table is a single
blended column — "is this class exercised by at least one automated test on at least one renderer"
— it does **not** decompose into a per-class-per-renderer cross-tabulation (e.g. "is `SkinnedEffect`
tested on Bgfx specifically"). That finer-grained data doesn't exist as a single queryable table
anywhere in this project today; the closest real proxy is Task 484's per-renderer table's own
"Fully correct" / "Partial" prose columns, which are renderer-scoped but not a strict per-class
matrix. **This is a real gap in this project's own tracking granularity, not something this report
can compute honestly from existing data** — flagging it here rather than inventing a number.

**This is a different, and more revealing, number than the test-pass-rate table above**: a class
can have 100% passing tests and still be rated ⚠️/❌ on FNA-compatible (e.g. `IndexBuffer`'s tests
all pass today — including the ones that assert the wrong numeric values, per Task 921's own
finding — while genuinely not matching FNA). Test pass rate alone would hide this; the per-class
axis table is what actually surfaces it.

## 3. What "confirmed bug" and "BLOCKED" mean for these numbers

Per Task 485's "Known deviations" list and Task 490's own release checklist, as of this report:

- **5 confirmed bugs, not yet fixed**: Task 921 (`IndexElementSize` values), Task 868 (Vulkan
  `BlendState`), Task 918 (EasyGL `Anisotropic` fallback), Task 916 (`Model` `Root` default), and
  Task 922 (`SpriteBatch::Draw`'s 7th overload has a required `Rectangle` where FNA's is optional —
  found by Task 487, after Task 485's own table was written, and not yet folded into it there;
  noting the same drift here that Task 490 already flagged).
- **5 BLOCKED tasks** (need a project-owner architecture decision, not just engineering time): 447
  (Vulkan `OcclusionQuery`), 686/687 (SDL_Renderer `Wrap`/`Mirror`), 725 (SDL_Renderer
  `Texture3D`/`TextureCube`), 732 (EasyGL non-`Color` `SurfaceFormat`).

Neither count has changed since Task 490's own checklist was written (2026-07-09, same day) —
these 5+5 numbers are the two hard gates Task 500's own "declare 1.0" criteria will need to check
against.

## 4. One honest combined figure — clearly labeled, not blended across unlike things

**"46.2% of major Graphics classes (12/26) have zero known FNA-compatibility gap on any tested
renderer; 42.3% (11/26) have a narrow, named partial gap; 7.7% (2/26) have a confirmed bug; the
remaining 3.8% (1/26) is a CNAEXT extension with no FNA-compatible axis at all."** This is the one
number in this report closest to "how compatible is Graphics," and it is explicitly a percentage of
**classes**, not of API surface area, lines of code, or test count — those would each give a
different number, and this report deliberately does not average them into one score, matching Task
490's own explicit warning against inventing a single blended "Graphics is N% done" figure without
saying what it's a percentage of.

## 5. Assessment against Task 500's own gate criteria

Task 500 (`plans/plan_graphics.md`, next task) reads: *"Declare `Microsoft.Xna.Framework.Graphics` 1.0
compatibility milestone only if Tasks 491–499 pass or deviations are explicitly documented."*
Based on the real numbers in this report:

- **Tasks 491–499 all genuinely pass** by their own stated criteria ("must pass" / "must pass or
  documented skips") — every failure encountered across all of Phase 55 is a previously-documented,
  understood, named issue (5 confirmed bugs + 5 BLOCKED tasks + environment-specific flakes), never
  an unexplained new regression.
- **However**, Task 500's own gate should NOT be read as "0 confirmed bugs, 0 BLOCKED tasks" —
  that's a stricter bar than "Tasks 491-499 pass," and per Task 490's own "100%" checklist tier,
  genuinely reaching zero on both counts is what **100%** compatibility requires, not what these 9
  verification tasks alone establish. **This report's own conclusion: the *test-execution* gate
  (Tasks 491-499) is satisfied; the *substantive* 95%/100% bars from Task 490's checklist are not
  yet met** (5 confirmed bugs > the checklist's stated ceiling of "documented and shrinking," 5
  BLOCKED tasks remain open) — Task 500 should declare a milestone consistent with that distinction
  (e.g. a qualified "~90%, test-execution-verified" milestone per Task 490's own 90% bar, which
  *is* satisfied — every class rates ✅/⚠️ rather than ❌ on Present/Implemented per §2 above — rather
  than an unqualified "1.0"/"100%" claim the real numbers don't yet support).

## 6. Milestone declaration (Task 500, 2026-07-09)

**`Microsoft::Xna::Framework::Graphics` has reached a qualified ~90% XNA/FNA compatibility
milestone — test-execution-verified, not estimated.** This is a project-owner decision (made
directly, not inferred), matching Task 490's own release-checklist "~90%" tier rather than an
unqualified "1.0"/"100%" claim, which the real numbers in this report do not yet support.

**What "~90%, test-execution-verified" means concretely:**

- Tasks 491–499 (this entire Phase 55) all genuinely pass their own stated criteria. Every test
  failure encountered across all 4 renderers — EasyGL, Vulkan, Bgfx, SDL_Renderer, both
  integration-only and full-suite runs (§1 above) — is a previously-documented, understood issue.
  None is an unexplained new regression.
- Per §2 above, **100% of the 26 major Graphics classes rate ✅ on Present and ✅ on Tested**, and
  **0 of the 26 rate ❌ on Implemented** (22 fully ✅, 4 rated ⚠️ partial) — this is what "~90%" is
  measuring: the API surface is complete and exercised, not merely declared.
- The gap to 100% is exactly **10 named, tracked, non-silent items** — nothing is missing or
  undocumented:
  - **5 confirmed bugs, not yet fixed**: Task 921 (`IndexElementSize`'s numeric values don't match
    FNA — `16`/`32` vs. FNA's real `0`/`1`), Task 868 (Vulkan `BlendState` hardcodes one blend
    equation regardless of request), Task 918 (EasyGL `TextureFilter::Anisotropic` silently falls
    back to trilinear), Task 916 (`Model`'s non-default constructor auto-defaults `Root` to
    `bones[0]`, no way to specify otherwise), Task 922 (`SpriteBatch::Draw`'s 7th overload is
    `CNAEXT`-tagged with a **required** `Rectangle source`, where real FNA's equivalent overload
    takes an **optional** `Rectangle?`).
  - **5 BLOCKED tasks**, each needing a project-owner architecture decision before implementation
    (not just engineering time): Task 447 (Vulkan `OcclusionQuery` — the deferred-draw-recording
    architecture can't correlate a query's Begin/End span with a specific draw without a design
    decision), Tasks 686/687 (SDL_Renderer `TextureAddressMode::Wrap`/`Mirror` via `SpriteBatch` —
    no native support in the draw path used, 3 unpicked design options), Task 725 (SDL_Renderer
    `Texture3D`/`TextureCube` construction — touches 94 existing shared tests with zero renderer
    guards), Task 732 (EasyGL non-`Color` `SurfaceFormat` GPU forwarding — conflicts with the
    already-shipped `Texture::ValidateFormat` contract, Task 176).
- **Reaching 100%** requires closing all 10 of the above — 5 real (but individually small, already
  root-caused) bug fixes, plus 5 project-owner architecture decisions followed by their
  implementation. None of the 10 is silent, unknown, or newly discovered by this declaration; all
  were already tracked before this milestone was written.

This declaration lives here (the report with the real computed numbers backing it) and is
summarized in `README.md`'s "Project Status" section as the single external-facing entry point.
