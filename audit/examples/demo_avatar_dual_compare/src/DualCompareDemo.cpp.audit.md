# Audit: examples/demo_avatar_dual_compare/src/DualCompareDemo.cpp

## Metadata
- Source file: `examples/demo_avatar_dual_compare/src/DualCompareDemo.cpp` (255 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_dual_compare` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises two independent `AvatarRenderer::SetAppearanceEXT`/`DrawRealEXT`
  instances in the same frame

## Purpose
Implements `LoadSlot()` (loads a `SkinnedModelEXT` + configures a distinct-tinted `AvatarRenderer`
per slot), 1/2-key active-slot selection, Space-key independent clip cycling, and side-by-side
rendering via per-slot `Matrix::CreateTranslation(Vector3(slot.worldX, 0, 0))`.

## Executive Verdict
Correct, no findings. `Draw()`'s `for (AvatarSlot& slot : slots_)` loop correctly sets each slot's
own `World`/`View`/`Projection` and calls `DrawRealEXT` with that slot's own clip/position — genuine
proof that per-`AvatarRenderer`-instance `World`/appearance state does not leak across instances
(both draw with the same shared `view`/`projection` but distinct `World` translations and distinct
tints, and both render correctly positioned side-by-side per the demo's own design).

## Checklist Results
- `slots_[0].worldX = -1.0f` / `slots_[1].worldX = 1.0f` place the two avatars at a fixed, distinct
  offset — a deliberate, simple way to visually separate them.
- Deliberately distinct tint palettes per slot (male: blue-ish shirt; female: pink-ish shirt) — the
  comment (lines 94-96) correctly explains this is "proves `SetAppearanceEXT` is genuine
  per-`AvatarRenderer`-instance state, not shared/global."
- `AdvanceSlotClip(activeSlot_)` and the deterministic smoke-test nudge both correctly index via
  `activeSlot_`/`1 - activeSlot_`, an appropriately simple 2-value toggle for exactly 2 slots.
- No `NetworkSession`/`GamerServices`-session dependency; no manual bone-weight-blending logic
  (rigid pre-baked-content consumption only, same as every sibling avatar demo).

## Detailed Findings
None.

## Cross-File Observations
`slots_[0].clipNames`/`slots_[1].clipNames` list `Stand0..Stand7, Wave, Clap, Celebrate` (`Wave`
before `Clap`) — the same hand-typed ordering already noted in `AvatarDemo.cpp`, and differing
cosmetically (harmlessly) from `GalleryDemo.cpp`'s enum-generated `Clap, Wave` ordering; see that
file's own LOW finding for the full analysis. Not re-flagged here as a separate finding since it's
the same root cause already documented once.

## Missing or Weak Tests
Not applicable — manual/visual-validation demo; its own `--smoke` summary print reports both slots'
final clip names as a light self-check.

## Positive Findings
A well-designed, genuinely informative demo: side-by-side simultaneous rendering with visibly
distinct tints is a real, human-verifiable proof of per-instance state isolation, not just an
assertion in a comment.

## Final Assessment
No findings.
