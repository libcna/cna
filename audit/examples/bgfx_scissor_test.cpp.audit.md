# Audit: examples/bgfx_scissor_test.cpp

## Metadata

- Source file: `examples/bgfx_scissor_test.cpp` (183 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RasterizerState.ScissorTestEnable` / `GraphicsDevice.ScissorRectangle`
  interaction test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_Scissor`
  (`cmake/Tests/BgfxTests.cmake:720-723`)
- XNA/FNA relevance: direct — `RasterizerState.ScissorTestEnable`, `GraphicsDevice.ScissorRectangle`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` —
  `ApplyScissorOverride()` (1880-1888), its 5 call sites in the 3D draw-dispatch functions (1524, 2305,
  2351, 2398, 2906, 3420), `SetScissorRectangle`-equivalent setter (~1845-1852), the `scissorEnabled_`
  setter (1785-1789); `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:1728`
  (`setScissorRectangleProperty`).

## Purpose

Bgfx-specific adaptation of `examples/easygl_scissor_test.cpp` (Task 209), restructured (per the header
comment, lines 4-9) into one separately-read `RunCheck()` pass per (RasterizerState, column) pair
because Bgfx's `GetBackBufferData()` only reliably reflects the first read per rendered frame (the
project-wide Task 406 finding). Four logical checks, 7 total pass/fail assertions: (1) scissor test
disabled → both columns show the draw colour despite a right-half `ScissorRectangle` being set; (2)
scissor test enabled with a right-half rectangle → only the right column shows the draw colour, left
stays background; (3) scissor test disabled again → both columns show the draw colour again, proving
the toggle is not sticky; (4) a pure getter/setter round-trip on `ScissorRectangle` with no rendering
involved.

## Executive Verdict

**Healthy** — the header comment credits this task with finding and fixing a genuine, real bug ("none
of Bgfx's 4 3D draw-dispatch functions ever called `bgfx::setScissor` — only the 2D SpriteBatch path
did"), and this audit independently confirmed both halves of that claim: the fix is present at 5 call
sites across the 3D dispatch functions, and the test's own 3-phase structure (off/on/off-again) would
have caught the described pre-fix state.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState.setScissorTestEnableProperty()`/`GraphicsDevice.setScissorRectangleProperty()`/
`getScissorRectangleProperty()` (lines 91-93, 156-157) match FNA's `RasterizerState.ScissorTestEnable`/
`GraphicsDevice.ScissorRectangle` property surface exactly (property is get/set on the device, not on
the rasterizer state object itself, matching XNA's actual split between "does scissoring apply" (state
object) and "what is the rectangle" (device-level) design).

### Behavioral correctness
Verified `ApplyScissorOverride()` (`BgfxGraphicsBackend.cpp:1880-1888`):
```
if (scissorEnabled_ && scissorW_ > 0 && scissorH_ > 0)
    bgfx::setScissor(scissorX_, scissorY_, scissorW_, scissorH_);
```
gated on `scissorEnabled_`, confirmed set from `RasterizerState.ScissorTestEnable` via a dedicated
setter (line 1789, `scissorEnabled_ = scissorTestEnable;`) — independently tracked from whether the
rectangle itself is non-zero, matching that member's own declaration-comment claim (quoted at line
1785-1788) that the two must be tracked independently. This is exactly the check this test's phase 1
(scissor off, rect set to right-half) discriminates: a naive implementation deriving "is scissoring
active" from "is the rect non-default" would incorrectly clip in phase 1.
`ApplyScissorOverride()` is confirmed called from all 5 of the file paths a `DrawUserPrimitives`/
`DrawPrimitives` call in this test could reach (lines 1524, 2305, 2351, 2398, 2906, 3420) — the fix the
header comment describes is real and current, not stale.

### Logic
The test's rectangle (`rightHalf = Rectangle(kSize/2, 0, kSize-kSize/2, kSize)`, line 120) spans the
**full height** of the 64×64 target — this means the test cannot discriminate a Y-axis
origin/orientation bug in `bgfx::setScissor`'s coordinate mapping (top-left vs. bottom-left origin),
since every row behaves identically regardless of which convention is used. This is a real, narrow gap
(not exercised because there was no need to for this task's actual scope — X-axis clipping is what the
3-phase design targets), noted as F1 below at low severity since XNA's `Rectangle`/`Viewport` convention
and bgfx's `setScissor` are both top-left-origin in practice, so this is a theoretical rather than
suspected-active gap.

