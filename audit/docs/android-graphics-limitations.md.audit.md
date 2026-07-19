# Audit: docs/android-graphics-limitations.md

## Metadata
- Source file: `docs/android-graphics-limitations.md` (91 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown status write-up
- XNA/FNA relevance: Android graphics-backend buildability, not XNA API surface directly

## Purpose
Documents that the Android cross-compile currently fails before reaching any CNA graphics code, due
to two `sharp-runtime` (a separate sibling repo) NDK-portability bugs, and honestly scopes what is
and isn't verified as a result.

## Executive Verdict
Exceptionally disciplined about scope and evidence: it re-ran the exact documented cross-compile
command live rather than trusting a prior dated write-up, explicitly calls out this as "a regression
against `docs/devices-build.md`'s own dated write-up" once it found the discrepancy, and stops short
of speculating about `SdlGraphicsBackend.cpp`'s own Android buildability since the build never reaches
it. This is the correct epistemic posture for a "what's actually broken and what's merely unknown"
document.

## Checklist Results
- The claim that both failing files (`FileStream.cpp`, `FileSystemInfo.cpp`) belong to `sharp-runtime`,
  not this repository, is consistent with this project's own established policy (memory: "do not fix
  bugs discovered in sharp-runtime... [that's a] separate repo with its own CLAUDE.md/git history" —
  also stated in `docs/devices-build.md`'s own TSan section). Correctly out of scope for this repo's
  audit.
- Correctly distinguishes "SDL_RENDERER is genuinely selected as the Android default" (verified) from
  "SdlGraphicsBackend.cpp compiles for Android" (NOT verified) — a precise, not overstated, claim
  boundary.

## Detailed Findings
None — the document's claims are internally consistent and appropriately scoped; the underlying
`sharp-runtime` bugs are out of this audit's authorized scope (a separate repository).

## Cross-File Observations
Consistent with `docs/devices-build.md`'s own Android section (Section 4/4.1), which documents a
*successful* Android cross-compile and APK build for `Microsoft::Devices` specifically — no
contradiction, since that success predates (2026-07-05/06) this document's own re-run (implied later,
given it's flagged as "a regression"), and the two documents cover different targets (`CNA`+`SHARP_RUNTIME`
static-library compile here vs. the full `cna_demo_devices` executable/APK there, which apparently
still built even after this regression — worth noting as a slightly surprising combination, since a
failing `SHARP_RUNTIME` compile 4/6 files) would be expected to block any Android target depending on
it, `cna_demo_devices` included. This document does not attempt to reconcile that apparent tension
with `devices-build.md`'s success story; a reader relying on both docs together might reasonably ask
whether `devices-build.md`'s own Android build steps still succeed today, or whether they were run
before this specific `sharp-runtime` regression landed. Not independently resolvable from either
document's own text — worth flagging as an open question for whoever next touches Android build docs.

## Missing or Weak Tests
N/A — a build-status document, not describing testable application code.

## Positive Findings
The explicit statement of what remains "NOT verified, currently unknown" (4 distinct bullets) rather
than silently assuming success once the immediate blocker is described, is a strong documentation
practice, matching the project's own general "no silent claims" discipline seen elsewhere in this
audit.

## Final Assessment
No direct findings against this document's own claims. One flagged cross-file tension (this doc's
"Android build currently broken" vs. `devices-build.md`'s "Android build succeeded" — likely a
simple time-ordering issue, not a contradiction, but not explicitly reconciled by either document).
