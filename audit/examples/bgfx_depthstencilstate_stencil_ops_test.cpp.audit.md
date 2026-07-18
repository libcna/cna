# Audit: examples/bgfx_depthstencilstate_stencil_ops_test.cpp

## Metadata

- Source file: `examples/bgfx_depthstencilstate_stencil_ops_test.cpp` (256 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DepthStencilState.StencilFail` /
  `.StencilDepthBufferFail` / `.StencilPass` (front-facing stencil-op triple) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_ops …)` /
  `cna_register_backend_test(NAME Bgfx_DepthStencilState_StencilOps …)`,
  `cmake/Tests/BgfxTests.cmake:616-618`).
- XNA/FNA relevance: direct — `DepthStencilState.StencilFail`, `.StencilDepthBufferFail`,
  `.StencilPass`, `StencilOperation` enum.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`XnaStencilOpToFailS`/`XnaStencilOpToFailZ`/`XnaStencilOpToPassZ`,
  `BuildBgfxStencil`, lines 1632-1672).

## Purpose

A 3-slot-isolation test plus one contrast/control check, each in its own frame. Method: (1) stamp
depth=0.5/stencil=0x05 unconditionally; (2) an "operation" quad whose `DepthStencilState` is
engineered so that only ONE of the three stencil-op slots (`StencilFail`/`StencilDepthBufferFail`/
`StencilPass`) can fire — the other two slots are set to `Decrement` as a trap that would show up
as a wrong final value if the wrong slot fired; (3) a read-back quad with stencil testing only
(`Equal`, `ReferenceStencil=0x06`) that renders GREEN only if the correct slot's `Increment` fired.
Check 3 is the file's own stated "CRITICAL for this test's validity" contrast: identical operation
to check 2 (buffer really is 0x06), but the read-back deliberately queries a mismatching
`ReferenceStencil=0x99`, which a genuinely-working stencil test must reject (BACKGROUND) — without
this, checks 0-2 alone cannot distinguish "the ops work" from "the stencil test is bypassed
entirely and everything always passes."

## Executive Verdict

**Healthy** — this audit independently re-traced the stencil state machine for each of the 4 checks
(stamp → operation → read-back) and confirms each expected buffer transition
(`0x05→0x06` via the targeted slot, or correctly rejected read for the contrast check) matches the
test's own claims; the 3-slot isolation technique (2 "trap" slots set to `Decrement` while only the
tested slot can plausibly fire `Increment`) is sound.

## Checklist Results

