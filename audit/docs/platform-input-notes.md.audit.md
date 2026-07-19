# Audit: docs/platform-input-notes.md

## Metadata
- Source file: `docs/platform-input-notes.md` (199 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (part of the INP-0003 input-doc family)
- Cross-references: `tests-xna-input`/`xna-input` shard audits (no contradicting finding); `input`
  shard is part of `xna-input`/`cna-input` (already fully audited earlier this session)

## Purpose
Catalogs platform-specific input behavior (X11/Wayland/Windows/macOS/Android/iOS/Emscripten) CNA
inherits from SDL3, plus cross-cutting concerns (logical-coordinate mouse-warp scaling, non-US
keyboard layout handling, gamepad backend/mapping).

## Executive Verdict
A precise, well-organized reference distinguishing "verified" (confirmed directly during input work,
cited by task) from "documented SDL3/OS platform behavior CNA inherits" (not independently
re-verified, taken as given). No overclaiming found — the one place a limitation exists (Wayland
global-cursor-position, non-ASCII key dropping), it's explicitly framed as a platform/XNA-API
constraint, not a CNA bug, with the reasoning stated each time.

## Checklist Results
- The Wayland global-cursor-position limitation is correctly attributed to "Wayland's compositor
  security model," not a CNA gap, with the exact workaround given (`Mouse::GetState()` reports
  window-local coordinates, unaffected) — a precise, actionable distinction for a porting reader.
- The "Mapping gap — accented/non-ASCII keys have no XNA `Keys`" section makes a specific, falsifiable
  behavioral claim: CNA drops such keys entirely in keycode mode (never enters the pressed set),
  deliberately diverging from FNA's own `Keys.None`-and-add convention — explicitly named as a
  deliberate CNA policy choice ("the DEC-16 policy"), with the exact test that pins it
  (`NonUsLayoutAccentedKeysAreUnmappedInKeycodeMode`) cited.
- The cursor/warp platform matrix (INPUT-MOUSE-022) is consistent with its own per-platform prose
  sections above it — cross-checked no contradiction between the compact table and the detailed
  narrative for any of the 5 platform rows.
- The Steam Input / virtual-controller section's claim that CNA "remaps the re-exposed controller to
  a fixed GUID... mirroring `SDL3_FNAPlatform.cs:2193-2210`" cites a specific FNA source location for
  the behavior being matched — a strong, falsifiable provenance claim (not independently re-verified
  against that exact FNA line range by this pass, but the citation style itself is the kind of
  specific attribution this audit values).

## Detailed Findings
None. All claims are either explicitly marked "verified" with a task citation, or explicitly framed
as inherited SDL3/OS behavior not independently re-verified — no claim overstates its own confidence
level.

## Cross-File Observations
Consistent with the `xna-input`/`tests-xna-input` shard audits (completed earlier this session),
which found the `Buttons`(31)/`Keys`(160) enum-value mapping exhaustively correct against FNA — this
document's Steam/Valve-GUID-override and non-US-keyboard-drop claims are a natural, non-contradicting
extension of that same verified mapping-fidelity work into platform-specific edge cases.

## Missing or Weak Tests
N/A for a documentation file — the doc's own citations (`EverySdlButtonMapsToTheExpectedXnaButton`,
`GetGuidUsesVendorProductAndValveOverrides`, `NonUsLayoutAccentedKeysAreUnmappedInKeycodeMode`, etc.)
suggest each specific behavioral claim has a corresponding pinned test, consistent with what this
audit found reviewing the `tests-xna-input` shard directly.

## Positive Findings
The "verified vs. inherited-and-not-reverified" distinction, applied consistently line-by-line
throughout, is a model example of honest documentation calibration — it lets a reader immediately
tell which claims to trust as CNA-specific engineering fact versus which are "this is what SDL3/the
OS does, we haven't re-derived it ourselves." The non-US-keyboard section's precise enumeration of
which accented characters are dropped (German `ä ö ü ß`, French `é è à ç`, Czech `ě š č`) versus
which resolve to a real `Keys` value via physical position (Nordic `æ`/`ø`) is unusually specific,
falsifiable detail for a documentation file.

## Final Assessment
No findings. A precise, well-calibrated reference document consistent with this audit's own
independent review of the `xna-input`/`tests-xna-input` shards.
