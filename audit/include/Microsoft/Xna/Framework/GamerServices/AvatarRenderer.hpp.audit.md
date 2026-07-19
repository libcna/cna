# Audit: include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp`
- Audit status: AUDITED (full read, 252 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type (the faithful `Draw`/`State`/`BindPose`/transform-matrix API
  surface) plus a substantial `NOXNA` real-rendering extension (`EnableRealRenderingEXT`,
  `DrawRealEXT`, `SetAppearanceEXT`) with no XNA equivalent at all; FNA has no reference material
  for either half
- Main related tests: referenced by name in the private `AvatarRendererTestAccess` friend
  declaration's own comment ("Task 13.1... Grants direct access for thorough Pants/Shoes/
  skin-fallback/case-sensitivity/substring-collision coverage") — not independently located in
  this pass

## Purpose
Renders a 3D avatar model. The faithful XNA-facing half (`Draw`, `getStateProperty`,
`getBindPoseProperty`, world/view/projection/lighting properties) matches real XNA's
`AvatarRenderer`, which never actually renders anything off-Xbox. The `NOXNA` half is a genuine,
functional CNA extension: real GPU-skinned mesh rendering via a loaded `Graphics::SkinnedModelEXT`
and `Graphics::SkinnedEffect`.

## Executive Verdict
Correct, and one of the most substantively documented files in this Avatar sub-family. The class
doc comment states plainly: "The real XNA implementation's constructors never actually read their
AvatarDescription... arguments — every instance ends up in an identical, permanently
AvatarRendererState::Unavailable state, since State's getter unconditionally forces itself to
Unavailable on every single read." This is confirmed consistent with `getBindPoseProperty()`'s own
doc comment (which correctly notes it throws `InvalidOperationException` "always the case in
practice, since State never becomes Ready").

## Checklist Results
- Doxygen coverage: complete, including the private `PartTintEXT` helper and the
  `AvatarRendererTestAccess` friend declaration's rationale.
- `NOXNA` tagging: correctly applied to `EnableRealRenderingEXT`, `IsRealRenderingEnabledEXT`,
  `SetAppearanceEXT`, `DrawRealEXT`, `PartTintEXT`, the destructor (declared out-of-line so
  `std::unique_ptr<Graphics::SkinnedEffect>` can destroy a complete type — a real, necessary C++
  reason with no C# analogue), and the `AvatarRendererTestAccess` friend.
- Exception contracts: `Draw(IAvatarAnimation*)` documents `ArgumentNullException`;
  `Draw(vector, expression)` documents `ArgumentException` for a wrong-length vector;
  `EnableRealRenderingEXT`/`SetAppearanceEXT`/`DrawRealEXT`/`getBindPoseProperty`/
  `getStateProperty` all document `ObjectDisposedException` — every one confirmed matching the
  `.cpp`.
- Resource ownership (checked per this task's directive): `realDevice_` is a non-owning raw
  pointer to an externally-owned `GraphicsDevice` (correct — this class never constructs or frees
  a device); `realModel_` is `std::shared_ptr<Graphics::SkinnedModelEXT>` (correct — shared
  ownership of a loaded, potentially-multi-instance-referenced mesh asset); `realEffect_` is
  `std::unique_ptr<Graphics::SkinnedEffect>`, exclusively owned and freed in `Dispose()` — no
  ownership ambiguity found.

## Detailed Findings
None.

## Cross-File Observations
- `AvatarRenderer.cpp`'s `DrawRealEXT` calls `realModel_->ComputeBoneTransformsEXT(...)` — the
  actual joint-weight/bone-skinning math lives in `Graphics::SkinnedModelEXT`, **not** in this
  file. This project's persistent memory records a multi-round avatar-rendering remediation
  history (joint-weight blending selecting an "infinite slab," a flat-cap garment redesign
  attempt later reverted as measured-worse) — that work concerns `SkinnedModelEXT`'s bone-weight
  computation, which is out of scope for this `xna-gamerservices` pass. `SkinnedModelEXT` should
  be checked for `.audit.md` coverage under its own shard (`xna-graphics`, since it lives under
  `Microsoft::Xna::Framework::Graphics`) if not already audited.
- `AvatarAppearanceEXT` (audited separately) is the type `PartTintEXT`/`SetAppearanceEXT` operate
  on — its own audit report documents the concrete, dated `shoesColor_` remediation this file's
  rendering output depends on.
- The `AvatarRendererTestAccess` friend's own comment references "Task 13.1" and explicitly
  motivates the friend declaration as filling a real coverage gap (`PartTintEXT`'s substring-match
  routing otherwise only reachable through a GPU-dependent `DrawRealEXT` + pixel-readback test) —
  a reasonable, narrowly-scoped test-only widening of encapsulation.

## Missing or Weak Tests
Not independently located in this pass; the `AvatarRendererTestAccess` friend strongly suggests a
dedicated non-GPU test exists for `PartTintEXT`'s substring-match behavior, but the test file
itself was not read.

## Positive Findings
The class doc comment's direct, unhedged statement of the "always Unavailable" behavior — including
naming exactly which getter forces it and why — is an excellent example of documenting a
counter-intuitive contract precisely enough that a future maintainer won't mistake it for a latent
bug. The ownership model for the three real-rendering resource handles (raw/shared/unique,
matched to actual ownership semantics) is textbook-correct.

## Final Assessment
No findings.
