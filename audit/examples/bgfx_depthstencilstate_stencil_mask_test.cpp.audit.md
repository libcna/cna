# Audit: examples/bgfx_depthstencilstate_stencil_mask_test.cpp

## Metadata

- Source file: `examples/bgfx_depthstencilstate_stencil_mask_test.cpp` (248 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DepthStencilState.StencilEnable` /
  `StencilMask` (read mask) / `StencilWriteMask` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_mask …)` /
  `cna_register_backend_test(NAME Bgfx_DepthStencilState_StencilMask …)`,
  `cmake/Tests/BgfxTests.cmake:607-609`).
- XNA/FNA relevance: direct — `DepthStencilState.StencilEnable`, `.StencilMask`,
  `.StencilWriteMask`, `.ReferenceStencil`, `.StencilFunction`.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BuildBgfxStencil`, lines 1662-1672; `ApplyDepthStencilState`/`RebuildStencilState`,
  lines 1674-1765).

## Purpose

Five per-frame checks, each its own `RunCheck()` pass (Bgfx `GetBackBufferData`-first-read
restructuring, same rationale as the rest of this shard): checks 1-2 stamp the whole screen to a
known stencil value then draw a test quad gated by `StencilFunction::Equal`, proving
`StencilEnable` actually gates draws on a stencil match/mismatch; checks 3-4 stamp `stencil=0x05`
and test with a narrow (`0x01`) vs. full (`0xFF`) *read* mask against `ReferenceStencil=0x01`,
proving the read mask is genuinely applied (narrow passes, full — same stamped value — fails);
check 5 is explicitly marked informational-only (not counted toward pass/fail) because bgfx has no
per-draw stencil *write* mask primitive at all.

## Executive Verdict

**Healthy** — the 4 counted checks are a correctly-designed differential test (each pairs a
passing and a matching-but-differently-masked failing case on the identical stamped value, which is
exactly what's needed to prove the mask is read and not silently ignored), and this audit
independently confirmed check 5's own claim (that bgfx has no stencil write-mask primitive) against
bgfx's `defines.h`.

## Checklist Results

### API / XNA / FNA parity
`setStencilEnableProperty`/`setStencilFunctionProperty`/`setStencilMaskProperty`/
`setStencilWriteMaskProperty`/`setReferenceStencilProperty`/`setStencilPassProperty`/
`setStencilFailProperty` (lines 93-114) are all exact matches against
`DepthStencilState.hpp`'s public property surface.

### Behavioral correctness
- Check 1/2 (`StencilEnable=true`, stamped vs. not-stamped): `MakeStampState(1)` stamps buffer=1 via
  `StencilFunction::Always`/`StencilPass::Replace`; `MakeTestState(1)` then tests
  `Equal(ref=1, stored)`. Stamped case: `1==1`→true→green drawn (matches `expectPass=true`).
  Not-stamped case: `MakeStampState(0)` leaves stored=0, test compares `1==0`→false→green rejected,
  background (`kBackground(20,20,20)`) remains visible (matches `expectPass=false`).
- Check 3/4 (mask): `MakeStampState(0x05)` stores 0x05. Check 3: `MakeTestState(0x01, readMask=0x01)`
  → `Equal((0x01&0x01), (0x05&0x01))` = `Equal(0x01,0x01)`=true→PASS→GREEN (matches). Check 4 (same
  stamped value, `readMask=0xFF`): `Equal((0x01&0xFF),(0x05&0xFF))`=`Equal(0x01,0x05)`=false→FAIL→
  BACKGROUND (matches). This audit independently re-verified both bitwise computations by hand —
  both match the file's own stated expectations exactly, and the pairing genuinely proves the mask
  is read (a silently-ignored mask would make checks 3 and 4 show the *same* result, which they do
  not).
- Check 5 (`StencilWriteMask=0x0F`, informational): stamps `0xFF`, attempts to write `0x00` through a
  narrow `0x0F` write mask (hoping for `0xF0` if honored), then tests `ReferenceStencil=0xF0`. Since
  `check.counted=false` (line 200), this check's outcome never affects `result_`/exit code
  regardless of whether it passes or fails, exactly as documented.

### Logic
`IsGreen`/`IsBackground` (lines 123-130) thresholds (`kBackground=(20,20,20)`, green requires G≥200)
have no overlap. Deliberately choosing a *non-black* background color (`(20,20,20)` rather than
`(0,0,0)`) is a correct and clever design choice: it lets the retry loop's
`got.getRProperty()!=0 || …` break condition (lines 151-153) distinguish a real, settled "background
still showing" frame (20,20,20 — non-zero, breaks immediately) from a genuinely stale/blank Bgfx
readback (`(0,0,0)` — retries), which a pure-black background could not do.

### C++ correctness
`std::function<void(GraphicsDevice&)>` lambdas captured by reference (`[&]`, lines 169 etc.) are all
invoked synchronously within the same `Draw()` call that constructs them (via `RunCheck`) — no
dangling-capture risk.

### Robustness
The narrow-vs-full-mask contrast (checks 3/4) is the right technique to distinguish "the mask is
genuinely applied" from "the mask parameter is accepted but ignored" — a bug class this audit
confirmed is real elsewhere in the same production function (see F1).

### Testing
Covers `StencilEnable` (partially — `StencilEnable=false` is explicitly *not* re-tested here, per
the file's own header comment, deferring to prior EasyGL/Vulkan coverage of Task 315) and
`StencilMask` (read mask) fully via a genuinely discriminating pair. `StencilWriteMask` is
documented as untestable on this backend rather than silently skipped.

## Detailed Findings

### F1 — `BuildBgfxStencil()`'s `(void)writeMask;` statement is unreachable dead code (production, not this test file)

- Severity: LOW
- Confidence: HIGH (read directly)
- Category: cross-file / maintainability (production code this test's own header comment cites)
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:1662-1672`,
  `static uint32_t BuildBgfxStencil(...)`
