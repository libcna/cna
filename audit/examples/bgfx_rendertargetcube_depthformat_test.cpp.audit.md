# Audit: examples/bgfx_rendertargetcube_depthformat_test.cpp

## Metadata

- Source file: `examples/bgfx_rendertargetcube_depthformat_test.cpp` (249 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RenderTargetCube` `DepthFormat` fidelity pixel test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_RenderTargetCube_DepthFormat`
  (`cmake/Tests/BgfxTests.cmake:512-515`) — **no** `CNA_BGFX_RENDERER=VULKAN` override (runs against
  Bgfx's default OpenGL renderer in this sandbox, unlike the sibling MSAA test above).
- XNA/FNA relevance: direct — `RenderTargetCube`'s `preferredDepthFormat` constructor parameter,
  `EnvironmentMapEffect` sampling of a `RenderTargetCube`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` —
  `BgfxRenderTargetCubeBackend` ctor (800-835), `BindAsRenderTargetFace()` (845-866, recreates the FBO
  fresh on every bind).
- Cross-referenced project docs: `plans/plan_graphics.md` Task 952 (two long entries, 2026-07-11), git log
  (`7c5862b8 investigate(Task 952): apitrace-based root-cause continuation, no fix yet`).

## Purpose

Verifies that a `RenderTargetCube`'s exact requested `DepthFormat` actually gates draws into a cube
face (near green quad at `z=0.2` should win over a far red quad at `z=0.8` when
`DepthStencilState::Default` is active and a real depth attachment is present). The file's own header
comment (lines 1-43) is not a normal "what this test does" summary — it is a **live, still-open bug
report**: `DepthFormat::None` correctly reads back green (proving the test's own updated
unbind-then-`EnvironmentMapEffect`-sample methodology, itself fixed for a Task 951-induced staleness,
is sound), but `DepthFormat::Depth24Stencil8` reads back **nothing at all** — not even the losing red
quad, just the sampling loop's own background clear colour — even with depth testing fully disabled
(`DepthStencilState::None`) and a single draw call, meaning the bug is not "the depth test wins for the
wrong quad," it is "the mere presence of a depth/stencil attachment on a cube face's framebuffer
silently blocks all colour output into that face."

## Executive Verdict

**Significant correctness risk** — this is not a test-authoring defect (the file's own methodology is
sound, confirmed by its `DepthFormat::None` control case) but a **currently-registered CTest target that
this audit independently confirms is very likely still failing** in this sandbox's default (non-Vulkan)
Bgfx renderer, per an extensively investigated, still-open, deliberately-deferred production bug
(Task 952), with no expected-failure/skip annotation in the CMake registration. See F1.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetCube(dev, kCubeSize, false, SurfaceFormat::Color, depthFormat)` (line 134) matches FNA's
4/5-arg constructor shape. `EnvironmentMapEffect`'s `EnvironmentMapAmount=1`/`EnvironmentMapSpecular=0`
combination (lines 161-162) correctly isolates a pure reflection sample with no specular contamination,
matching `IEffectLights`/`EnvironmentMapEffect`-specific properties' FNA semantics.

### Behavioral correctness
This audit independently re-derived the file's own diagnosis rather than trusting the comment at face
value:
- `BgfxRenderTargetCubeBackend`'s constructor (`BgfxGraphicsBackend.cpp:802-835`) creates the colour
  cube texture via `bgfx::createTextureCube(...)` and, when a depth format is requested, a **separate**
  2D depth texture shared across all 6 faces (`depthTex`, lines 826-832) — structurally identical in
  shape to `RenderTarget2D`'s own already-working depth attachment (which the Task 952 write-up in
  `plans/plan_graphics.md` also independently confirms via apitrace comparison).
- `BindAsRenderTargetFace()` (845-866) **recreates the framebuffer object from scratch on every single
  face bind** (`if (bgfx::isValid(fbo)) bgfx::destroy(fbo);` then a fresh `bgfx::createFrameBuffer(...)`)
  — `plans/plan_graphics.md`'s Task 952 entry independently flags this exact "FBO-recreation-per-bind pattern"
  as "the most plausible remaining CNA-side suspect, but NOT yet confirmed" after a RenderDoc capture
  ruled out FBO-handle validity, view-id targeting, and texture-handle identity as the cause.
- The git log confirms the investigation is real and recent: `7c5862b8 investigate(Task 952):
  apitrace-based root-cause continuation, no fix yet` sits directly on top of this file's own prior
  commits — this is not a stale comment describing an already-fixed issue, it is describing the
  current HEAD state.

### Logic
Check `(b)` (lines 224-227) asserts `matches(gotDepth, kGreen)` with `check()`. Given Task 952's own
documented finding that `Depth24Stencil8` reads back "the sampling loop's own background clear colour"
(`Color(10,10,10,255)` per this file's own `dev.Clear(Color(10,10,10,255))` at line 156), and
`matches()`'s tolerance is ±40 per channel (line 120-122) against `kGreen(0,255,0,255)` — `(10,10,10)`
is nowhere near `(0,255,0)` within a ±40 tolerance on the G channel, so the check would fail, not pass
by accidental tolerance overlap the way the sibling EasyGL specular test's check (b) does (see this
project's own prior audit example). This is a straightforward, unambiguous FAIL given the documented
current rendered value.

### Robustness
The file's own "DIAGNOSTIC v2" framing (lines 127-131) and the fact that `DepthFormat::None` is
deliberately left unasserted (`std::printf("[INFO] ... (not asserted, see file header)")`, lines
218-222) shows the test author was already aware this file's `check()` on the depth-attachment case is
expected to currently fail — this is an honest, transparent "regression trip-wire kept in place while
the bug is worked" pattern, not negligence. But see F1 for why leaving it wired into normal `ctest`
output without a skip/xfail marker is still a problem for anyone else running the suite.

### Testing
Only one of the file's two RenderAndReadFace() calls is actually asserted (`Depth24Stencil8`); `None` is
informational-only. This means the file currently contributes exactly one pass/fail signal to the test
suite, and per the analysis above that signal is very likely FAIL.

## Detailed Findings

### F1 — Registered CTest target for a known, extensively-investigated, currently-unresolved production bug has no expected-failure/skip annotation
- Severity: **HIGH**
- Confidence: MEDIUM-HIGH (not independently re-executed in this sandbox — no bgfx build tree or bgfx
  source checkout is present to build against, per `AUDIT_SCOPE.md`'s D-6 "bgfx is a genuine
  external/upstream dependency, reference-only" scoping decision — but corroborated by three
  independent, mutually-reinforcing pieces of primary evidence: (1) this file's own header comment
  giving an exact, specific description of the current failure mode; (2) `plans/plan_graphics.md`'s Task 952
  entries, which describe a real, tool-assisted (apitrace + RenderDoc) investigation spanning multiple
  sessions, explicitly marked `⬜` (open) and "DEFERRED (2026-07-11) — explicitly paused by the project
  owner... do not resume investigating this without explicit direction"; (3) `git log` confirming the
  most recent commit touching this file is itself an "investigate ... no fix yet" commit, not a "fix"
  commit)
- Category: correctness / test-suite-health / process
- Location/symbol: `check(matches(gotDepth, kGreen), ...)` (lines 224-227); CTest registration
  `Bgfx_RenderTargetCube_DepthFormat` (`cmake/Tests/BgfxTests.cmake:512-515`, no `CNA_BGFX_RENDERER`
  override, no `WILL_FAIL`/skip)
- Evidence: `plans/plan_graphics.md` row 952 states verbatim (abridged): *"DepthFormat::None correctly reads
  back green... while Depth24Stencil8 reads back nothing at all... Root cause still not found... commit-
  free scratch history, not present in the tree... DEFERRED — explicitly paused by the project owner
  after this round; resolve later, not this session."* This file's own `getResult()` returns
  `fail_ == 0 ? 0 : 1` (line 241), i.e. a nonzero process exit code, which is exactly what CTest treats
  as a test failure with no special-casing.
- Why it matters: a full `ctest` run in this environment (Bgfx's default OpenGL renderer, no
  `CNA_BGFX_RENDERER=VULKAN` override for this specific target) is very likely to report
  `Bgfx_RenderTargetCube_DepthFormat` as FAILED alongside any genuinely new regression, with nothing in
  the CTest output itself distinguishing "known, deferred, already-triaged Task 952 issue" from "a
  change I just made broke something new." Anyone running the suite without having independently read
  this file's header comment (or `plans/plan_graphics.md`) would reasonably treat this as a fresh regression
  to chase. The project's own established convention for a similar situation
  (`Bgfx_RenderTarget2D_MsaaResolve`, audited separately in this same batch) is to route the CTest
  environment through `CNA_BGFX_RENDERER=VULKAN` specifically because the OpenGL path is known-broken
  for an unrelated, different reason — this file does not receive the same treatment, and per the
  Task 952 write-up's own root-cause investigation, the bug is suspected to be bgfx/Mesa-driver-level
  (an FBO-recreation-per-bind pattern interacting with llvmpipe/RadeonSI), not proven to be
  renderer-specific the way the MSAA issue was — so it's plausible (not confirmed by this audit) that
  routing through Vulkan would not even fix it here.
- FNA/XNA comparison: N/A — this is a Bgfx-backend implementation defect, not an XNA/FNA API-shape or
  documented-behavior question. `RenderTargetCube` depth-tested rendering is unambiguously part of the
  XNA 4.0 surface (matches `RenderTarget2D`'s own already-correct depth behavior on this same backend),
  so the underlying capability gap is real and in-scope, even though this specific finding is about test
  suite hygiene rather than the production bug's root cause (which the project has already spent
  substantial, tool-assisted effort chasing and deliberately paused, per `NEXT.md`'s own "do not resume"
  framing referenced in the `plans/plan_graphics.md` entry).
- Suggested follow-up (not implemented by this audit, per the audit-only mandate): either (a) mark this
  CTest target `WILL_FAIL` / add a project-appropriate expected-failure convention referencing Task 952
  until the underlying bug is fixed, so a real regression elsewhere doesn't hide behind this always-red
  signal, or (b) resume the paused Task 952 investigation per the project owner's own stated "resume
  later" plan. Neither is this audit's call to make — flagged for whoever owns test-suite triage.

## Cross-File Observations

- Contrasts directly with `bgfx_rendertarget2d_msaa_test.cpp` (audited separately in this batch), which
  hit an analogous "known-broken on this sandbox's default OpenGL renderer" situation and was given an
  explicit `CNA_BGFX_RENDERER=VULKAN` CTest environment override to route around it — this file received
  no equivalent treatment, which this audit flags as an inconsistency in how the project handles
  "known-environment-limitation" vs. "known-unresolved-bug" CTest registrations (F1).
- `BgfxRenderTargetCubeBackend::BindAsRenderTargetFace()`'s FBO-recreate-per-bind pattern (also present,
  unmodified, for the mip and MSAA cube tests below) is only actually exercised end-to-end with a real
  depth attachment by this one file in the shard — `bgfx_rendertargetcube_mip_test.cpp` and
  `bgfx_rendertargetcube_msaa_test.cpp` both deliberately use `DepthFormat::None`, so neither of them
  would surface Task 952's bug even if it were present at those code paths too.

## Missing or Weak Tests

The file itself is not under-tested for its stated purpose — the real gap is the opposite: the test's
core assertion is documented as currently failing, and that fact is not surfaced anywhere a plain
`ctest` invocation would show it (see F1).

## Positive Findings

- The file's header comment is an exceptionally thorough, honest piece of engineering documentation —
  it names exactly what was ruled out (stencil presence, specific depth format, framebuffer
  completeness, an "orphaned view re-clear" theory that was investigated and explicitly retracted after
  controlled repro attempts), which is precisely the kind of transparency this audit's "verify claims,
  don't trust them" mandate is designed to reward once independently corroborated — and here it was.
- The `DepthFormat::None` control case is a genuinely good piece of test design: it isolates "is the
  updated sampling methodology itself sound" from "does the depth-attachment case work," so this file's
  own diagnosis of *where* the bug lives (attachment-presence, not methodology) is well-supported by its
  own code, not just asserted.

## Final Assessment

The file itself is a well-designed, well-documented differential test; the underlying production
capability it tests (depth-tested `RenderTargetCube` rendering on Bgfx) has a real, currently-open,
extensively investigated defect that the project has explicitly and deliberately paused work on. The
audit-relevant finding is procedural rather than about this file's own code quality: a CTest target
whose own header comment predicts a FAIL is registered with no distinguishing marker, which risks
masking a genuinely new regression behind an already-known one.
