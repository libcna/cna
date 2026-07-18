# Audit: examples/easygl_rendertargetcube_properties_test.cpp

## Metadata

- Source file: `examples/easygl_rendertargetcube_properties_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `RenderTargetCube` constructor/property black-box test (backend-agnostic
  per its own header comment)
- File type: C++ example/integration-test executable (`RenderTargetCubePropertiesTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::RenderTargetCube` (`RenderTargetCube.cpp`/`.hpp`),
  `Microsoft::Xna::Framework::Graphics::GraphicsDevice::SetRenderTarget(RenderTargetCube*, CubeMapFace)`
  (`GraphicsDevice.cpp:1861-1879`)
- XNA/FNA relevance: direct — `Width`/`Height`/`Format`/`DepthStencilFormat`/`RenderTargetUsage`/`MultiSampleCount`/
  `LevelCount`/`IsContentLost`/`ContentLost`/`GetTypeName` are all real XNA/.NET `RenderTargetCube` surface; judged
  against `FNA/src/Graphics/RenderTargetCube.cs`.
- Main related tests: this file (Task 332); sibling to `easygl_rendertarget2d_properties_test.cpp` (same rationale,
  applied to the cube variant); behavioral counterparts are
  `easygl_rendertargetcube_sample_test.cpp`/`easygl_rendertargetcube_depthformat_test.cpp`.

## Purpose

Pure black-box property/constructor audit for `RenderTargetCube`, mirroring
`easygl_rendertarget2d_properties_test.cpp`'s approach: constructs via each public constructor overload, asserts
every getter against FNA's documented/computed values, and validates the Task 336 (`LevelCount`) and Task 337
(`MultiSampleCount`) fixes. Uniquely among this batch, it also documents (in its own header comment, lines 18-27) an
architectural limitation it explicitly does **not** attempt to fix: `RenderTargetCube::Dispose(bool)` has no "still
bound" guard because `RenderTargetBinding` only stores `Texture*` and `RenderTargetCube` doesn't inherit `Texture`
(a Task 863 gap). Correctly placed per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — every property assertion checks out against both the CNA implementation and FNA's
`RenderTargetCube.cs`; the file's own architectural-limitation disclosure (Dispose's missing bound-guard,
`SetRenderTarget`'s missing binding-tracking) was independently verified accurate against the real
`GraphicsDevice.cpp`/`RenderTargetCube.cpp` source, not just asserted in a comment.

## Checklist Results

### API / XNA / FNA parity
Confirmed against `FNA/src/Graphics/RenderTargetCube.cs`: `DepthStencilFormat`, `MultiSampleCount`,
`RenderTargetUsage`, `IsContentLost` (hardcoded `false`, matching FNA's own `get { return false; }`), `ContentLost`
(declared, never raised, matching FNA's identical XNA4-compliance-only event) are all real FNA members with matching
semantics — same pattern as `RenderTarget2D`'s equivalent members, confirmed consistent between the two files' FNA
counterparts. `GetTypeName() == "Microsoft.Xna.Framework.Graphics.RenderTargetCube"` (lines 72-73) is a `NOXNA`
CNA-internal convenience (per `CLAUDE.md`'s own `GetTypeName()` requirement for `System::Object`-derived concrete
classes), correctly checked against `RenderTargetCube::GetTypeName()`'s actual literal
(`RenderTargetCube.cpp:68-72`) — confirmed to match exactly, and confirmed via the header comment (lines 19-20) and
independent code inspection that this was previously missing (inherited `TextureCube`'s string instead) before Task
332's fix.

### Behavioral correctness — verified against production code
- **`LevelCount == 1` (no mipMap)** (line 69) / **`LevelCount == 7` for size=64, mipMap=true** (lines 96-99):
  independently re-computed `RenderTargetCube.cpp`'s local `CalculateMipLevels(size)` (lines 14-20): `s=64`, loop
  halves with `std::max(1, s/2)` until `s<=1`: `64→32→16→8→4→2→1`, 6 halvings ⇒ `levels = 1+6 = 7` — matches the
  test's expectation exactly and matches the equivalent `RenderTarget2D` computation confirmed in that file's own
  audit.
- **`MultiSampleCount` default 0 / clamped for 4 / never-blind-passthrough for 9999** (lines 68, 105-119): identical
  structure and identical reasoning to `easygl_rendertarget2d_properties_test.cpp`'s equivalent assertions; traced
  `RenderTargetCube::RenderTargetCube` (`RenderTargetCube.cpp:38-61`) — `multiSampleCount_` is reassigned from
  `rtCubeBackend_->GetMultiSampleCount()` post-construction (lines 58-60), the exact same "ask the backend for the
  real, clamped value" pattern as `RenderTarget2D`. Confirmed `EasyGLRenderTargetCubeBackend::CreateResources()`
  (`EasyGLGraphicsBackend.cpp:772-779`) performs the identical `GL_MAX_SAMPLES` clamp as the 2D backend's
  `CreateResources`. The test's `v == 4 || v == 0` / power-of-two-or-zero validity checks are correctly designed for
  the same cross-backend-portability reason documented in the 2D sibling test.

### Logic
Six independently-scoped `{ }` blocks, same pattern as the 2D sibling — no state leakage between sub-tests.

### Memory/resource lifetime
Each `RenderTargetCube rt` is stack-local and scoped, destroyed before the next sub-test begins — no accumulation of
live GPU handles. `SetRenderTarget` is never called in this file, so the Dispose-bound-guard gap the header comment
discloses is genuinely out of scope for what this file itself exercises (correctly not attempted here).

### C++ correctness
No raw pointers, no casts, straight-line construction + getter calls only.

### Performance
N/A — one-shot property test.

### Thread safety
N/A.

### Architecture
The header comment's architectural disclosure (lines 21-27) was independently verified: confirmed
`GraphicsDevice::SetRenderTarget(RenderTargetCube*, CubeMapFace)` (`GraphicsDevice.cpp:1861-1879`) indeed calls
`currentRenderTargets_.clear()` (line 1870) but — unlike its `RenderTarget2D` sibling overload
(`GraphicsDevice.cpp:1821-1859`, which pushes a `RenderTargetBinding` at lines 1830-1832) — never pushes anything
back into `currentRenderTargets_` afterward, confirming the comment's claim that cube-face bindings are never
recorded in `GetRenderTargets()`. This is a real, correctly-diagnosed architectural gap (traced to
`RenderTargetBinding` only storing `Texture*`, and `RenderTargetCube` inheriting `TextureCube` rather than `Texture`
— confirmed via `RenderTargetCube.hpp:20`, `class RenderTargetCube : public TextureCube, public IRenderTarget`) —
accurately reported as a known, deliberate non-fix rather than silently ignored.

### Maintainability
Same clean structure as the 2D sibling test; the architectural-limitation comment is unusually transparent and
specific (naming the exact class relationship gap) rather than a vague "known issue" note.

### Portability
N/A for this file; `MultiSampleCount` assertions are deliberately cross-backend-portable (see above).

### Robustness
Same `check()`/pass-fail-counter pattern as the rest of the shard.

### Cross-file consistency
Directly parallels `easygl_rendertarget2d_properties_test.cpp` assertion-for-assertion (same constructor-overload
structure, same `LevelCount`/`MultiSampleCount` reasoning) — appropriate consistency for two sibling XNA types with
near-identical property surfaces. Complements `easygl_rendertargetcube_depthformat_test.cpp` (behavioral proof the
depth format this file only checks the *declared* value for actually gates rendering) and
`easygl_rendertargetcube_sample_test.cpp` (behavioral proof the render target is actually sampleable).

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — The disclosed `SetRenderTarget(RenderTargetCube*)` binding-tracking gap has no regression test anywhere in this shard

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage / architecture
- Location/symbol: `GraphicsDevice::SetRenderTarget(RenderTargetCube*, CubeMapFace)` (`GraphicsDevice.cpp:1861-1879`);
  `RenderTargetCube.cpp`'s lack of a `Dispose(bool)` override
- Evidence: this file's own header comment (lines 21-27) accurately discloses that (a) `RenderTargetCube` has no
  "still bound, cannot dispose" guard (unlike `RenderTarget2D::Dispose`, confirmed at `RenderTarget2D.cpp:84-93`),
  and (b) `GetRenderTargets()` never reflects a currently-bound cube face — but no test in this file or elsewhere in
  the shard actually exercises "dispose a `RenderTargetCube` while it is bound as the active render target" to
  confirm what actually happens (verified: nothing prevents it; the GPU resource is destroyed while
  `EasyGLGraphicsBackend`'s `currentRtCube_` may still point at the now-dangling backend object, since
  `SetRenderTarget(RenderTargetCube*)` never clears `currentRtCube_` on the disposed object's behalf).
- Why it matters: a caller that disposes a bound `RenderTargetCube` (a programming error XNA/FNA would normally
  reject via `InvalidOperationException` for `RenderTarget2D`, per `RenderTarget2D.cpp:90-92`) currently has no
  equivalent protection for the cube variant, and — because this is disclosed as a known limitation rather than
  fixed — remains a live use-after-free risk with no regression test proving or disproving the actual runtime
  consequence.
- FNA/XNA comparison: FNA's own `RenderTargetCube.cs` and `RenderTarget2D.cs` do not differ in this respect at the
  XNA-surface level (both are ordinary `IDisposable` XNA types); this is a CNA-internal architectural gap, not an
  FNA parity issue.
- Related files: `RenderTargetCube.cpp`/`.hpp`, `GraphicsDevice.cpp` (`SetRenderTarget` overloads),
  `RenderTargetBinding.hpp` (queued as a cross-cutting note for whichever shard audits those files directly).
  Note: this finding's root cause is in production code already disclosed by the test's own comment, but the
  **absence of a test proving/disproving the actual crash/UAF consequence** is itself a test-coverage gap worth
  flagging in this test file's own report per the audit checklist's "Testing" section.
- Suggested future action (not implemented by this audit): add a regression test (in this file or a new one) that
  disposes a bound `RenderTargetCube` and asserts either a thrown exception (if the guard is ever added, matching
  `RenderTarget2D`'s behavior) or documents the current actual behavior (no crash under ASan, or a real UAF) so the
  disclosed gap has a concrete, checked outcome rather than remaining purely descriptive.

## Cross-File Observations

- This file's disclosure of the `RenderTargetBinding`/`Texture*` architectural gap is the most specific and
  actionable of any comment in this batch — worth surfacing to whichever shard eventually audits
  `RenderTargetBinding.hpp`/`GraphicsDevice.cpp`'s `SetRenderTarget` overloads directly, since the gap's root cause
  lives in those files, not this one.

## Missing or Weak Tests

- See F1 — no test anywhere in this shard exercises dispose-while-bound for `RenderTargetCube`.
- No test asserts `Width`/`Height`/`Format` for the non-default constructor overloads (same minor gap noted in the
  2D sibling's audit).

## Positive Findings

- `GetTypeName()` fix (Task 332) independently confirmed correct — a real, previously-missing member now present and
  correctly asserted.
- The architectural-limitation disclosure in the header comment is unusually precise and was independently verified
  accurate down to the specific class-hierarchy reason (`RenderTargetBinding` storing `Texture*`,
  `RenderTargetCube` not inheriting `Texture`) — a genuinely valuable piece of documentation, not hand-waving.
- Consistent, correct reuse of the 2D sibling test's proven `LevelCount`/`MultiSampleCount` methodology.

## Final Assessment

A solid, accurate property test whose real value-add beyond its 2D sibling is the explicit, verified-correct
disclosure of a genuine architectural gap (F1) — one that would benefit from an actual regression test proving its
concrete runtime consequence rather than remaining a documented-but-unverified risk.
