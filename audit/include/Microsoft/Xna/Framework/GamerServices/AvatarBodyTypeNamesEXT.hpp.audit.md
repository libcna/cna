# Audit: include/Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp`
- Audit status: AUDITED (full read, 33 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA extension; not part of the XNA 4.0 API
- Main related tests: not independently located in this pass

## Purpose
Declares `AvatarBodyTypeToContentNameEXT()`, mapping an `AvatarBodyType` to the `ContentManager`
asset name for the corresponding procedurally-generated avatar body model.

## Executive Verdict
Correct. The doc comment precisely and correctly explains *why* this mapping cannot be derived
from `AvatarDescription` automatically: `AvatarDescription::getBodyTypeProperty()` always lazily
defaults to `Female` (a faithfully-preserved real XNA quirk — the 1021-byte format is never
actually populated), so a caller must supply the desired `AvatarBodyType` through some other
channel (e.g. its own player-selection UI) rather than reading it back out of a
freshly-constructed/random `AvatarDescription`.

## Checklist Results
- `NOXNA` tagging: correct.
- Exception contract: documents `System::ArgumentException` for an unrecognized value — confirmed
  matching the `.cpp`.

## Detailed Findings
None.

## Cross-File Observations
See `include/Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp.audit.md` for the
cross-referenced `getBodyTypeProperty()` stub behavior this doc comment explains the workaround
for.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Clearly documents a real, non-obvious API-design constraint (why this mapping can't be automatic)
rather than leaving a reader to wonder why an `AvatarDescription`-based overload doesn't exist.

## Final Assessment
No findings.
