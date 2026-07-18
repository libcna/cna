# Audit: examples/easygl_depthstencilstate_stencil_mask_test.cpp

## Metadata

- Source file: `examples/easygl_depthstencilstate_stencil_mask_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard), Task 316
- File type: standalone `Microsoft::Xna::Framework::Game` subclass / pixel-readback integration test, built as its own executable
- Related production code: `Microsoft::Xna::Framework::Graphics::DepthStencilState` (`include/…/DepthStencilState.hpp`,
  `src/…/DepthStencilState.cpp`), `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::ApplyDepthStencilState`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:1924-1974`)
- FNA reference: `Graphics/States/DepthStencilState.cs` (`StencilMask`, `StencilWriteMask` properties, default
  `Int32.MaxValue`)
- Build registration: `cmake/Tests/EasyGLTests.cmake:1398-1402` (`EasyGL_DepthStencilState_StencilMask`), **and**
  `cmake/Tests/VulkanTests.cmake:176-182` (`Vulkan_DepthStencilState_StencilMask`) — this exact source file is
  compiled into two different backend-selected executables, not EasyGL-exclusive despite the filename.

## Purpose

Verifies `DepthStencilState::StencilMask` (the read mask applied to both the stencil-buffer value and
`ReferenceStencil` before the compare) and `DepthStencilState::StencilWriteMask` (which bits of a stencil write
actually land) genuinely affect rendered output on the EasyGL backend, using a 4-column screen layout with a
"stamp" draw establishing a known stencil baseline (never via `Clear()`, since `GraphicsDevice::Clear` ignores
`ClearOptions::Stencil`, a documented separate defect, Task 871) followed by an operation draw and a read-back
quad.

## Executive Verdict

**Healthy.** The test is a genuinely differential design (narrow-mask column paired with a full-mask
contrast/control column expecting the opposite outcome), and cross-checking `EasyGLGraphicsBackend::
ApplyDepthStencilState` (lines 1924-1974) confirms the backend actually implements `stencilMask`/`stencilWriteMask`
via real `glStencilFuncSeparate`/`glStencilMaskSeparate`-equivalent calls, so the properties under test are real,
implemented behavior, not a no-op the test would pass against vacuously.

## Checklist Results

### Purpose
PASS — single-responsibility test file, correctly placed under `examples/`, named and commented per its task ID
(316).

### API / XNA / FNA parity
PASS — `DepthStencilState::setStencilMaskProperty`/`setStencilWriteMaskProperty`/`setReferenceStencilProperty` map
1:1 to FNA's `StencilMask`/`StencilWriteMask`/`ReferenceStencil` (`DepthStencilState.cs:104-198`). The test's use of
`0x7FFFFFFF` as the "full/no-op mask" constant matches FNA's actual default (`StencilMask = Int32.MaxValue;`,
`DepthStencilState.cs:267-268`), confirmed by reading the FNA constructor directly rather than assuming it.

