# Audit: src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-headless` shard
- File type: C++ implementation (950 lines)
- Related header/implementation: `include/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp` (audited
  separately, same shard)
- XNA/FNA relevance: implements `IGraphicsBackend` for a backend whose entire purpose is running game logic
  without any GPU/window — no direct FNA equivalent (FNA has no headless mode), but its validation rules encode
  real XNA/CNA contract expectations (buffer capacity, SpriteBatch Begin/End pairing, sub-resource bounds).
- Graphics backend relevance: one of the 14 confirmed backends (per `AUDIT_GRAPHICS_BACKEND_MATRIX.md`); primarily
  a testing/CI tool, not a rendering target.
- FNA reference: N/A directly (no headless concept in FNA); validated conceptually against XNA's own documented
  argument-validation behavior for the methods it wraps (e.g. `SpriteBatch.Begin/End` pairing throws in real XNA
  too).
- Main related tests: exercised by any test using `CNA_HEADLESS_MODE`/the `Headless` backend build
  (`examples-tests-headless` shard, 7 files — not yet audited at time of writing, cross-check queued).

## Purpose

Implements every `HeadlessMode`-aware backend/resource class declared in the paired header: an in-memory-only
graphics backend that does real bookkeeping (resource-lifetime registry, cumulative/per-frame statistics, an
optional structured call trace) instead of touching a GPU, explicitly for fast/deterministic test runs and
behavior-drift detection between commits (`HeadlessTrace` mode + `CompareTraceLogs`/`FormatTraceLogDiff`).
Placement and design match `plans/plan_headless.md`'s stated intent well — this is a coherent, single-responsibility file
despite its size (950 lines), because nearly every one of its ~14 resource-backend classes is only a screenful of
constructor/destructor/validation boilerplate.

## Executive Verdict

