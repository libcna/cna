# Audit: docs/input-public-api-frozen.md

## Metadata
- Source file: `docs/input-public-api-frozen.md` (346 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (golden signature snapshot, compile-guard companion)
- XNA/FNA relevance: the canonical STRICT/FNA-compatible/FNAEXT/NOXNA/INTERNAL API-tier
  classification for every public `Microsoft::Xna::Framework::Input`(`::Touch`) member
- Related audit: `xna-input` shard (this session)

## Purpose
The human-readable companion to `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`
(which pins every member below via a compile-time signature check) — defines the canonical API-tier
glossary (STRICT/FNA-compatible/FNAEXT/NOXNA/INTERNAL) and enumerates every public member of the
`GamePad`, `Keyboard`/`Mouse`/`MouseCursor`/`TextInputEXT`, and `TouchPanel`/`TouchCollection`/
`TouchLocation`/`TouchPanelCapabilities`/`GestureSample` clusters with its tier tag.

## Executive Verdict
Excellent, and enforced rather than merely descriptive — this document's own header states plainly
that any drift (a removed/renamed member, a changed signature) **fails to compile**, via the freeze
test it's paired with. This is a fundamentally different (and stronger) reliability guarantee than
almost every other document in this shard, most of which can only go stale silently. Its own
"Audit result (2026-07-05)" section records a real, small, self-found defect (`GamePadState()`/
`GestureSample()` default constructors were untagged while their 9 sibling value-struct default
constructors were correctly `NOXNA`) and its fix — a good demonstration of the tagging convention
being actively enforced, not just declared.

## Checklist Results
- The EXT/NOXNA tagging convention (§"EXT / NOXNA tagging convention") gives precise, example-backed
  rules for every category (non-XNA enum value gets `EXT` suffix only, not `NOXNA`; non-XNA
  non-enum member gets `NOXNA` and, if FNA-compatible, also `EXT`; whole non-XNA classes are
  `NOXNA` at the class level) — internally consistent with every per-cluster table that follows it
  (spot-checked: `GamePad::GetGUIDEXT` — EXT only, no bare `NOXNA`, correctly following the
  "FNA-compatible extension carries EXT" rule since it's a real FNA/MonoGame-family extension, not a
  CNA-only convenience).
- The `GetTypeName()` exemption section (INPUT-API-029) gives a specific, mechanically-enforced
  claim (a `static_assert(!std::is_base_of_v<System::Object, T>)` block over 18 public Input types)
  — consistent with this project's CLAUDE.md rule that concrete `System::Object` subclasses must
  override `GetTypeName()`, correctly reasoning that Input types are all non-`Object` (value
  structs/static classes/enums, with `MouseCursor`'s sole base being `IDisposable`, not `Object`)
  and therefore legitimately exempt.
- The "Deliberately excluded" list (internal SDL-bridge/test plumbing exposed as public but not
  frozen as API) is a sound, well-reasoned scoping decision — keeping the golden file decoupled from
  internal churn that isn't really part of the API contract.

## Detailed Findings
None.

## Cross-File Observations
Directly the basis for `docs/input-member-parity-matrix.md`'s own tier tags (spot-checked in this
batch: `GamePadState`, `TouchPanel`, `TouchLocation` cluster tables in the parity matrix use
identical STRICT/EXT/NOXNA tiering to this document's own per-member tables) — fully consistent
between the two, as expected given the parity matrix is generated partly from these same headers.

## Missing or Weak Tests
N/A — the document's own enforcement mechanism (the compile-time freeze test) is the test; not
independently re-run in this pass (out of scope for a docs-only audit; `tests-xna-input` shard
already covers this file's test-side counterpart).

## Positive Findings
Pairing a human-readable golden-signature document with a compile-time enforcement test is the
single most robust anti-staleness mechanism found anywhere in this session's `docs/` shard audit —
every other document in this shard can only be caught stale by a human noticing a drift; this one
cannot drift undetected at all, by construction.

## Final Assessment
No findings. The most robustly anti-stale document format encountered in the entire `docs/` shard.