### API / XNA / FNA parity
`StencilOperation::{Increment,Decrement,Replace,Keep}` and
`setStencilFailProperty`/`setStencilDepthBufferFailProperty`/`setStencilPassProperty` (lines
104-106, 171-218) match `DepthStencilState.hpp`'s surface and `StencilOperation.hpp`'s enum exactly
(verified by direct read of both headers — not reproduced here for brevity since covered fully in
this batch's mask-test report).

### Behavioral correctness
Re-traced each check's stencil-buffer arithmetic by hand:
- Check 0 (`StencilFail`): stamp→0x05. `MakeOpState(stencilPasses=false /* ref=0x99 */, …,
  onFail=Increment, onDepthFail=Decrement, onPass=Decrement)`: stencil test itself is
  `Equal(0x99, 0x05)`=false → **fails** → only `StencilFail` (`Increment`) can fire →
  0x05→0x06. Read-back `Equal(ref=0x06, stored=0x06)`=true→GREEN. Matches `expectGreen=true`.
- Check 1 (`StencilDepthBufferFail`): stamp writes depth=0.5 (`DepthBufferWriteEnable=true` in
  `MakeStampState`). Op draw: `stencilPasses=true` (`ref=0x05`==stored 0x05, stencil test
  **passes**), `depthFunc=Less`, drawn at `z=0.8`: `Less(0.8, 0.5)`=false → depth test **fails**.
  Stencil-pass-but-depth-fail is exactly the `StencilDepthBufferFail` slot (`Increment` here) →
  0x05→0x06. Read-back GREEN. Matches.
- Check 2 (`StencilPass`): same `stencilPasses=true`/`depthFunc=Less`, drawn at `z=0.2`:
  `Less(0.2,0.5)`=true → depth test **passes** too → both stencil and depth pass → `StencilPass`
  slot (`Increment`) fires → 0x05→0x06. Read-back GREEN. Matches.
- Check 3 (contrast): identical op to check 2 (buffer really becomes 0x06), but read-back queries
  `ReferenceStencil=0x99`: `Equal(0x99, 0x06)`=false → read-back's own stencil test rejects the
  GREEN fragment → the quad drawn immediately before it (`kBackground`, from the op-draw call)
  remains visible → BACKGROUND. The test's own `ok = check.expectGreen ? sawGreen : !sawGreen`
  (line 226) with `expectGreen=false` (line 218) correctly requires *not* seeing green here. Matches.

### Logic
The "trap" design (setting the two non-tested slots to `Decrement` in every check) is a real safety
mechanism: if e.g. the shared bit-packing in `BuildBgfxStencil`'s single `uint32_t` return value ever
had the wrong slot mapped to the wrong bgfx flag macro (a plausible copy-paste risk given
`XnaStencilOpToFailS`/`XnaStencilOpToFailZ`/`XnaStencilOpToPassZ` are three near-identical switch
functions, `BgfxGraphicsBackend.cpp:1600-1660` region), the wrong slot firing `Decrement` instead of
the intended slot's `Increment` would flip the final stored value away from 0x06, and the read-back
would then show BACKGROUND instead of GREEN — a real, not merely cosmetic, failure signal.

### C++ correctness
No lifetime issues; `std::function` captures by reference within the same synchronous call chain as
the mask test in this shard.

### Robustness
Check 3 is exactly the right technique per the file's own stated design goal, and this audit agrees
it is necessary — without it, a stencil-test-bypass bug (every fragment always passes regardless of
compare function) would be invisible, since checks 0-2 would all still show GREEN by coincidence
(the ops would still fire, just unconditionally rather than because the compare genuinely gated
them).

### Testing
Fully covers the 3 front-facing stencil-op slots individually, plus the validity-of-the-test-itself
contrast check. Two-sided (`CounterClockwiseStencil*`) slots are explicitly out of scope here and
covered by the sibling `bgfx_depthstencilstate_stencil_twosided_test.cpp` in this same batch.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Shares the `MakeStampState`/`MakeOpState`/`MakeReadBackState` three-state pattern conceptually
  with `bgfx_depthstencilstate_stencil_twosided_test.cpp` (same shard, same batch) — both correctly
  isolate a single specific stencil behavior per check via a stamp→operate→read-back pipeline.
- The file's own header comment explicitly credits this design as mirroring the already-validated
  EasyGL Task 317 test ("already reused verbatim on Vulkan") — git history (`58207b39`,
  `95abf994`: "test(Task 317): pixel test for front-face stencil ops; fourth reconfirms Task 870")
  corroborates this lineage claim.

## Missing or Weak Tests

None for this file's stated scope (front-facing stencil ops). Two-sided stencil ops are covered
separately (see `bgfx_depthstencilstate_stencil_twosided_test.cpp.audit.md`).

## Positive Findings

- The "trap" slots (setting untested ops to `Decrement`) turn what could have been a weak
  same-outcome-regardless-of-bug test into one that would actually produce a visibly wrong result
  if the wrong stencil-op slot fired.
- Check 3's contrast design directly addresses the exact "test bypass invisible" failure mode this
  project's own `known_bugs.md`/`NEXT.md` precedent (Task 870, Task 317's fourth check) had already
  identified as a real risk class in this codebase — applying that lesson consistently here.

## Final Assessment

A rigorous, correctly-derived 4-check test that isolates each of the three front-facing stencil
operation slots and includes the necessary contrast check to prove the isolation is meaningful. No
defects found in the test or in the stencil-op mapping it exercises.
