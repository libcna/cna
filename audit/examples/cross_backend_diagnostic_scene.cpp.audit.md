# Audit: examples/cross_backend_diagnostic_scene.cpp

## Metadata

- Source file: `examples/cross_backend_diagnostic_scene.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — cross-backend diagnostic dump producer
  (`plans/plan_software.md` SOFTWARE-61/SOFTWARE-84).
- File type: `Game`-subclass executable, built per-backend, **not** CTest-registered anywhere.
  Confirmed registrations (both explicitly commented "not registered as a ctest"):
  `cmake/Tests/SoftwareTests.cmake:45` (`cna_diag_software`) and
  `cmake/Tests/EasyGLTests.cmake:38` (`cna_diag_easygl`). Grepped all of `cmake/Tests/*.cmake` for
  `cross_backend_diagnostic_scene` — **no** Vulkan, Bgfx, D3D9/11/12, SdlGpu, WebGPU, Dx3, Canvas,
  Ascii, or Headless registration exists, despite the file's own header comment claiming "built once
  per backend" in general terms.
- XNA/FNA relevance: exercises real public XNA API only (`Game`, `GraphicsDeviceManager`,
  `BasicEffect`, `VertexBuffer`, `VertexPositionColor`, `GraphicsDevice::DrawPrimitives`/
  `GetBackBufferData`) — no `NOXNA` surface beyond `Game::Run()`/`Exit()`'s own CNA lifecycle.
- FNA reference: `BasicEffect`'s vertex-color-only (unlit) rendering path — no FNA file directly
  relevant since this is a CNA-authored diagnostic scene, not a ported `.cs` file.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`GetBackBufferData`), and whichever
  backend is compiled in (`SOFTWARE`/`EasyGL` today).

## Purpose

Renders one intentionally minimal, fully unlit (`VertexColorEnabled=true`, no lighting) triangle —
three vertices at `(0,0.9,0.5)`/Red, `(-0.9,-0.9,0.5)`/pure-Green, `(0.9,-0.9,0.5)`/Blue — with
`RasterizerState::CullNone`, reads back the full 64×64 backbuffer via `GetBackBufferData`, and
writes it byte-for-byte (`R,G,B,A` per pixel via `Color::getRProperty()`..`getAProperty()`, not
`Color`'s internal packed representation) to a raw file named by `argv[1]`. The header comment is
explicit about the design intent: "deliberately backend-agnostic (no `#ifdef`, no backend-specific
include)... so the SAME source produces one dump per backend, compared afterwards by
`cross_backend_diagnostic_compare`." This was independently verified — the file has zero `#ifdef`/
backend-specific includes, confirming the "backend-agnostic source" claim is accurate for the file
itself (though see F1 for the gap between "backend-agnostic source" and "actually built for every
backend"). Placement under `examples/` (not `tools/`) matches the sibling comparator's placement.

The header comment's `CullNone` justification is independently verifiable: it states the triangle's
"screen-space winding happens to be counter-clockwise-as-displayed, which the real default
`RasterizerState.CullCounterClockwise` would cull" — this matches the exact winding-order
justification used by numerous other example tests in this codebase (e.g.
`dualtextureeffect_vertexcolor_test.cpp`'s "Task 896 finding" comment, audited separately), a
recurring, correctly-diagnosed CCW/back-face culling gotcha across this whole `examples/` tree.

## Executive Verdict

**Mostly healthy** — the rendering/dump logic itself is correct and genuinely backend-agnostic as
claimed, but the file's own framing ("built once per backend... so the SAME source produces one
dump per backend") signficantly overstates actual current backend coverage: only 2 of the project's
~11 graphics backends (`SOFTWARE`, `EasyGL`) currently build this diagnostic at all, so its practical
cross-backend value today is limited to an EasyGL-vs-Software comparison, not the general
"any-backend-vs-any-backend" tool the framing implies.

## Checklist Results

### API / XNA / FNA parity
Uses real XNA-compatible surface throughout: `BasicEffect::VertexColorEnabled` (see F2 for the
known bare-public-field API design issue, not a defect of this file), `GraphicsDevice::Clear`,
`SetVertexBuffer`, `DrawPrimitives(PrimitiveType::TriangleList, 0, 1)` (1 primitive = correct
per-triangle count, not vertex count — this codebase has a documented recurring mistake of passing
vertex count instead of primitive count per `MEMORY.md`'s
`feedback_drawprimitives_primitivecount_not_vertexcount`; this file gets it right: 3 vertices, 1
triangle, `1` passed). `GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()))`
matches the real `GraphicsDevice.GetBackBufferData(Rectangle?, T[], int, int)` FNA overload shape.

### Behavioral correctness
- The vertex list (lines 61-65) is exactly 3 vertices for exactly 1 triangle, matching the
  `DrawPrimitives` call's primitive count of `1`.
- `dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()))` where
  `region = Rectangle(0,0,kSize,kSize)` and `pixels.size() == kSize*kSize` — correct full-backbuffer
  readback with `elementCount` matching the requested region's pixel count exactly.
- File-write failure (`std::fopen` returning `nullptr`) is handled: prints to `stderr` and calls
  `Exit()` with `result_` left at its already-initialized failure value (`1`), rather than crashing
  on a null `FILE*` dereference — a real, if minor, robustness check most similarly-shaped example
  files skip.
- `result_` starts at `1` (fail) and is only set to `0` after a successful `fwrite` — correct
  fail-safe default (a crash or early-return before reaching the success line leaves the process
  reporting failure, not a false-positive pass).

### Logic
Single-frame `Draw()` — no state machine needed since this scene is deliberately as simple as
possible. `Exit()` is called unconditionally at the end of `Draw()`'s only meaningful path, ensuring
the process terminates after exactly one frame regardless of pass/fail.

### C++ correctness
- `std::vector<Color> pixels(...)` sized as `static_cast<std::size_t>(kSize) * kSize` — correct
  `int*int` to `size_t` widening order (casts the first operand before multiplying, avoiding
  `int*int` overflow at this trivial 64×64 size, though the cast wouldn't have mattered numerically
  here — good habit regardless).
- `std::vector<std::uint8_t> rgba(pixels.size() * 4u)` then indexed via `i*4+0..3` for
  `i` in `[0, pixels.size())` — bounds are correct (`rgba.size() == pixels.size()*4`, and the loop's
  max index is `(pixels.size()-1)*4+3 == rgba.size()-1`).
- `getRProperty()`/`getGProperty()`/`getBProperty()`/`getAProperty()` used consistently rather than
  reading `Color`'s packed in-memory layout directly — correctly future-proofs the dump format
  against any change to `Color`'s internal packing (e.g. the AABBGGRR layout CLAUDE.md documents),
  exactly as the file's own comment states.

### Memory/resource lifetime
`gdm_` is a `std::unique_ptr<GraphicsDeviceManager>` constructed in the constructor body — standard
ownership pattern matching this project's other example files. `FILE*` is closed
(`std::fclose(f)`) after the single `fwrite` on the success path; the failure path (fopen returning
null) has no handle to close. No leaks identified.

### Performance
N/A/trivial — single-frame diagnostic tool, 64×64 backbuffer, no hot path concerns.

### Thread safety
N/A — single-threaded `Game` harness, standard for this whole example tree.

### Architecture
Correctly backend-agnostic at the *source* level (confirmed: no `#ifdef`, no backend-specific
includes) — the architectural mechanism that actually selects which backend's `cna_diag_*` binary
gets built is entirely in CMake (`CNA_GRAPHICS_BACKEND`), which is the right layer for that decision
and matches this project's established `IGraphicsBackend` abstraction-boundary discipline. See F1
for the gap between this correct architecture and the currently narrow set of backends that actually
wire it up.

### Maintainability
Small (123 lines), single-responsibility, comment-to-code ratio appropriately high for a diagnostic
tool whose whole value depends on later readers understanding exactly what scene it renders and why
`CullNone` is needed. No dead code, no magic numbers beyond the documented `kSize=64`.

### Portability
No platform-specific code. `std::fopen(..., "wb")` — binary mode explicit, matching the comparator's
own binary-mode read.

### Robustness
See Behavioral correctness above — file-open failure is the one plausible runtime failure mode and
it is handled explicitly rather than left to crash.

### Testing
This file is not itself a pass/fail CTest (by design, per both of its own CMake registration
comments: "not registered as a ctest... just a plain executable a developer/script runs by hand").
Its correctness is therefore only ever exercised manually or by whatever external script invokes the
3-step `docs/software-backend.md` workflow — there is no CI job (that this audit could find) that
actually runs that 3-step workflow automatically, meaning a regression in either half of this
producer/comparator pair could go unnoticed indefinitely. This mirrors, at a smaller scope, the
"CI-masking risk" pattern documented in `AUDIT_CROSS_CUTTING_FINDINGS.md` for other never-exercised
test registrations — worth flagging for the same reason: an intentionally-manual diagnostic with zero
automated invocation is a gap in the test *process*, not a code defect.

### Cross-file consistency
Verified byte-layout agreement with `cross_backend_diagnostic_compare.cpp` (see that file's audit).
`docs/software-backend.md`'s "Cross-backend diagnostic" section was independently read and its
description of this file ("renders one simple, fully unlit... triangle and dumps the resulting 64x64
RGBA8 backbuffer... deliberately backend-agnostic (no `#ifdef`) and is built once per backend that
needs it") is accurate and appropriately hedged ("that needs it", not "every backend") — the docs
file is more careful in its phrasing than this source file's own header comment, which reads more
like "built once per backend" unconditionally.

## Detailed Findings

### F1 — Header comment's "built once per backend" framing overstates actual backend coverage (2 of ~11 backends)

- Severity: LOW
- Confidence: HIGH (directly grepped every `cmake/Tests/*.cmake` file for
  `cross_backend_diagnostic_scene` and found registrations only in `SoftwareTests.cmake` and
  `EasyGLTests.cmake`)
- Category: documentation-accuracy
- Location/symbol: header comment lines 2-6 ("built once per backend (see CMakeLists.txt's SOFTWARE
  and EASYGL sections)") vs. line 6's own more precise parenthetical naming only those two sections
- Evidence: the header comment's own parenthetical *does* correctly scope this to "SOFTWARE and
  EASYGL sections" — so this is a mild internal tension within the same comment (the lead sentence
  reads as a general "every backend" claim, while the parenthetical is accurate) rather than an
  outright false statement. No Vulkan/Bgfx/D3D9/D3D11/D3D12/SdlGpu/WebGPU/Dx3/Canvas/Ascii/Headless
  registration of this file exists anywhere in the tree.
- Why it matters: a future reader skimming only the lead sentence ("so the SAME source produces one
  dump per backend") could reasonably expect to find (or add without checking first) a Vulkan/Bgfx
  registration and be surprised none exists — low-impact since the parenthetical right there in the
  same comment block does correctly disambiguate, and `docs/software-backend.md`'s own description
  is accurately hedged.
- FNA/XNA comparison: N/A — tooling/documentation issue, not an XNA behavior question.
- Related files: `cmake/Tests/SoftwareTests.cmake:45`, `cmake/Tests/EasyGLTests.cmake:38`,
  `docs/software-backend.md`'s "Cross-backend diagnostic" section (which is accurately hedged).
- Suggested future action (not implemented by this audit): either extend the CMake registration to
  Vulkan/Bgfx (making the lead sentence true), or soften the lead sentence to match the
  parenthetical's already-accurate scoping.

### F2 — Uses `BasicEffect::VertexColorEnabled` as a bare public field (line 69), the known project-wide API-design lapse

- Severity: LOW (usage of an existing defect, not a new one introduced by this file)
- Confidence: HIGH
- Category: API-design consistency
- Location/symbol: `fx.VertexColorEnabled = true;` (line 69)
- Evidence: `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents that `BasicEffect::VertexColorEnabled`
  is "a bare public field with no `getXProperty()`/`setXProperty()` wrapper at all, unlike every
  other property on the class — a direct violation of this project's own explicit C# property
  convention." This file's line 69 is simply a caller of that pre-existing production API shape, not
  an independent defect — included here only as an additional confirmed usage site for that
  already-tracked cross-cutting finding, extending its confirmed occurrence count beyond the two
  `bgfx`/`vulkan` test files already cited there.
- Why it matters: same rationale as the cross-cutting entry — this is the one property on
  `BasicEffect` that cannot be renamed/wrapped without breaking every caller (including this file),
  which is itself a small piece of evidence for why the underlying `BasicEffect.hpp`/`.cpp` fix
  (adding `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` while keeping — or
  deliberately breaking, per CLAUDE.md's "No Backward Compatibility Hacks" rule — the bare field) is
  a nontrivial, multi-call-site change.
- FNA/XNA comparison: N/A (this is a CNA-internal C++ API-shape observation about a property that
  in real C# XNA is `public bool VertexColorEnabled { get; set; }` — the C# property syntax itself
  is what CLAUDE.md's `getXProperty()`/`setXProperty()` convention is supposed to translate).
- Related files: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`.
- Suggested future action: none from this file's own audit — tracked centrally in
  `AUDIT_CROSS_CUTTING_FINDINGS.md`; fixing `BasicEffect` itself is out of this file's scope.

## Cross-File Observations

- This file and its comparator (`cross_backend_diagnostic_compare.cpp`) form a genuinely
  well-designed pair at the source level — see that file's own audit for confirmation their byte
  layouts agree. The gap identified here (F1) is entirely a CMake-registration-breadth issue, not a
  flaw in either source file.
- Shares the exact `CullNone`/CCW-winding workaround comment pattern with
  `dualtextureeffect_vertexcolor_test.cpp`, `environmentmapeffect_alphascaledlerp_test.cpp`, and
  `graphicsdevice_clear_depth_test.cpp` (all four files in this same audit batch cite the same
  underlying "Task 896" default-`RasterizerState`/CCW-culling finding) — a genuinely consistent,
  correctly-diagnosed idiom across this whole example tree, not a one-off workaround.

## Missing or Weak Tests

- No automated CI step (found by this audit) actually runs the documented 3-command cross-backend
  workflow (build SOFTWARE, build EASYGL, diff) — the entire diagnostic pair is manual-only today,
  meaning a regression in either the dump format or the comparator's diff logic would not be caught
  by ordinary `ctest` runs. This is a process gap, not a code defect in this file.

## Positive Findings

- Genuinely backend-agnostic source (verified, not just claimed) — no `#ifdef`, no backend-specific
  include.
- Explicit, checked file-write error path (`fopen` failure handled, not left to crash).
- Correct `DrawPrimitives` primitive-count usage (`1`, not `3`) — gets right a mistake this exact
  codebase's own memory log documents as a recurring bug elsewhere.
- Dump format explicitly decoupled from `Color`'s internal packed representation via property
  getters, future-proofing the format against internal layout changes.

## Final Assessment

A correct, well-reasoned diagnostic scene whose only real weakness is a documentation/coverage gap
(F1) between its "backend-agnostic, built once per backend" framing and the current reality of only
2 backends actually wiring it up — plus the pre-existing, already-tracked `BasicEffect` bare-field
API-design issue (F2) that this file merely uses rather than introduces. No behavioral defects found.
