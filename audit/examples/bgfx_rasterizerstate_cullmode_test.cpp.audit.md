# Audit: examples/bgfx_rasterizerstate_cullmode_test.cpp

## Metadata

- Source file: `examples/bgfx_rasterizerstate_cullmode_test.cpp` (191 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RasterizerState.CullMode` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_bgfx_rasterizerstate_cullmode`
  / `Bgfx_RasterizerState_CullMode`, `cmake/Tests/BgfxTests.cmake:676-679`)
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.CullMode`,
  `RasterizerState.CullMode` (default `CullCounterClockwiseFace` per FNA's `RasterizerState.cs`).
- Related production code: `include/Microsoft/Xna/Framework/Graphics/CullMode.hpp` (values match FNA's
  `CullMode.cs` exactly: `None`, `CullClockwiseFace`, `CullCounterClockwiseFace`),
  `src/Microsoft/Xna/Framework/Graphics/RasterizerState.cpp` (default ctor),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplyRasterizerState()` lines 1773-1797, `DrawColoredPrimitives()` lines 2274-2317).

## Purpose

6-check test (Task 765) proving `CullMode::None` disables culling entirely and that
`CullClockwiseFace`/`CullCounterClockwiseFace` each cull the *opposite* winding they're named after — a
Bgfx port of `easygl_rasterizerstate_cullmode_test.cpp` (Tasks 323-325), restructured into one
`Clear+Draw+Read` pass per `(CullMode, winding)` pair because Bgfx's `GetBackBufferData` only reliably
reflects the first read per rendered frame (Task 406, applied consistently across this shard). The
file's own header comment explicitly calls out the key test-design principle: a same-expected-outcome
test family cannot distinguish "culling works" from "culling is bypassed entirely" — this test redraws
the *same two windings* under all 3 `CullMode` values so each winding's own visibility flips depending on
the mode, which a naive always-on/always-off implementation could not satisfy across all 6 checks.

## Executive Verdict

**Healthy.** Independently re-derived the signed-area/winding direction of both `DrawQuadCW`/`DrawQuadCCW`
and confirmed the production `ApplyRasterizerState()` mapping (`CullClockwiseFace→BGFX_STATE_CULL_CW`,
`CullCounterClockwiseFace→BGFX_STATE_CULL_CCW`) is a direct, correct 1:1 translation of FNA's `CullMode`
enum semantics, and that `cullFlags_` is consistently wired into the exact draw path
(`DrawColoredPrimitives`, invoked via `GraphicsDevice::DrawUserPrimitives`) this test uses.

## Checklist Results

### API / XNA / FNA parity
`CullMode::None=0`, `CullClockwiseFace=1`, `CullCounterClockwiseFace=2` match FNA's `CullMode.cs` exactly
(confirmed by reading `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/CullMode.cs`: "Cull faces
with clockwise order" / "Cull faces with counter clockwise order"). `RasterizerState`'s default ctor
(`src/Microsoft/Xna/Framework/Graphics/RasterizerState.cpp:11`) sets `cullMode_ = CullCounterClockwiseFace`,
matching FNA's documented default.

### Behavioral correctness
Verified `DrawQuadCW`'s vertex order (`(1,-1),(-1,-1),(-1,1)` then `(1,1),(1,-1),(-1,1)`) has negative
signed area (shoelace formula) in NDC, i.e. clockwise winding, and `DrawQuadCCW`'s is the mirror
(positive area, counter-clockwise) — matches the file's own comment and is geometrically correct.
Traced `ApplyRasterizerState()`'s switch (`case 1→BGFX_STATE_CULL_CW`, `case 2→BGFX_STATE_CULL_CCW`,
`default→0`) — this is the correct direct mapping given bgfx's own `BGFX_STATE_CULL_CW`/`_CCW` cull the
winding named by the flag. The 6 expected outcomes in the `checks[]` table (None: both visible;
CullCounterClockwiseFace (default): CW survives, CCW culled; CullClockwiseFace: CW culled, CCW survives)
are internally consistent with this mapping and with FNA's documented default behavior.

### Logic
`RunCheck()` correctly re-applies a *fresh* `RasterizerState` object per call (`RasterizerState rs;
rs.setCullModeProperty(mode);`) rather than mutating a shared instance, avoiding a stale-state class of
bug this audit has seen elsewhere in the project (aliased mutable default `RasterizerState` /
`DepthStencilState` singletons).

### C++ correctness
`DrawColoredPrimitives()` (the code path `DrawUserPrimitives` routes through,
`GraphicsDevice.cpp:692-747`) folds `blendFlags_ | depthFlags_ | cullFlags_` into a single
`bgfx::setState()` call per draw — confirmed `cullFlags_` is genuinely read fresh each draw (not cached
stale from a previous test's `RasterizerState`), since `ApplyRasterizerState()` is called synchronously
from `GraphicsDevice::setRasterizerStateProperty()` immediately before each check's draw.

### Testing
The differential design (same geometry, 3 different expected outcomes across the 3 modes) is genuinely
strong: it defeats both "culling always off" and "culling always on for winding X" failure modes, unlike
a test that only ever exercises one `CullMode` value. This audit confirms the specific expected/actual
pairing is correct, not merely self-consistent.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM defects found.

## Cross-File Observations

- `cullFlags_` is the same backend member consumed by `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` (the
  BasicEffect/PbrEffect-driven draw paths exercised by `bgfx_pbreffect_test.cpp` and others in this
  shard) — a regression isolated to only one of the two draw-path families (`DrawColoredPrimitives` vs.
  the `Ex` overloads) would not necessarily be caught by this file alone, since it only exercises the
  colored/no-effect path. Low risk in practice since `cullFlags_` is a single shared field set from one
  place (`ApplyRasterizerState`), not duplicated logic.
- Companion cull-mode coverage exists at `examples/rasterizerstate_cullmode_camera_test.cpp` and
  `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp` (both registered in
  `cmake/Tests/BgfxTests.cmake:685-693`, outside this shard's file list) which extend coverage to a real
  camera transform and an indexed BasicEffect path respectively.

## Missing or Weak Tests

- This file alone does not exercise `CullMode` through an indexed draw or through an `Effect`-driven
  (`DrawPrimitivesEx`) path — covered by the sibling files noted above, not a gap in this file's own
  scope.

## Positive Findings

- Genuinely well-designed differential test: the file's own header comment states the exact reasoning
  ("a test where every check expects the SAME outcome cannot distinguish 'culling works' from 'culling
  is bypassed entirely'") and the 6-check table is constructed precisely to defeat that failure mode —
  this audit confirms the geometry and expected outcomes are actually correct, not just self-consistent.
- Correct, minimal-state-per-check design (`RunCheck` builds a fresh `RasterizerState` each time) avoids
  a whole class of "leftover state from previous check" false negatives.

## Final Assessment

A solid, correctly-reasoned test whose production-code mapping this audit independently traced end to
end (XNA enum → backend switch → bgfx state flag → draw call) and found correct at every step.
