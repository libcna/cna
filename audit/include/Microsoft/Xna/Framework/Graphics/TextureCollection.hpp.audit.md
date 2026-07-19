# Audit: include/Microsoft/Xna/Framework/Graphics/TextureCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/TextureCollection.hpp`
- Audit status: AUDITED (full read, 46 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/TextureCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
A fixed-size (`MaxTextures = 16`) collection of texture-sampler-slot bindings, exposed as
`GraphicsDevice.Textures`/`VertexTextures`.

## Executive Verdict
Mostly correct, but missing one real FNA safety check (a DEBUG-only guard in FNA itself — see
Detailed Findings) and using C++ operator overloads (`operator[]`/`operator()`) instead of FNA's
single C# indexer with distinct get/set bodies — a reasonable, disclosed-by-necessity C++
adaptation, not itself a defect.

## Checklist Results
- `MaxTextures = 16` matches FNA's real slot-count convention (constructed with `slots` = 16 for the
  main texture stage in `GraphicsDevice`, confirmed by convention elsewhere in this codebase, not
  independently re-verified against `GraphicsDevice.cpp` in this pass since it's outside this batch's
  file list).
- `RemoveDisposedTexture` (declared here, implemented in the `.cpp`) matches FNA's own
  `internal void RemoveDisposedTexture(Texture tex)` semantics: clears every slot currently holding
  the given pointer.

## Detailed Findings

### MEDIUM (capped by FNA's own DEBUG-only scope) — missing FNA's render-target/texture *conflict*
check when binding a texture
FNA's real indexer setter (`TextureCollection.cs` lines 26-53) is wrapped in `#if DEBUG` and, beyond
the disposed check this project's own `.cpp` correctly ports, ALSO checks whether the texture being
bound is simultaneously bound as an active render target on the same device (guarded by an
`ignoreTargets` internal flag `GraphicsDevice` sets when it legitimately needs to bind a render
target as a sampler texture internally, e.g. resolving multisampling): if not `ignoreTargets` and the
value is currently one of `GraphicsDevice`'s own `renderTargetBindings`, FNA throws
`InvalidOperationException("The render target must not be set on the device when it is used as a
texture.")`. This project's `TextureCollection::operator()` (audited in the paired `.cpp`) has no
equivalent check at all. Since FNA itself only enforces this in `DEBUG` builds (not a Release-mode
guarantee even in real XNA/FNA), this is capped at MEDIUM rather than HIGH — but it is a genuine,
real safety net this codebase currently lacks entirely (in any build configuration), for a mistake
class (reading and writing the same GPU resource in the same draw) that is undefined behavior on
real GPUs, not merely an XNA-API nicety.

## Cross-File Observations
This check would need cooperation from `GraphicsDevice` (to expose its current render-target
bindings and an `ignoreTargets`-equivalent internal flag) — out of this batch's file list
(`GraphicsDevice.hpp`/`.cpp` belong to a different `xna-graphics` sub-batch); flagging here for
whoever audits `GraphicsDevice` directly, since the fix is not localizable to this file alone.

## Missing or Weak Tests
Not independently located in this pass; a test that binds a currently-active render target as a
sampler texture and expects a thrown exception would exercise this gap directly.

## Positive Findings
The disposed-texture check that IS present is correctly implemented and uses the project's own
`System::ObjectDisposedException` (see the paired `.cpp` report).

## Final Assessment
One MEDIUM finding: missing render-target/sampler conflict check (a real, if FNA-DEBUG-only-scoped,
safety net).
