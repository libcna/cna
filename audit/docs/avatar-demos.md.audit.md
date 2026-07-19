# Audit: docs/avatar-demos.md

## Metadata
- Source file: `docs/avatar-demos.md` (110 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown quick-reference/troubleshooting document
- XNA/FNA relevance: describes NOXNA avatar demos and Net demo startup troubleshooting

## Purpose
Quick reference for 8 avatar-related demos' controls (deliberately deferring to each demo's own F1
overlay rather than duplicating it) plus troubleshooting for the avatar asset pipeline and Net demo
discovery/startup.

## Executive Verdict
Accurate and well-scoped. Its explicit design choice not to duplicate the F1 help text ("a duplicated
list drifts out of sync the next time a control changes") is a sound documentation practice this
audit has seen pay off negatively elsewhere when violated (e.g. the stale scope-note pattern found in
`demo_achievement_showcase.hpp` and `generate_animations.py`'s own docstring, both audited earlier
this session) — this document avoids that trap by design.

## Checklist Results
- Its "known, still-open residual gaps" callout (shoe-area dark artifact, `Wave`-pose chest-band
  artifact) is directly corroborated by `docs/avatar-real-rendering-ext.md`'s own Phase 7 "Honest
  overall assessment," which names the identical two gaps — internally consistent across documents,
  not just within one.
- The `validate_gltf.py` caveat ("checks presence, not per-vertex correctness... does not catch
  NaN/Inf weight values or out-of-range bone indices") is consistent with this session's own
  `tools-avatar-builder` shard audit of `validate_gltf.py` (already AUDITED), which did not find this
  characterization contradicted.
- The Net-demo troubleshooting section's specific claims (discovery port 61190, `SO_REUSEADDR`,
  `kSearchWindowMs`=150ms/100 retries) read as precise, specific, verifiable claims rather than vague
  gestures — consistent with the level of rigor this session found in the `xna-net`/`tools-net`
  shards' own audited source (`ENetDiscoveryService.cpp`, `NetworkSession::Find`).

## Detailed Findings
None found — no internal inconsistency, and every cross-checkable claim held up against this
session's own independent source-level findings.

## Cross-File Observations
Directly corroborates and is corroborated by `docs/avatar-real-rendering-ext.md`'s Phase 7 section
(same two residual gaps, same mesh-craft pipeline description) — a consistent, non-contradictory pair
of documents describing the same subsystem from different angles (quick-reference vs. architecture).

## Missing or Weak Tests
N/A — a demo/troubleshooting reference document, not describing testable library code directly.

## Positive Findings
The "genuinely new/different-looking artifacts are worth investigating; these two specific ones are
already tracked" framing is a precise, actionable distinction for a future maintainer — it doesn't
just say "known issues exist," it tells the reader exactly how to tell a new bug from an already-known
one.

## Final Assessment
No findings. An accurate, well-scoped, cross-consistent reference document.
