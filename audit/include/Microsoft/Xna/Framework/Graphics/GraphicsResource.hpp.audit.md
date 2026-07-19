# Audit: include/Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp` (93 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/GraphicsResource.cs` (209 lines)
- Main related tests: not independently located in this pass

## Purpose
Abstract base class for all graphics resources (textures, buffers, render targets, etc.):
device/name/tag identity, disposal lifecycle, and the `Disposing` event.

## Executive Verdict
Mostly correct, with one confirmed structural gap: this type has no way to reassign a resource to a
different `GraphicsDevice` after construction, unlike FNA's real `GraphicsResource.GraphicsDevice`
property, which has an `internal set` used by at least one real resource type
(`VertexDeclaration`) to move between devices during its lifetime.

## Checklist Results
- Doxygen coverage: complete.
- Copy semantics (lines 67-75): explicitly documented as carrying device/name/tag but resetting
  `isDisposed_`/event subscribers — a deliberate, disclosed design choice (each copy owns its own
  lifecycle), not an oversight.
- `Dispose(bool disposing)` correctly documented as needing derived-class overrides for
  resource-specific cleanup, with native resources released "regardless of the @p disposing flag"
  — matches FNA's own documented contract for this pattern.

## Detailed Findings

### LOW-MEDIUM — no public/protected way to reassign `graphicsDevice_` after construction
FNA's real `GraphicsResource.GraphicsDevice` property (`GraphicsResource.cs` lines 22-53) has an
`internal set` accessor explicitly designed to let a resource move between devices during its
lifetime — its own comment states: "`VertexDeclaration` objects can be bound to multiple
`GraphicsDevice` objects during their lifetime. But only one `GraphicsDevice` should retain
ownership," and the setter correctly removes the resource's reference from its old device before
attaching to the new one. This port's `GraphicsResource` has only a getter
(`getGraphicsDeviceProperty()`) and no equivalent setter at all — `graphicsDevice_` is fixed for the
object's lifetime once set in the constructor. If this codebase's own `VertexDeclaration` (or any
other resource type sharable across devices) ever needs the same real cross-device-reassignment
behavior FNA supports, there is currently no mechanism for it in this base class.

**Not independently confirmed as a live, reachable bug** in this pass (would require checking
whether `VertexDeclaration`'s own actual usage in this codebase ever needs cross-device
reassignment — out of this batch's file list, in the `vertex_packed` group of this same shard).
Recorded as a structural gap worth checking there.

## Cross-File Observations
See the paired `.cpp` report for confirmation that the constructor's registration behavior
(`AddResourceReference`/`OnResourceCreated` called unconditionally when a device is supplied) is a
reasonable C++ structural adaptation of FNA's separate constructor/property-setter registration
split, not a bug.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The copy-semantics documentation (lines 67-75) is a clear, deliberate design decision correctly
explained rather than left as an unexplained divergence from FNA's own non-copyable resource model.

## Final Assessment
One LOW-MEDIUM structural finding (missing device-reassignment capability); worth checking against
`VertexDeclaration`'s actual usage when that file is audited.
