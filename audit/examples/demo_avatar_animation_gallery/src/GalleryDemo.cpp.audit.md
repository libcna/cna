# Audit: examples/demo_avatar_animation_gallery/src/GalleryDemo.cpp

## Metadata
- Source file: `examples/demo_avatar_animation_gallery/src/GalleryDemo.cpp` (278 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_animation_gallery` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises all 31 `AvatarAnimationPreset` values via
  `AvatarAnimationPresetToClipNameEXT`
- Related production code: `AvatarAnimationPresetNamesEXT.hpp`/`.cpp` (already audited as part of
  the `xna-gamerservices` shard, including its own `AvatarAnimationPresetNamesEXTTests`)

## Purpose
Implements `AllPresetsInOrder()` (all 31 presets via the same stringizing-macro pattern used by the
production `AvatarAnimationPresetNamesEXTTests`), the gender-compatibility-skip/advance state
machine, and the F1 help overlay.

## Executive Verdict
Correct, no findings. `AdvanceToNextCompatiblePreset()`'s `for (;;)` loop is provably terminating:
`IsCompatibleWithCurrentGender()` treats every non-`Female*`/non-`Male*` clip name as
gender-neutral (line 134), and the preset list contains 11 such gender-neutral entries (`Stand0`-
`Stand7`, `Clap`, `Wave`, `Celebrate`) — so regardless of `currentGender_`, the loop cannot advance
more than 2 full passes (62 iterations) before finding a compatible preset, even in a hypothetical
edge case; in practice it finds one within at most 20 (one gender's incompatible run).

## Checklist Results
- `AllPresetsInOrder()` uses the enum's own declaration order via the `PRESET(x)` stringizing macro
  — the same anti-drift technique already confirmed correct in the production
  `AvatarAnimationPresetNamesEXTTests` (audited earlier this session as part of `tests-xna-
  gamerservices`), reused here rather than hand-typed, so this list cannot silently drift out of
  sync with the real enum.
- `IsCompatibleWithCurrentGender()`'s prefix check (`clipName.rfind("Female", 0) == 0`) correctly
  uses the idiomatic C++ "starts-with" idiom (`rfind(needle, 0) == 0`), not a substring search that
  could false-positive on an embedded "Female"/"Male" elsewhere in a name.
- Both `Update()`'s clip-timeout path and `AdvanceToNextCompatiblePreset()`'s own internal wrap-
  around path independently reload content and flip gender identically — confirmed no logic
  divergence between the two call sites that trigger the same state transition.
- No `NetworkSession`/`GamerServices`-session leak-pattern dependency; no manual bone-weight-
  blending logic.

## Detailed Findings

### LOW (informational) — Preset cycling order differs cosmetically from `AvatarDemo`'s/`DualCompareDemo`'s hand-typed `clipNames_` lists
`AllPresetsInOrder()`'s gender-neutral prefix, in the enum's real declaration order, is `Stand0..
Stand7, Clap, Wave, Celebrate` (line 42: `PRESET(Clap) PRESET(Wave)`). `AvatarDemo.cpp`'s own
hand-typed `clipNames_` (and `DualCompareDemo.cpp`'s per-slot `clipNames`) instead list `Stand0..
Stand7, Wave, Clap, Celebrate` — `Wave` before `Clap`. This has zero functional impact (both lists
contain the same 11 gender-neutral entries; only the display/cycling order differs between demos),
but it does confirm those two files' lists are hand-typed rather than generated from the enum,
unlike this file's own anti-drift `PRESET()` macro approach — worth noting as the more robust
pattern other avatar demos could adopt if they ever need to add a new preset.

## Cross-File Observations
This file is the only one among the 7 avatar-demo shards in this batch that generates its preset
list directly from the enum via a stringizing macro rather than hand-typing it — see the LOW
finding above for the resulting (harmless) cross-file ordering inconsistency.

## Missing or Weak Tests
Not applicable — manual/visual-validation demo; its own `--smoke` mode's summary print
(`played=%d skipped=%d genderSwitches=%d (expected played+skipped >= 31)`) is itself a light
self-check, not a full automated assertion, but a reasonable one for a demo of this kind.

## Positive Findings
Reusing the exact enum-order stringizing-macro technique already validated in the production
`AvatarAnimationPresetNamesEXTTests` is a genuinely good anti-drift practice — this list cannot
silently miss a newly-added preset the way a hand-typed list could.

## Final Assessment
One LOW/informational finding: a harmless cosmetic cycling-order difference from sibling demos'
hand-typed lists, which also highlights this file's more robust enum-driven approach.
