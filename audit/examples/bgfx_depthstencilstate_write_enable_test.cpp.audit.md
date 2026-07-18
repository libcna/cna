# Audit: examples/bgfx_depthstencilstate_write_enable_test.cpp

## Metadata

- Source file: `examples/bgfx_depthstencilstate_write_enable_test.cpp` (163 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DepthStencilState.DepthBufferWriteEnable` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_depthstencilstate_write_enable …)` /
  `cna_register_backend_test(NAME Bgfx_DepthStencilState_WriteEnable …)`,
  `cmake/Tests/BgfxTests.cmake:587-589`).
- XNA/FNA relevance: direct — `DepthStencilState.DepthBufferWriteEnable`.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyDepthStencilState`,
  `depthFlags_ = depthWriteEnable ? BGFX_STATE_WRITE_Z : 0;`, line 1685).

## Purpose

A three-quad, two-check differential test proving that `DepthBufferWriteEnable=false` skips
recording the depth *value* while still allowing the *color* write to happen (the correct GPU
semantics: depth-write and color-write are independent). Draws far quad A (red, z=0.8, writes
depth), then near quad B (green, z=0.2, write-enable is the variable under test), then a third quad
C (blue, z=0.5 — strictly between A and B) with `DepthStencilState::Default` (test+write both on).
C's fate reveals what the depth buffer actually holds after B: if B's write was skipped, the buffer
still holds A's 0.8, so C's `0.5 < 0.8` passes and the centre ends BLUE; if B's write happened
anyway (a write-disable bug), the buffer holds B's 0.2, `0.5 < 0.2` fails, and the centre stays
GREEN. Check B is the sanity/contrast pass proving the depth-compare mechanism itself is not broken
(`DepthStencilState::Default`, writes genuinely on, expects GREEN — i.e. C is correctly rejected).

## Executive Verdict

**Healthy** — this audit independently re-derived both checks' expected final colors from the
stated depth values and compare function, and both match; the three-quad "does write-disable also
disable *color*" isolation is a correct, non-trivial technique (a naive test that only checked "did
the near quad's color show up" could not distinguish write-disable from a depth-test bypass).

## Checklist Results

### API / XNA / FNA parity
`setDepthBufferWriteEnableProperty`/`setDepthBufferEnableProperty`/`setDepthBufferFunctionProperty`
(lines 85-87) match `DepthStencilState.hpp` exactly. `CompareFunction::LessEqual` used for B's state
(line 87) matches FNA's own `DepthStencilState.Default`'s comparison function
(`CompareFunction.LessEqual`, confirmed against `DepthStencilState.hpp:15` doc comment: "depth test
and write both enabled with LessEqual comparison (XNA default)").

### Behavioral correctness
Re-traced both checks:
- Check A (`bWritesEnabled=false`): A writes depth=0.8 (color red). B drawn at z=0.2 with
  `DepthBufferFunction=LessEqual`, write disabled: `LessEqual(0.2, 0.8)`=true→passes→color
  overwritten to green, **but depth buffer left at 0.8** (write disabled). C drawn last at z=0.5
  with `DepthStencilState::Default` (write **on**): `LessEqual(0.5, 0.8)`=true→passes→color
  overwritten to blue, depth updated to 0.5. Final visible color: **BLUE**. Matches
  `aOk = a.getBProperty()>=200 && …` (line 129), and the file's own stated expectation (line 23:
  "B's write was truly skipped… C's compare is 0.5 < 0.8 → PASSES → centre ends BLUE").
- Check B (`bWritesEnabled=true`, sanity): B now genuinely writes depth=0.2 (color green). C at
  z=0.5: `LessEqual(0.5, 0.2)`=false→**fails**→C's blue color is *not* written, green remains
  visible. Final visible color: **GREEN**. Matches `bOk` (line 130) and the file's stated
  expectation (line 26: "expect GREEN (C is correctly rejected)").
- Both derivations independently confirm the test's own math is internally consistent and correctly
  isolates the write-enable flag from the depth-compare mechanism itself.

### Logic
Each `RunCheck()` call constructs its own `DepthStencilState bState` fresh with the caller-supplied
`bWritesEnabled` (lines 84-87) — no state leakage between the two checks, each of which is run in
its own retry-until-settled loop (lines 90-115) against a pure-black `(0,0,0,255)` clear, so the
break-on-non-zero condition (line 111) is unambiguous (all three quads' colors are non-zero).

### C++ correctness
`Red()`/`Green()`/`Blue()` (lines 78-80) are function-local `static const Color` singletons returning
references — safe (no static-init-order issue since each is only touched from within `Draw()`,
well after program start, and constructed on first use).

### Robustness
The `if (!aOk) { std::printf("[INFO] …"); }` block (lines 137-141) gives a specific, actionable
diagnostic message tied to exactly what a check-A failure would mean (a genuine write-enable bypass
bug), which is good test-authoring practice beyond the bare pass/fail print every other file in this
batch uses.

### Testing
This is the single property (`DepthBufferWriteEnable`) this file covers, and it does so completely,
including the necessary sanity/contrast pairing that distinguishes "write-disable itself is broken"
from "the depth-compare mechanism used to observe it is broken."

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Shares the `RasterizerState::CullNone` (Task 364/896) workaround and the single-read-per-frame
  Bgfx restructuring (Task 406) with the rest of this batch; both independently re-verified in the
  sibling `bgfx_depthstencilstate_compare_function_test.cpp.audit.md` report and not re-litigated
  here.
- This file's three-quad "does write-enable actually gate the *depth value*, not just visibility"
  technique is more rigorous than a simpler two-quad "is B visible" test would be, and is the same
  general pattern (an in-between witness quad revealing hidden buffer state) used successfully by
  `bgfx_depthstencilstate_stencil_ops_test.cpp`'s read-back quad in this same batch.

## Missing or Weak Tests

None identified for this file's single-property scope.

## Positive Findings

- The three-quad witness technique is a genuinely non-obvious and correct way to test a
  write-*enable* flag (as opposed to a read/compare flag) via pixel readback, since the write flag
  has no direct visible effect on its own draw call — only a *later* draw's depth comparison can
  reveal whether the write happened.
- The diagnostic `[INFO]` message on failure is a small but real quality-of-life improvement over
  the bare pass/fail prints elsewhere in this shard.

## Final Assessment

A correctly-designed and independently-verified test of `DepthBufferWriteEnable`, whose three-quad
witness technique properly isolates the write flag from the depth-compare mechanism used to observe
it. No defects found.