**Mostly healthy**, with one genuine, concrete correctness bug in the statistics feature (F1 — instanced-draw
`primitiveCount` undercount) and one confirmed dead-code constructor (F2). Both are low real-world blast-radius
(this backend never renders anything a player sees; its only consumers are tests/tools that trust its counters),
but F1 in particular directly contradicts the stated purpose of `HeadlessStatistics` ("real bookkeeping... so a
test can assert on actual buffer contents... not just that SetData() was called") for exactly the one draw variant
(instancing) where getting the count right is least obvious.

## Checklist Results

### API / XNA / FNA parity
N/A (see Metadata) beyond argument-validation semantics, which are reasonable proxies for what real XNA/CNA
argument checks should look like (e.g. `SpriteBatch::Begin`/`End` pairing, buffer-capacity checks) — these read as
correct translations of the documented contract, not invented rules.

### Behavioral correctness
`Require()` (anonymous-namespace helper, lines 31-35) correctly gates every validation check behind
`state->ValidationEnabled()` (`mode != Fast`), matching the three-mode design in the header. Verified this gating
is applied consistently across every one of the ~25 `Require(...)` call sites in this file — none bypass it.
`ParseHeadlessModeFromEnvironment()` (lines 38-52) does a case-insensitive match against `"fast"/"trace"/
"validation"` and defaults unmatched/absent values to `Validation` — matches its own header doc.

`GetLastFrameStatistics()` (lines 832-858) computes a plain field-by-field diff against
`state_->statsAtLastPresent` (snapshotted in `Present()`, line 521-522) — correct and exhaustive (every
`HeadlessStatistics` field present in the header has a matching diff line; cross-checked field-by-field, no field
omitted).

### Logic
**F1 (see Detailed Findings)**: `DrawInstancedPrimitivesEx` (lines 819-830) corrects `drawCallCount` for the
instance count (`+= instanceCount - 1`, on top of the `+1` its `DrawIndexedPrimitivesEx→DrawIndexedColoredPrimitives`
delegate chain already added) but never corrects `primitiveCount` the same way — `primitiveCount` is only
incremented once (by the un-multiplied `primitiveCount` argument) regardless of `instanceCount`.

`PrimitiveVertexCount`/`PrimitiveIndexCount` (lines 13-29): switch over `PrimitiveType` with no `default:` case,
falling through to `return 0`. Cross-checked against `include/Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp`
— the enum has exactly the 5 values handled (`TriangleList=0, TriangleStrip=1, LineList=2, LineStrip=3,
PointListEXT=4`), so this is exhaustive *today* and not a live bug — but see F3 for the latent risk if a 6th value
is ever added (it would silently report "0 vertices needed," which makes every capacity `Require()` check
vacuously pass instead of failing loud, the opposite of what this backend exists to do).

### Memory/resource lifetime
Every resource-backend constructor registers with `state_->registry` and every destructor unregisters
(`HeadlessVertexBufferBackend`, `IndexBufferBackend`, `TextureBackend`, `RenderTargetBackend`,
`RenderTargetCubeBackend`, `TextureCubeBackend`, `Texture3DBackend`, `EffectBackend`, `SpriteBatchBackend`,
`OcclusionQueryBackend` — verified all 10 register-in-ctor/unregister-in-dtor pairs present). `state_` is a
`shared_ptr<HeadlessSharedState>` held by every resource, so a resource can outlive its owning
`HeadlessGraphicsBackend` safely (matches the header's own documented defensive rationale) — verified no resource
method dereferences anything backend-owned other than `state_`.

`HeadlessRenderTargetBackend`'s two-arg `HeadlessTextureBackend` constructor overload is unused (see F2).

### C++ correctness
`shadowData_.assign(reinterpret-as-uint8_t-range, ...)` pattern in both `VertexBufferBackend::SetData` (line
149-150) and `IndexBufferBackend::Upload` (line 189-190) correctly computes byte counts before constructing the
range (`vertex_count * stride_in_bytes`, `index_count * elementSize`) — no overflow guard, but `int` vertex/index
counts multiplied by small strides (16-32 bytes) are far below `size_t` overflow in any realistic test scenario;
not flagged as a practical risk. `HeadlessTextureBackend::UpdatePixels` (lines 225-238) computes
`pixels_.begin() + y * rowBytes` using a mix of `std::size_t` and an explicit `std::ptrdiff_t` cast for the
iterator-arithmetic term — correctly avoids the signed/unsigned iterator-arithmetic UB that a plain `size_t` offset
into a signed-diff-type `begin()+n` expression could otherwise risk.

### Performance
N/A / not a concern — this backend's entire purpose trades realism for the fastest possible non-rendering path;
nothing here is a hot-path regression relative to its design goal. `RecordTrace`'s string formatting
(`std::to_string` concatenation per call) only runs when `TraceEnabled()`, correctly gated (checked at each call
site indirectly via `RecordTrace`'s own early-return at line 56-57, so `Fast`/`Validation` modes never pay this
cost even though the call site always constructs the `argsSummary` string argument eagerly — see F4).

### Thread safety
`HeadlessResourceRegistry` is correctly mutex-guarded (`std::lock_guard<std::mutex>` in every one of `Register`/
`Unregister`/`AliveResources`/`AliveCount`/`Clear`). However, `HeadlessSharedState`'s other mutable fields —
`stats` (every `state_->stats.*++` in this file), `frameIndex`, `traceLog`, `debugLabelStack` — have **no**
synchronization at all. Consistent with `IGraphicsBackend.hpp`'s own `windowRegistry()` finding: not a live bug
under CNA's single-threaded game-loop model (no caller in this codebase drives a `HeadlessGraphicsBackend` from
multiple threads), but an inconsistency worth noting — the registry got a mutex and the (equally shared, equally
mutated) statistics/trace state didn't. `LOW` severity, `MEDIUM` confidence (no reproducing multi-thread caller
found).

### Architecture
Clean single-file backend, consistent with the other single-file adapters (EasyGL, Dx3, SdlRenderer, Software,
WebGPU) in this codebase's overall backend-maturity pattern (see `AUDIT_GRAPHICS_BACKEND_MATRIX.md`). No backend
abstraction leakage — this file never touches SDL, OpenGL, or any real graphics API, correctly matching its
"touches no GPU and no window at all" charter (header line 464).

### Maintainability
950 lines is proportionate — dominated by ~14 small, near-identical resource classes' ctor/dtor/validate
boilerplate, not by any one overgrown function. No `TODO`/`FIXME`/stub markers found. Two genuine findings below
(F1, F2) are the only real issues.

### Portability
N/A — no platform-specific code.

### Robustness
`ReadBackbuffer` (lines 547-562) validates `w >= 0 && h >= 0` but never checks `pixels != nullptr` before writing
`w*h*4` bytes into it — see F5. Every other data-receiving method in this file (`UpdatePixels`,
`UpdatePixelsLevel`) does check its pointer argument for null via `Require`, making this one omission a real
(if minor) inconsistency in an otherwise consistently-defensive file.

### Testing
Not independently assessed in this report (queued for `examples-tests-headless`, 7 files) — but note that F1
(the `primitiveCount` instancing undercount) is exactly the kind of defect a statistics-based test *should* catch
if one exists that calls `DrawInstancedPrimitivesEx` and asserts `GetStatistics().primitiveCount`; flagging this
finding for cross-reference when that shard is audited.