- Evidence:
  ```cpp
  static uint32_t BuildBgfxStencil(int func, int pass, int fail, int depthFail,
                                   int mask, int writeMask, int ref)
  {
      return XnaCompareFuncToBgfxStencilTest(func)
           | XnaStencilOpToPassZ(pass)
           | XnaStencilOpToFailS(fail)
           | XnaStencilOpToFailZ(depthFail)
           | BGFX_STENCIL_FUNC_REF(static_cast<uint8_t>(ref))
           | BGFX_STENCIL_FUNC_RMASK(static_cast<uint8_t>(mask));
      (void)writeMask; // bgfx uses a global stencil write mask, not per-state
  }
  ```
  The `(void)writeMask;` statement is placed **after** the `return`, inside the same block, so it is
  unreachable — it can never execute. This does not change behavior (the parameter is discarded
  either way, since it's absent from the returned OR-expression regardless of whether the cast runs)
  but is a genuine, if harmless, authoring mistake: the intended "silence unused-parameter warning"
  comment doesn't actually do anything, because control flow never reaches it.
- Why it matters: purely cosmetic/maintainability today (the parameter genuinely is unused in the
  bitmask either way, confirmed correct per this test's own check 5 expectations), but the dead
  statement could mislead a future reader into thinking the cast is load-bearing, and some compiler
  configurations (`-Wunreachable-code`, not on by default in this project's build flags) would flag
  it.
- FNA/XNA comparison: N/A — bgfx API limitation, not an FNA behavior question. This audit
  independently confirmed via `bgfx/include/bgfx/defines.h` that `BGFX_STENCIL_FUNC_RMASK` exists
  but no `BGFX_STENCIL_FUNC_WMASK` or equivalent per-draw write-mask flag exists anywhere in bgfx's
  state system — the comment's claim that this is a genuine upstream bgfx limitation is accurate.
- Related files: this test file's own header comment (lines 33-39) already correctly describes this
  limitation; the fix (moving the `(void)writeMask;` before the `return`, or removing it since the
  parameter is already implicitly "used" as a formal parameter name) belongs to
  `BgfxGraphicsBackend.cpp`, out of scope for this examples-file audit.
- Suggested future action (not implemented by this audit): move the `(void)writeMask;` line before
  the `return` statement in `BuildBgfxStencil()`.

## Cross-File Observations

- Confirms the same `RasterizerState::CullNone`/`GetBackBufferData` first-read patterns documented
  in the sibling files of this shard (see `bgfx_depthstencilstate_compare_function_test.cpp.audit.md`
  for the independent verification of those two shared claims).
- `stencilWriteMaskCached_` (`BgfxGraphicsBackend.cpp:1713`) is faithfully cached and *passed* into
  `BuildBgfxStencil` (lines 1744, 1748, 1755) even though the callee discards it — this confirms the
  test file's own claim ("`BgfxGraphicsBackend::BuildBgfxStencil` already discards `stencilWriteMask`
  with a documented comment") is accurate, not a stale/unverified assertion.

## Missing or Weak Tests

None beyond what the file itself already documents as out of scope (`StencilEnable=false` isolation,
deferred to other files in this project; `StencilWriteMask` real enforcement, impossible on this
backend).

## Positive Findings

- The choice of a non-black `kBackground` specifically to make the Bgfx stale-read retry idiom
  reliable (rather than reusing black, which would be ambiguous with a stale `(0,0,0)` readback) is
  a subtle, correct piece of test-harness engineering that this audit had to independently reason
  through to confirm — not something obvious from a first skim.
- Check 5's honest "cannot be fixed here, permanent bgfx API limitation" framing, backed by this
  audit's own independent confirmation against bgfx's `defines.h`, is good engineering practice
  consistent with this project's stated precedent (Task 872, Task 923).

## Final Assessment

A well-designed, genuinely discriminating test for `StencilEnable` and `StencilMask`, with an
honestly-scoped informational check for the one property this backend cannot support. The one
finding (F1) is a harmless dead-code cosmetic issue in the production function this test's header
comment cites, not a defect in the test itself.
