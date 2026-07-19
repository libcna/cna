# Audit: include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp`
- Audit status: AUDITED (full read, 145 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Represents a single achievement and its earned state (description, gamerscore, earned
date/time, key, name).

## Executive Verdict
Correct. Full documented property set present (`Description`, `DisplayBeforeEarned`,
`EarnedDateTime`, `EarnedOnline`, `GamerScore`, `HowToEarn`, `IsEarned`, `Key`, `Name`,
`GetPicture`). `GetPicture()` throws `System::NotImplementedException`, honestly disclosed as
matching a genuine platform-unavailability gap (real Xbox 360 achievement artwork was streamed
from Xbox LIVE at request time; there is no local equivalent to substitute) rather than a
CNA-side omission.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` usage: correctly applied to `operator==`/`operator!=` (required by
  `AchievementCollection`'s `IndexOf`/`Contains`/`Remove`, not real XNA members) and to
  `CreateInternal`.
- Visibility: constructor is `private`, only reachable via `CreateInternal` — matches real XNA's
  own internal-construction-only pattern for this type.

## Detailed Findings
None.

## Cross-File Observations
`operator==`'s doc comment explicitly parallels `LeaderboardEntry::operator==`'s reasoning (both
audited in this same pass): FNA's own type has no custom equality (falls back to reference
identity), and since this port stores the type by value, structural field comparison is the
closest achievable equivalent — the same accepted, disclosed divergence pattern in both files.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `GetPicture()` NotImplementedException is grounded in a specific, plausible real-platform
reason (no local equivalent to Xbox LIVE-streamed artwork) rather than a vague "not implemented."

## Final Assessment
No findings.