## Detailed Findings

### F1 — `HeadlessStatistics::primitiveCount` undercounts instanced draws by a factor of `instanceCount`

- Severity: MEDIUM
- Confidence: HIGH
- Category: correctness (logic)
- Location/symbol: `HeadlessGraphicsBackend::DrawInstancedPrimitivesEx` (lines 819-830),
  `DrawIndexedColoredPrimitives` (lines 770-785)
- Evidence: `DrawInstancedPrimitivesEx` delegates to `DrawIndexedPrimitivesEx` → `DrawIndexedColoredPrimitives`,
  which does `state_->stats.primitiveCount += primitiveCount;` (line 783) exactly once. Back in
  `DrawInstancedPrimitivesEx`, only `drawCallCount` is corrected afterward (`+= instanceCount - 1`, line 829) — no
  equivalent `primitiveCount += primitiveCount * (instanceCount - 1)` (or equivalent) exists. So a single
  `DrawInstancedPrimitivesEx(..., primitiveCount=10, instanceCount=100, ...)` call records `drawCallCount += 100`
  (correct — matches "100 draw calls' worth of GPU work") but `primitiveCount += 10` (wrong — should be `1000`,
  since 100 instances each draw 10 primitives).
- Why it matters: `GetStatistics()`/`GetLastFrameStatistics()` are the backend's primary test-facing API
  (`plans/plan_headless.md Phase N4`) — a test asserting "did my instanced-rendering code draw the expected total
  primitive count" would get a number `instanceCount`× too small, for every instanced draw in the codebase (used
  wherever `DrawInstancedPrimitivesEx` appears, e.g. instancing tests in the Bgfx/Vulkan/D3D backends that might
  reuse Headless for a fast pre-check, or any Headless-mode test of instancing itself).
- FNA/XNA comparison: N/A (CNA-internal diagnostic feature, no FNA equivalent).
- Related files: `include/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp` (`HeadlessStatistics`
  struct definition); any `examples-tests-headless` or backend instancing test that reads `primitiveCount` after
  an instanced draw (to be cross-checked when that shard is audited).
- Suggested future action (not implemented by this audit): multiply by `instanceCount` when incrementing
  `primitiveCount` inside `DrawInstancedPrimitivesEx`, mirroring the existing `drawCallCount` correction pattern.

### F2 — `HeadlessTextureBackend`'s `(state, width, height, typeNameOverride)` constructor has zero callers

- Severity: LOW
- Confidence: HIGH
- Category: maintainability (dead code)
- Location/symbol: `HeadlessTextureBackend::HeadlessTextureBackend(state, int width, int height, std::string
  typeNameOverride)` (lines 212-218)
- Evidence: repository-wide grep for `HeadlessTextureBackend(` across all tracked `.cpp`/`.hpp` files found only
  the declaration, its own definition, and the *other* (ImageData) constructor's use at line 566
  (`CreateTexture`) — no call site anywhere constructs a `HeadlessTextureBackend` via the 4-argument overload.
- Why it matters: dead code in an audit-scope file — harmless today, but it also skips the
  `state_->stats.texturesCreated++` bump the ImageData constructor does (line 207 vs. no equivalent at line
  212-218), so if this constructor is ever wired up later (e.g. for a render-target-backed texture path) it would
  silently under-report `texturesCreated` too, compounding with a similar-shaped bug to F1.
- FNA/XNA comparison: N/A.
- Related files: none currently reference it.
- Suggested future action (not implemented by this audit): either remove the unused overload, or — if it's
  reserved for a planned call site — add the missing `texturesCreated++` now while it's easy to see the omission,
  and add a one-line comment explaining its intended future caller.

### F3 — `PrimitiveVertexCount`/`PrimitiveIndexCount` silently return 0 for any unhandled `PrimitiveType`

- Severity: LOW (latent — not exploitable today, see Behavioral correctness above for the exhaustiveness check)
- Confidence: HIGH
- Category: robustness
- Location/symbol: lines 13-29
- Evidence: no `default:` branch; falls through the switch to `return 0` for any value not in the 5 handled cases.
- Why it matters: this is exactly backwards for a *validation* backend — the whole point of `Require()` gating is
  to fail loud on misuse, but an unrecognized `PrimitiveType` would make `neededVertices`/`neededIndices` compute
  as `0`, which trivially satisfies every capacity check (`0 <= vb.GetVertexCount()` is always true), silently
  *passing* validation instead of throwing on the actually-more-suspicious "unknown primitive type" case.
- FNA/XNA comparison: N/A.
- Related files: `include/Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp` (confirmed only 5 values exist
  today).
