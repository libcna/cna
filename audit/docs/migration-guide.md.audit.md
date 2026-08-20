# Audit: docs/migration-guide.md

## Metadata
- Source file: `docs/migration-guide.md` (349 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (Task 486, `plans/plan_graphics.md` Phase 54, plus later Tasks 487-489)
- Cross-references: `docs/xna-4-api-coverage.md`, `docs/graphics-backend-feature-matrix.md`,
  `docs/sdl-renderer-2d-completeness.md` (all referenced, not independently re-audited by this pass)

## Purpose
A practical, backend-selection-oriented porting guide for bringing an existing XNA 4.0/FNA game to
CNA: the property-rewrite mechanical difference, backend selection guidance, the two biggest real
gaps (no `.xnb` pipeline, no compiled `.fx` bytecode), a caveats list (with fixed items struck
through and dated), and two detailed per-member compatibility checklists (2D/3D) plus a
troubleshooting section.

## Executive Verdict
An unusually disciplined "living document" — struck-through, dated caveat entries ("~~EasyGL:
anisotropic filtering~~ — fixed, Task 918 (2026-07-09)") are kept rather than deleted, explicitly
"so old bookmarks/searches still find them," and every checklist row cites the specific task that
verified it against the real current header/FNA source rather than general knowledge. No overclaiming
found; several claims are independently corroborated by this audit's own `xna-graphics`/`tests-xna-graphics`
shard reviews.

## Checklist Results
- The `SpriteBatch::Draw(Texture2D, Rectangle destination, Rectangle? source, Color, rotation,
  origin, SpriteEffects, layerDepth)` fix note (Task 922) is a precise, falsifiable claim (a
  previously-`NOXNA`-marked near-equivalent with a required, not optional, source rectangle) —
  consistent with this audit's own `xna-graphics` shard review, which found no contradicting
  overload-shape issue for `SpriteBatch::Draw`.
- The stock-effect caveats table (`AlphaTestEffect`/`DualTextureEffect` missing `VertexColorEnabled`
  on Vulkan/Bgfx, Tasks 887/889) and the "fixed 2026-07-11" `EnvironmentMapEffect`/`SkinnedEffect`
  entries are consistent with the `xna-graphics` shard audit, which did not find any contradicting
  evidence for these specific effects (that shard's own 6 HIGH findings were in `SpriteFont`/
  `SpriteBatch`/`EffectParameter`/`GraphicsException`, a disjoint area).
- The "Troubleshooting" section's claim "Never run multiple backends' full `ctest` suites
  concurrently... confirmed to cause transient GPU/driver-contention false failures" cites `NEXT.md`
  §2 as the source — a reasonable, specific attribution, not an unfounded claim.
- Cross-checked the `EffectParameter` Matrix Get/Set/Transpose semantics finding from the
  `xna-graphics` shard audit (a confirmed HIGH finding: inverted relative to FNA) — **this guide does
  not mention it anywhere**, despite being exactly the kind of "gotcha that would silently break a
  real port" this document otherwise catalogs carefully. See Detailed Findings.
- Cross-checked the `GraphicsException` wrong-exception-type finding (confirmed HIGH, baked into
  `GraphicsExceptionTests.cpp`) — also not mentioned. A game catching `System::Exception` around
  device-lost handling (a common real XNA pattern) would silently fail to catch CNA's
  `std::runtime_error`-derived exceptions instead, exactly the kind of porting trap this guide exists
  to warn about.

## Detailed Findings

### MEDIUM — Guide omits two confirmed HIGH-severity porting traps this audit found in the same subsystem
This document is specifically written to catalog exactly the kind of subtle, easy-to-miss porting
gotcha these two findings represent (an inverted Matrix Get/Set/Transpose convention on
`EffectParameter`; `DeviceLostException`/`DeviceNotResetException`/`NoSuitableGraphicsDeviceException`
deriving from `std::runtime_error` instead of the project's own `System::Exception` hierarchy a real
port's `catch` blocks would likely expect) — yet neither appears in the "What has caveats" list nor
the 3D compatibility checklist, both of which this document maintains with real discipline elsewhere.
This is a document-currency gap, not a code defect (the underlying bugs are already tracked findings
in the `xna-graphics` shard audit) — recommend adding both to the caveats list once confirmed/fixed
upstream.

## Cross-File Observations
Otherwise strongly consistent with this session's own `xna-graphics`/`tests-xna-graphics` shard
audits — no contradicting claim found for any of the ~40 individually-cited member/task claims this
document makes, only the two omissions noted above.

## Missing or Weak Tests
N/A for a documentation file.

## Positive Findings
The struck-through-but-retained fixed-caveat pattern is a genuinely good documentation practice for
a living compatibility guide — it lets a reader who remembers an old caveat confirm it's resolved
without losing the historical context of what was wrong and when it was fixed. The "is it a bug or a
known limitation?" closing section correctly directs a reader to check existing tracked-issue
documents before filing a new report — reduces duplicate-finding churn.

## Final Assessment
One MEDIUM finding: this document — otherwise a careful, currency-maintained catalog of exactly this
class of porting gotcha — omits two confirmed HIGH-severity findings from the same `xna-graphics`
subsystem it already extensively covers (the `EffectParameter` Matrix transpose inversion, and the
`GraphicsException` hierarchy mismatch). No other findings; the rest of the document's ~40 specific
claims are consistent with this audit's own independent review.
