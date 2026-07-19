# Audit: include/Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp`
- Audit status: AUDITED (full read, 41 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/RenderTargetBinding.cs`
- Main related tests: not independently located in this pass

## Purpose
Associates a render-target texture with a rendering output slot (used in
`GraphicsDevice::SetRenderTargets`'s array-of-bindings overload).

## Executive Verdict
Structurally reasonable but missing FNA's real constructor-time validation (null-check and
`CubeMapFace` range-check) and adding an undocumented `arraySlice` parameter not present in real
XNA 4.0/FNA's own `RenderTargetBinding` at all, without a `NOXNA` tag.

## Checklist Results
- FNA's real `RenderTargetBinding` is a `struct` with exactly two constructors:
  `RenderTargetBinding(RenderTarget2D)` and `RenderTargetBinding(RenderTargetCube, CubeMapFace)` —
  both explicitly typed to the concrete render-target classes, not the `Texture` base. CNA's version
  takes a plain `Texture*` in both constructors, a broader (less type-safe) surface than FNA's own —
  a real, if minor, type-safety regression: nothing at the type level prevents constructing a
  `RenderTargetBinding` from an arbitrary non-render-target `Texture*`.
- `RenderTargetBinding(Texture* renderTarget, int arraySlice = 0)` (line 21): the `arraySlice`
  parameter has **no equivalent in real XNA 4.0/FNA's `RenderTargetBinding` at all** (texture-array
  render targets are a newer/MonoGame-era concept, not part of XNA 4.0). Per this project's own
  stated convention ("If implementing functionality that is NOT part of the XNA 4.0 API... you MUST
  wrap it with the NOXNA macro"), this parameter should be `NOXNA`-tagged or otherwise disclosed as
  an extension — it currently isn't.
- Default constructor `RenderTargetBinding()` (line 15) has no direct FNA equivalent either (FNA's
  struct gets an implicit parameterless constructor for free via C# struct semantics, so the
  *effect* is achievable in FNA too via `default(RenderTargetBinding)`, but FNA never exposes it as an
  explicit public API) — also undocumented as an extension.

## Detailed Findings

### MEDIUM — missing FNA's real constructor-time validation
FNA's real `RenderTargetBinding(RenderTarget2D)` constructor throws `ArgumentNullException` for a
null `renderTarget` (`RenderTargetBinding.cs` lines 50-53); its
`RenderTargetBinding(RenderTargetCube, CubeMapFace)` constructor additionally throws
`ArgumentOutOfRangeException` for a `cubeMapFace` outside `[PositiveX, NegativeZ]` (lines 65-68).
CNA's two-argument constructors (`.cpp`, audited alongside this header) perform neither check — a
null `renderTarget` or an invalid `cubeMapFace` (e.g. constructed via an explicit out-of-range
`static_cast<CubeMapFace>`) is silently accepted and stored. This is exactly the "missing
bounds/null-check" shape already flagged elsewhere in this audit (`NetworkSessionProperties::Insert`/
`RemoveAt` in the sibling `xna-net` shard) — here on a type consumed directly by
`GraphicsDevice::SetRenderTargets`'s array overload, so a caller-constructed invalid binding could
propagate silently into device state rather than failing fast at construction.

### LOW — undocumented NOXNA-worthy extensions
See Checklist Results above: `arraySlice` and the default constructor are both real, un-tagged
departures from FNA's actual API surface.

## Cross-File Observations
`GraphicsDevice::SetRenderTargets` (outside this batch's file list) is the primary consumer of this
type — worth checking whether it separately re-validates `renderTarget`/`cubeMapFace` when auditing
that file directly, since this constructor currently does not.

## Missing or Weak Tests
Not independently located in this pass; tests constructing a `RenderTargetBinding` with a null
texture or an out-of-range `CubeMapFace` and expecting a thrown exception would exercise this gap
directly.

## Positive Findings
The overall shape (texture + array-slice-or-cube-face) is a reasonable, if not fully type-safe,
adaptation of FNA's real API to C++.

## Final Assessment
One MEDIUM finding: missing FNA's real null-check/range-check constructor validation. One LOW
finding: undisclosed NOXNA-worthy API extensions.