- Suggested future action (not implemented by this audit): add a `default:` that throws (or asserts) rather than
  returning 0, so a future `PrimitiveType` addition fails loudly here instead of silently defeating validation.

### F4 — `RecordTrace` call sites always eagerly build their `argsSummary` string, even outside Trace mode

- Severity: LOW
- Confidence: HIGH
- Category: performance (minor)
- Location/symbol: essentially every `state_->RecordTrace("...", "..." + std::to_string(...))` call site in this
  file (e.g. lines 131, 151, 168-169, 209, 257, …)
- Evidence: `RecordTrace` itself early-returns when `!TraceEnabled()` (line 56-57), but the `argsSummary` argument
  — often a `std::to_string`-heavy concatenation — is evaluated by the *caller* before the call, unconditionally,
  in every mode including `Fast`.
- Why it matters: purely a `Fast`-mode performance nit (the whole point of `Fast` mode is "the minimum bookkeeping
  ... skips validation entirely, for test runs that just need the game loop to execute quickly," per the header's
  own doc comment) — every draw/state-change call still pays for string formatting it immediately discards. Likely
  negligible in absolute terms but works directly against `Fast` mode's stated purpose.
- FNA/XNA comparison: N/A.
- Related files: none beyond this file.
- Suggested future action (not implemented by this audit): could defer string construction behind a lambda/macro
  gated on `TraceEnabled()`, if `Fast`-mode throughput is ever actually measured to matter.

### F5 — `ReadBackbuffer` doesn't validate `pixels != nullptr` before writing through it

- Severity: LOW
- Confidence: HIGH
- Category: robustness
- Location/symbol: `HeadlessGraphicsBackend::ReadBackbuffer` (lines 547-562)
- Evidence: `Require(state_, w >= 0 && h >= 0, ...)` is the only validation; the subsequent loop
  (`pixels[i*4+0] = r; ...`) unconditionally dereferences `pixels` for `w*h` iterations with no null check, unlike
  every other data-writing method in this file (`UpdatePixels`/`UpdatePixelsLevel` both `Require(rgba != nullptr,
  ...)` first).
- Why it matters: a null `pixels` argument would segfault instead of throwing the clean
  `HeadlessValidationException` this backend otherwise consistently provides for caller mistakes — inconsistent
  with the file's own established defensive pattern, though not a currently-observed live bug (no caller in this
  codebase passes a null buffer here, based on the call sites checked).
- FNA/XNA comparison: N/A.
- Related files: none beyond this file.
- Suggested future action (not implemented by this audit): add `Require(state_, pixels != nullptr, ...)` for
  consistency with the rest of the file.

## Cross-File Observations

- F1's fix pattern should be double-checked against how *other* backends (EasyGL, Vulkan, Bgfx, D3D9/11/12,
  SdlGpu — all of which implement real instancing) account for primitive counts in whatever equivalent statistics/
  profiling counters they expose, to see if this is a Headless-only oversight or a copy-pasted pattern.
- The `HeadlessTrace`/`CompareTraceLogs`/`FormatTraceLogDiff` behavior-drift-detection feature (HEADLESS-43) is a
  genuinely distinctive piece of NOXNA tooling not seen in any other backend audited so far — worth highlighting
  in `AUDIT_CROSS_CUTTING_FINDINGS.md` as a positive architectural pattern other backends' test tooling could learn
  from, not just a Headless-specific detail.

## Missing or Weak Tests

Cross-referenced against `examples-tests-headless` (7 files) queued for its own audit — in particular, no evidence
yet either way of a test that would have caught F1 (an instanced-draw statistics assertion). Flagging this as a
concrete "missing test" candidate regardless of what that shard's audit finds, since F1 shows the gap is real.

## Positive Findings

- The three-mode (`Fast`/`Validation`/`Trace`) design is executed consistently and correctly throughout — every
  validation call is properly gated, and the mode's own documented cost/benefit tradeoff (header comments) matches
  what the code actually does.
- Resource-lifetime bookkeeping (registration/unregistration, leak detection via `AssertNoLeaks`) is complete and
  correct for every resource type actually exercised via the public factory methods.
- `CompareTraceLogs`/`FormatTraceLogDiff` are clean, small, well-tested-looking utility functions with sensible
  edge-case handling (prefix-length mismatch, identical-logs fast path).

## Final Assessment

A well-built, single-purpose testing backend with one concrete, fixable statistics bug (F1) and a small handful of
low-severity consistency gaps (F2-F5). None of these affect real rendering (there is none), but F1 specifically
undermines the file's own stated purpose of being a trustworthy source of ground-truth counters for tests.
