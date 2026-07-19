# Audit: include/Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp`
- Audit status: AUDITED (full read, 31 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/IRenderTarget.cs`
- Main related tests: not independently located in this pass

## Purpose
Common interface implemented by both `RenderTarget2D` and `RenderTargetCube`, exposing the
render-target-specific properties (`Width`, `Height`, `LevelCount`, `RenderTargetUsage`,
`DepthStencilFormat`, `MultiSampleCount`).

## Executive Verdict
Mostly correct, with two minor, defensible divergences from FNA worth recording (not scored as
defects — see below).

## Checklist Results

### Visibility mapping
FNA's `IRenderTarget` is declared `internal interface IRenderTarget` (line 19 of the FNA source) —
per this project's own stated C#-to-C++ visibility convention, `internal` should map to `private`/
`protected`/a detail namespace/or be omitted, not left as a plain public type. CNA's `IRenderTarget`
is a fully public class directly in `Microsoft::Xna::Framework::Graphics` with no narrower
visibility. This is a real, if low-impact, deviation from the stated convention — C++ has no
first-class `internal` equivalent, and `RenderTarget2D`/`RenderTargetCube` (public XNA types) must
be able to implement this interface from outside a "detail" namespace without excessive friction, so
the pragmatic accommodation is understandable, but it isn't disclosed anywhere as an intentional
exception the way other visibility deviations in this codebase usually are.

### Missing members vs. FNA
FNA's real `IRenderTarget` additionally declares `IntPtr DepthStencilBuffer { get; }` and
`IntPtr ColorBuffer { get; }` (native FNA3D renderbuffer handles) — CNA's version omits both. This
is consistent with CNA's fundamentally different backend architecture (`IGraphicsBackend`/
`IRenderTargetBackend` abstraction instead of FNA3D's raw native handles), so these two members have
no direct CNA equivalent to expose through this specific interface; not treated as a defect.

## Detailed Findings

### LOW — `IRenderTarget`'s FNA-`internal` visibility is not preserved or explicitly disclosed as an
accepted exception
See "Visibility mapping" above. No behavioral consequence (C++ access control differences here
don't change observable behavior), but worth a one-line acknowledgment in the header that this is a
deliberate accommodation, consistent with how other visibility deviations are documented elsewhere
in this codebase.

## Cross-File Observations
Both `RenderTarget2D` and `RenderTargetCube` (audited in this same batch) correctly implement every
declared pure virtual, with `getWidthProperty()`/`getHeightProperty()` correctly delegating to their
own texture-family width/height/size storage.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every member this interface DOES declare has correct semantics matching FNA's own getter contracts.

## Final Assessment
One LOW finding (undisclosed visibility-mapping deviation, no behavioral impact).
