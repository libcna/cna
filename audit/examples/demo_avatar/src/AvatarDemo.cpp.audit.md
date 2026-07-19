# Audit: examples/demo_avatar/src/AvatarDemo.cpp

## Metadata
- Source file: `examples/demo_avatar/src/AvatarDemo.cpp` (283 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `AvatarRenderer::EnableRealRenderingEXT`/`SetAppearanceEXT`/
  `DrawRealEXT`, `SkinnedModelEXT::AttachPartEXT`, `ContentManager::Load<shared_ptr<SkinnedModelEXT>>`
- Related production code: `AvatarRenderer.cpp` (ambient-light-color remediation history cited
  directly in this file's own comments — see below)

## Purpose
Implements avatar loading (with optional wardrobe-hair attach), an orbiting camera, Space-key clip
cycling, and an F1 help overlay.

## Executive Verdict
Correct, no findings, and this file's own comments provide genuinely valuable, independently
verifiable historical context for a previously-audited production fix. Lines 137-144's comment
directly documents the `audit_net.md` remediation history for `EasyGL`'s skinned-shader
double-ambient-multiplication bug (`EmissiveColor * DiffuseColor` applied twice, crushing dark
materials) and explains precisely why `setAmbientLightColorProperty` was reverted from a
compensating `0.5` back to the "real" `0.35` once the shader fix landed — citing
`scripts/avatar_visual_regression_check.py` (already audited this session) as the actual
measurement tool used to confirm `0.35` is strictly better post-fix.

## Checklist Results
- `clipNames_` construction (lines 54-78) appends `FemaleIdle*`/`Female{Angry,Confused,...}` or
  `MaleIdle*`/`Male{Angry,Confused,...}` depending on `bodyType_` — correctly gender-gated, matching
  the header's own documented design.
- `AttachPartEXT` (line 117) is correctly documented as having "replace-by-name semantics" that
  free the old part's GPU resources — "no manual workaround needed here anymore," an explicit,
  positive confirmation that an older workaround was removed once the underlying fix landed (cross-
  referenced again in `HotswapDemo.cpp`'s own near-identical comment).
- `Update()`'s `smokeFramesLeft_ > 0` (not `>= 0`) guard (line 204) is explicitly commented as
  matching "every other smoke-testable demo's own convention" — confirmed true across every other
  file in this batch.
- `Draw()`'s screenshot-capture timing (`smokeFramesLeft_ == 1`, not `== 0`) is explained precisely:
  `Game::Exit()`'s `suppressDraw_` flag skips the final `Draw()` call entirely, so capturing at `0`
  would never run — capturing at `1` (one frame before `Exit()` fires) is the only frame guaranteed
  to both contain the just-rendered content and still execute `Draw()`. Confirmed logically sound.
- No `NetworkSession`/`GamerServices` session/leak-pattern dependency.
- No manual bone-weight-blending logic (only consumes `AvatarRenderer::DrawRealEXT`, no vertex
  weight math in this file) — not a candidate for the "infinite slab" bug class.

## Detailed Findings
None.

## Cross-File Observations
This file's `renderer_->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f))` comment
(lines 137-144) is a genuinely valuable, independently-checkable historical record of the
`audit_net.md` remediation's "fourth round" finding (per this session's persistent project memory:
`EasyGL`'s skinned shaders multiplied `EmissiveColor` by `DiffuseColor` a second time) — this file
corroborates that finding's resolution from the demo-consumer side, not just the shader-source side.
The identical `0.35f` value and near-identical light-direction/light-color setup is repeated
verbatim across every sibling avatar demo in this batch (`GalleryDemo`, `TintStudioDemo`,
`DualCompareDemo`, `StressDemo`, `HotswapDemo`) — consistent, not divergent, configuration.

## Missing or Weak Tests
Not applicable — manual/visual-validation demo with a `--smoke`/`--screenshot` CI mode.

## Positive Findings
The ambient-light-color comment is an excellent example of a demo file preserving load-bearing
historical context for a real, previously-fixed production bug — future maintainers touching this
value have enough information to know *why* it's `0.35` and not some other number, and what
regression to watch for if they change it.

## Final Assessment
No findings.