### C++ correctness
`RunCheck()` (78-107) is a straightforward stack-scoped helper; no lifetime issues. `sampleX_` as a
mutable member toggled before each `RunCheck()` call (lines 109, 131/134/139/142/147/150) is a slightly
unusual "hidden parameter via member state" pattern but is not a correctness risk here since `Draw()` is
single-threaded and each `RunCheck()` call fully completes (including its own read) before `sampleX_` is
reassigned for the next check.

### Robustness
Checks 1 and 3 (scissor off, both before and after the "on" phase, lines 130-136 and 146-152) are the
correct technique to prove the property genuinely toggles back rather than latching — this specifically
guards against a plausible implementation bug where `ApplyScissorOverride()` might only ever be called
once, or where `scissorEnabled_` might get set but never unset. Both are independently exercised with
*different* draw colours (Red, then Blue) for phases 1 and 3, which additionally rules out a
"check silently re-reads a stale framebuffer from an earlier phase" false-positive.

### Testing
Check 4 (getter/setter round-trip, lines 154-160) is appropriately kept out of the Bgfx-specific
restructuring, since it involves no rendering — a correct scoping decision, not an oversight.

## Detailed Findings

No MEDIUM/HIGH/CRITICAL findings. One LOW observation:

### F1 — Test rectangle spans full height; cannot discriminate a Y-axis scissor origin/orientation bug
- Severity: LOW
- Confidence: HIGH (structural — confirmed directly from the rectangle's own construction)
- Category: test-coverage
- Location/symbol: `rightHalf(kSize / 2, 0, kSize - kSize / 2, kSize)` (line 120)
- Evidence: the rectangle's `Y=0, Height=kSize` spans every row of the 64×64 target; both sample points
  (`leftX, kSize/2` and `rightX, kSize/2`, lines 100, 118-119) only vary in X. A hypothetical bug that
  flipped `bgfx::setScissor`'s Y coordinate (top-left vs. bottom-left origin) would be completely
  invisible to this test, since the scissor rectangle covers the same full-height band either way.
- Why it matters: this is a real, narrow gap in coverage, not a bug — `bgfx::setScissor`'s own
  documented coordinate convention matches XNA's `Rectangle`/`Viewport` top-left-origin convention (no
  flip needed in practice, and this audit found no evidence of one being applied anywhere in
  `ApplyScissorOverride()`'s call chain), so this is unlikely to be hiding an active defect. Recorded
  as a coverage gap for completeness, matching this audit's mandate to flag untested dimensions even
  when no evidence of an actual defect exists there.
- Suggested follow-up (not implemented by this audit): a 5th check with a rectangle restricted to the
  top or bottom half (rather than full-height, left/right-half) would close this gap cheaply, following
  the same single-pass-per-check Bgfx pattern this file already uses.

## Cross-File Observations

- The "found and fixed a genuine bug: none of Bgfx's 4 3D draw-dispatch functions ever called
  `bgfx::setScissor`" claim (from `cmake/Tests/BgfxTests.cmake:719-720`'s own registration comment,
  matching this file's header) was independently corroborated against the actual call sites in
  `BgfxGraphicsBackend.cpp` rather than trusted at face value — a real fix, still present.
- Shares the `RasterizerState::CullNone` / `DepthStencilState` disable-depth-test conventions with every
  other pixel test in this shard (per Task 364/896's established finding that Bgfx's default cull state
  is the only one of the 3 backends that actually culls this project's standard NDC quad winding).

## Missing or Weak Tests

See F1 — a full-height-only scissor rectangle leaves the Y-axis scissor mapping unverified by this
specific file (though not by any concrete evidence of a defect there).

## Positive Findings

- The 3-phase (off/on/off) structure is a well-chosen, minimal design that specifically proves
  non-stickiness of the toggle, not just "scissoring works when turned on."
- The header comment's claim of a genuine, previously-undiscovered production bug (missing
  `bgfx::setScissor` calls in the 3D path) was independently verified against current source and found
  accurate and still-fixed — a real, useful bug find, not an overclaimed one.

## Final Assessment

A well-scoped, correctly-implemented scissor test with one narrow, low-severity coverage gap (no
Y-axis-only scissor rectangle variant). The header's engineering claim about the underlying bug it
found and fixed was independently confirmed against current production code.