### Behavioral correctness
PASS, with real verification, not just assertion-shaped code. Traced the arithmetic by hand:
- Columns 1-2 (read mask): stamp writes `0x05` with full write mask. Column 1 tests
  `ReferenceStencil=0x01, StencilMask=0x01` against `Equal` → `(0x01 & 0x01) == (0x05 & 0x01)` → `0x01==0x01` → PASS.
  Column 2 (control) uses the same reference/compare with `StencilMask=0xFF` → `0x01==0x05` → FAIL. Only a backend
  that genuinely honors the read mask can produce this specific divergent pair; a backend that silently uses a
  full/no-op mask for both would show BACKGROUND for both, which the test's pass/fail matrix (`expectPass = {true,
  false, true, false}`, lines 201-214) would catch as a wrong result for column 1.
- Columns 3-4 (write mask): stamp `0xFF` full-write, then `Replace` with `ReferenceStencil=0x00` under narrow
  (`0x0F`) vs. full (`0xFF`) write masks. Narrow case: `(0x00 & 0x0F) | (0xFF & ~0x0F) = 0xF0` (verified by hand);
  full case: `0x00`. Read-back queries `0xF0` in both — narrow column passes, full column correctly fails. This is
  exactly the kind of masked-replace arithmetic a naive test could get backwards; it does not here.

### Logic
PASS — `MakeStampState`/`MakeTestState` (lines 91-114) correctly set only the fields relevant to each phase
(`StencilPass=Keep`/`StencilFail=Keep` on the read-only test state so the read-back draw cannot itself perturb the
buffer it's checking).

### Memory/resource lifetime
N/A (no manual resource ownership beyond the `std::unique_ptr<GraphicsDeviceManager>`, which is a standard,
correctly-scoped member).

### C++ correctness
PASS — `results[4]` array is default-initialized to `(0,0,0,0)` before being overwritten by `GetBackBufferData`;
`static_cast<int>(...)` used consistently for float→pixel-coordinate conversion (line 190). No UB observed.

### Performance
N/A — single-frame test, not a hot path.

### Architecture
PASS — cleanly isolates the property under test (`DepthBufferEnable=false` throughout, per the file's own header
comment, to avoid conflating with the separately-tracked depth-compare bug, Task 870).

### Maintainability
LOW note: the literal `0x7FFFFFFF` (int32 max, i.e. "full/no-op mask") appears three times (lines 160, 173, 180)
instead of a named constant (e.g. referencing `DepthStencilState`'s own default via a helper or a local
`constexpr int kFullMask`). Purely cosmetic — the value is correct and the comments explain it clearly each time.

### Robustness
N/A for a test harness.

### Testing
This *is* the test; see Behavioral correctness above for how its own internal validity was checked.

### Cross-file consistency
The file's own header comment (lines 37-43) documents that this same source, when compiled for the Vulkan backend
(`VulkanTests.cmake:176-182`), is expected to show columns 1/3 passing "by coincidence" (Vulkan's
`ApplyDepthStencilState` never sets `stencilTestEnable`, Task 870) while columns 2/4 correctly fail — a clear,
self-aware acknowledgment that passing on Vulkan is not evidence the feature works there. This is exactly the kind
of documented, deliberate test design this audit rewards; verified consistent with the Vulkan shard's own
`VulkanTests.cmake:176-178` comment ("expected to partially fail...2 of 4 checks pass only by coincidence").

## Detailed Findings

No HIGH or CRITICAL findings. No MEDIUM findings — the test's differential design specifically defeats the
"trivially passes on a bypassed/no-op implementation" failure mode this audit is watching for in effect/state
tests, and its assertions were hand-verified against the real EasyGL backend implementation.

- LOW / maintainability: repeated `0x7FFFFFFF` magic-number mask literal (see Maintainability above). Confidence
  HIGH, but cosmetic only — no behavioral risk.

## Missing or Weak Tests

The test does not exercise `StencilMask`/`StencilWriteMask` values other than the two endpoints (narrow vs.
`Int32.MaxValue`) — e.g. a mask that only clears the *high* bits, or a mask value larger than 8 bits (XNA's
`StencilMask`/`StencilWriteMask` are `int`, but the underlying hardware stencil buffer is typically 8 bits; FNA
itself does not special-case this, so it's a legitimate but low-priority gap, not a bug in this file).

## Positive Findings

- Exemplary differential-test methodology: every property under test has a paired contrast/control case with an
  *opposite* expected outcome, closing off the "silently-always-passes" failure mode that a same-outcome-only test
  would miss.
- The file's own comments explicitly document the known Vulkan-backend gap (Task 870) and correctly predict *which*
  of its 4 checks would pass there "by coincidence" rather than as real evidence — an unusually rigorous, falsifiable
  claim for a test file's own header comment.
- Confirmed against real production code (`EasyGLGraphicsBackend::ApplyDepthStencilState`) rather than assumed: the
  backend genuinely calls separate stencil-func/op/mask setters gated on `stencilEnable`, so this test has real
  power to catch a regression.

## Final Assessment

A well-constructed, evidence-based differential test that genuinely exercises `StencilMask`/`StencilWriteMask` on
the EasyGL backend and is correctly wired to a real, verified backend implementation. No correctness issues found;
only a cosmetic magic-number note.
