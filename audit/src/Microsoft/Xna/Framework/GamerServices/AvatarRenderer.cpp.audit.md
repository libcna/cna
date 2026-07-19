# Audit: src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp`
- Audit status: AUDITED (full read, 242 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type (faithful half) plus a substantial `NOXNA` real-rendering
  extension; FNA has no reference material for either half
- Main related tests: not independently located in this pass

## Purpose
Implements `AvatarRenderer`'s constructors, the always-`Unavailable` state machine, the validated
no-op `Draw` overloads, and the real GPU-skinned `EnableRealRenderingEXT`/`SetAppearanceEXT`/
`DrawRealEXT`/`PartTintEXT` extension.

## Executive Verdict
Correct, and confirms every behavioral claim made in the paired header. `kParentBoneIds` (lines
22-26) is a 71-entry table — manually counted and confirmed to match `BoneCount = 71` exactly, with
`-1` at index 0 (the root bone) and every other entry referencing a strictly-lower index (a valid
tree-shaped parent-pointer structure, consistent with a real skeletal hierarchy — no entry
references itself or a later, not-yet-defined bone). `getStateProperty()` (lines 79-89)
unconditionally sets `state_ = AvatarRendererState::Unavailable` on every call before returning it
— confirmed this is a genuine "forces itself" behavior, not a one-time lazy default, exactly as the
header's class-level remarks state. `getBindPoseProperty()` (lines 62-77) checks the raw `state_`
field directly rather than calling `getStateProperty()` (which would force it to `Unavailable`
first) — a subtle, correct implementation-order detail explicitly called out in an inline comment,
confirming this matches the real XNA implementation's exact order of operations rather than an
accidentally-different check.

## Checklist Results
- `Draw(IAvatarAnimation*)` (lines 102-115): null-checks `animation` via `ArgumentNullException`
  before dereferencing — inline comment cites "Task 1.5" as the specific fix that closed a prior
  undefined-behavior gap (unconditional dereference of a null animation).
- `Draw(vector, expression)` (lines 117-128): checks `isDisposed_` first, then validates
  `bones.size() == BoneCount` via `ArgumentException` — correct order (disposed-check before
  argument-validation, matching this project's established convention elsewhere).
- `EnableRealRenderingEXT` (lines 130-152): checks `isDisposed_` (inline comment cites "Task 11.6"
  as the fix for a prior gap where this call silently "undisposed" the object by
  re-populating `realDevice_`/`realModel_`/`realEffect_` after `Dispose()`), then null-checks
  `model` via `ArgumentNullException` (inline comment cites "Task 1.6" as the fix for a prior
  poor-diagnostic gap where a null model only surfaced later, inside `DrawRealEXT`, as a misleading
  `InvalidOperationException`) — both fixes independently confirmed present and correctly ordered
  (disposed-check before null-check).
- `SetAppearanceEXT` (lines 159-167): same Task 11.6 disposed-check fix, confirmed present.
- `DrawRealEXT` (lines 178-228): checks `isDisposed_`, then `IsRealRenderingEnabledEXT()` via
  `InvalidOperationException` — correct order. The lighting-setup sequence (`EnableDefaultLighting()`
  called *before* the custom ambient/key-light overrides, not after) is explicitly documented via
  inline comment as fixing a real, previously-diagnosed visual defect (ambient light silently reset
  to XNA's dim ~0.05-0.18 default after being set to the intended ~0.35, causing joints/creases to
  render near-black) — confirmed the code's actual call order (`EnableDefaultLighting()` at line
  206, `setAmbientLightColorProperty()` at line 207) matches the documented fix.
- `Dispose(bool)` (lines 235-241): resets all three real-rendering handles
  (`realEffect_.reset()`, `realModel_.reset()`, `realDevice_ = nullptr`) — correctly releases the
  owned `unique_ptr`/`shared_ptr` and clears the non-owning raw pointer, consistent with the
  ownership model confirmed in the paired header's audit.

## Detailed Findings
None.

## Cross-File Observations
`PartTintEXT` (lines 169-176) does simple ordered substring matching (`Hair` → `Shirt` → `Pants` →
`Shoes` → fallback to skin tint) against a `SkinnedModelEXT` part's name — matches
`AvatarAppearanceEXT`'s own documented substring-match design (see its audit report). Since the
checks are sequential `if` returns (not `else if`, but functionally equivalent — the first match
wins and the function returns immediately), a part name containing multiple keywords (e.g. a
hypothetical "ShirtPants" mesh name) would resolve to whichever keyword's `if` appears first in
source order (`Hair` > `Shirt` > `Pants` > `Shoes`) — a reasonable, low-risk tie-breaking rule for
content-pipeline-controlled naming, not flagged as a defect given the content pipeline is the
single source of truth for these names (per `AvatarAppearanceEXT`'s own doc comment).

## Missing or Weak Tests
Not independently located in this pass; per the header's `AvatarRendererTestAccess` friend comment,
some non-GPU test coverage for `PartTintEXT`'s substring routing likely exists but was not located.

## Positive Findings
Three distinct, specifically-tracked defect fixes (Task 1.5, 1.6, 11.6) are all confirmed correctly
implemented in this file, each with a clear inline explanation of what the prior bug was and why
the current ordering fixes it — plus a fourth, non-task-numbered but still concretely documented
lighting-order fix (`EnableDefaultLighting()` before custom overrides) with a specific, plausible
visual-symptom description (near-black joints/creases) rather than a vague "improved lighting"
claim.

## Final Assessment
No findings.
