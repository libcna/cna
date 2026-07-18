# Audit: include/CNA/Internal/Backends/D3DCommon/D3DStateMapping.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3DCommon/D3DStateMapping.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ header (63 lines)
- Related implementation: `src/CNA/Internal/Backends/D3DCommon/D3DStateMapping.cpp` (same shard)
- XNA/FNA relevance: `Blend`/`BlendFunction`/`CompareFunction`/`CullMode`/`FillMode`/`TextureAddressMode`/
  `TextureFilter`/`StencilOperation` -> D3D11 equivalents
- Graphics backend relevance: shared between D3D11 and D3D12 (declared design decision: D3D11-prefixed enum types
  reused directly since D3D11/D3D12 enumerator values are numerically identical, explicitly verified against both
  SDK headers per the file's own comment)
- FNA reference: FNA's own render-state mapping conventions (behavioral reference)
- Main related tests: none found exercising this table directly

## Purpose

Declares 8 XNA-render-state-to-D3D11-enum mapping functions, each documented with its fallback value for an
unrecognized ordinal.

## Executive Verdict

**Healthy — exceptionally well-documented, with an independently-verifiable cull-mode convention explanation.**

## Checklist Results

### API / XNA parity
`CullModeToD3D11`'s doc comment claims `CullClockwiseFace -> D3D11_CULL_FRONT` and
`CullCounterClockwiseFace -> D3D11_CULL_BACK`, reasoning from D3D11's `FrontCounterClockwise = FALSE` default
(clockwise-is-front). **Independently re-derived and confirmed correct**: with clockwise as the front-facing
winding (D3D's native default, unlike OpenGL/Vulkan's counter-clockwise-is-front default, which this project's own
Vulkan backend must explicitly override), culling "clockwise faces" is equivalent to culling front faces
(`D3D11_CULL_FRONT`), and culling "counter-clockwise faces" is equivalent to culling back faces
(`D3D11_CULL_BACK`) — the mapping is correct, and the comment's own reasoning is sound, not just asserted.

### Architecture
The explicit claim that D3D11_BLEND/D3D12_BLEND (and 5 other enum-pair families) "share identical enumerator
values one for one... VERIFIED, not assumed, by direct inspection of both SDK headers" is a genuinely valuable,
falsifiable engineering claim rather than a hand-wave — and correctly caveats that a future D3D12 consumer should
re-verify this against whatever headers it actually builds with, rather than treating the verification as
permanent.

### C++ correctness / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

`StencilOperationToD3D11`'s doc comment correctly distinguishes XNA's `Increment`/`Decrement` (wrapping) from
`IncrementSaturation`/`DecrementSaturation` (clamping) as "two genuinely distinct D3D11 ops, not interchangeable"
— independently verified against the `.cpp` implementation, which does map them to the correct distinct
`D3D11_STENCIL_OP_INCR`/`_DECR` vs `_INCR_SAT`/`_DECR_SAT` targets.

## Missing or Weak Tests

No dedicated test found for the cull-mode/winding-order convention on this backend (this project's own
`rasterizerstate_cullmode_camera_test.cpp`/`docs/xna_culling_compatibility_audit.md`, reviewed elsewhere in this
audit, cover EasyGL/Vulkan/Bgfx but not D3D11/D3D12).

## Positive Findings

The cull-mode convention explanation is a rare example in this audit of a comment whose *reasoning*, not just its
conclusion, was independently re-derivable and found sound — a genuinely higher standard of documentation than a
bare assertion.

## Final Assessment

No issues found.
