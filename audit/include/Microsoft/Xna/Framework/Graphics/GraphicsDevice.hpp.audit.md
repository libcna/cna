# Audit: include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` (888 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct, central XNA type; FNA reference: `src/Graphics/GraphicsDevice.cs` (1820 lines)
- Main related tests: not independently located in this pass

## Purpose
The central XNA graphics type: device state (blend/depth-stencil/rasterizer/viewport/scissor),
resource binding (vertex/index buffers, render targets, textures), drawing, presenting, and
resetting.

## Executive Verdict
Correct and exceptionally thoroughly documented. Nearly every non-trivial member's doc comment
cites a specific `plans/plan_dx9.md` task ID explaining a real, concrete behavioral gap this port closes
or a deliberate, disclosed simplification. See the paired `.cpp` report for the full behavioral
verification (this header declares the full real XNA `GraphicsDevice` surface plus several
well-justified `NOXNA` extensions; no header-level defect found).

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` usage: correctly applied throughout (`PrimitiveVerts`, `OnResourceCreated`/`OnResourceDestroyed`/
  `AddResourceReference`/`RemoveResourceReference`/`GetTrackedResourceCount`, `SetDepthTestEnabled`/
  `SetBlendEnabled`/`SetDepthWriteEnabled`, `SetGraphicsProfileEXT`, `SetContextRecoveryEnabled`,
  `SetStringMarkerEXT`, `GetBackend`/`GetGraphicsBackendType`/`GetGraphicsBackendName`/
  `SupportsCapability`, `SetCurrentEffect`, `SetPresentationParameters`,
  `RecreateBackendForMultiSampleCount`).
- Copy/move explicitly deleted (matches real XNA's reference-type, non-copyable `GraphicsDevice`
  semantics faithfully — a `class`, not a `struct`, correctly non-value-typed here).
- Concrete `System::Object`-derived class overrides `GetTypeName()` with `NOXNA` — correct.
- `ResetViewportAndScissorForRenderTarget`'s doc comment correctly documents a real FNA behavior
  (every `SetRenderTarget`/`SetRenderTargets` call resets Viewport/ScissorRectangle to the new
  target's dimensions) — confirmed consistent with the `.cpp`'s implementation.

## Detailed Findings
None at the header level — see the paired `.cpp` report for behavioral findings (raw exception
types, `Dispose()` ordering).

## Cross-File Observations
`SetGraphicsProfileEXT`'s doc comment (lines 679-692) documents a genuine, real architectural
constraint: `GraphicsProfile` is fixed at construction in real XNA (`GraphicsDevice.GraphicsProfile`
has no public setter there), but this project's `Game`/`GraphicsDeviceManager` architecture
eagerly default-constructs `Game`'s `GraphicsDevice_` member (hardcoded `Reach`) before a game's own
`GraphicsDeviceManager.GraphicsProfile` request can run — `SetGraphicsProfileEXT` is the disclosed,
narrowly-scoped internal fix for that ordering problem, not a general public runtime profile switch
(which real XNA doesn't have either).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `SetGraphicsProfileEXT`/`RecreateBackendForMultiSampleCount`/`deviceStatus_` doc comments are
exemplary: each explains a real, specific architectural constraint (construction-order dependency,
one backend property fixed at backend-construction time, most backends never reporting real device
loss) rather than leaving a reader to wonder why a NOXNA member exists.

## Final Assessment
No header-level findings. See the paired `.cpp` report for the shard's most significant findings
in this fork's batch.
