# Audit: examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp

## Metadata
- Source file: `examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp` (247 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_wardrobe_hotswap` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `ContentManager::Unload()`/`Load()`, `SkinnedModelEXT::
  AttachPartEXT`'s replace-by-name semantics

## Purpose
Implements `ApplyHairState()` (state 0: `Unload()` + fresh reload of the base avatar; states 1/2:
`AttachPartEXT` a wardrobe hair piece, relying on its replace-by-name semantics to free the old
part) and `ConfigureRenderer()` (rebuilds `renderer_` around whatever `model_` currently is).

## Executive Verdict
Correct, no findings. `ApplyHairState(0)`'s `content.Unload()` followed immediately by a fresh
`content.Load<...>(AvatarBodyTypeToContentNameEXT(gender_))` correctly produces a genuinely fresh
`model_` (a new `shared_ptr`, replacing the old one, whose prior contents — including any attached
wardrobe parts — are simply dropped along with the old `shared_ptr`, not explicitly cleaned up
piece-by-piece, which is correct: there's no `RemovePartEXT` loop needed here since the whole model
object is being replaced wholesale). `renderer_` is likewise reassigned via `ConfigureRenderer()`'s
`renderer_ = std::make_unique<AvatarRenderer>(...)`, correctly destroying the previous
`AvatarRenderer` via `unique_ptr`'s move-assignment before constructing the new one — no leak.

## Checklist Results
- Comment (lines 114-117) explicitly documents that `AttachPartEXT`'s replace-by-name semantics
  removes the old `"CNAAvatarHair"` part "before attaching the new one — no manual removal needed
  here, unlike the workaround `AvatarDemo.cpp` needed before that fix landed" — an accurate,
  specific cross-reference to a real historical fix, consistent with the identical claim in
  `AvatarDemo.cpp`'s own comment (already confirmed in that file's audit).
- `content.Unload()` in the `state == 0` branch only affects `ContentManager`'s own cache; since
  this demo doesn't load anything else via `ContentManager` (font/whitePixel/spriteBatch are all
  constructed directly, not loaded), there's no unintended side effect on other cached assets.
- No `NetworkSession`/`GamerServices`-session dependency; no manual bone-weight-blending logic.

## Detailed Findings
None.

## Cross-File Observations
Provides a second, independent confirmation (alongside `AvatarDemo.cpp`) that
`AttachPartEXT`'s replace-by-name fix genuinely eliminated a previously-needed manual-removal
workaround — worth noting for anyone auditing `SkinnedModelEXT::AttachPartEXT` itself (already
covered in the `xna-graphics` shard) as corroborating evidence from two independent demo-consumer
call sites.

## Missing or Weak Tests
Not applicable — manual/visual-validation demo; the smoke-test summary print
(`finalHairState=%s`) is a light self-check.

## Positive Findings
Correct, careful reasoning about content-cache lifetime and object replacement semantics, with
historical context (the removed manual-removal workaround) preserved for future readers.

## Final Assessment
No findings.
