# Audit: include/Microsoft/Xna/Framework/Storage/StorageDevice.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Storage/StorageDevice.hpp`
- Audit status: AUDITED (full read, 186 lines)
- Subsystem: `xna-storage` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Storage/StorageDevice.cs` (356 lines,
  read in full)
- Main related tests: not independently located in this pass

## Purpose
Declares `StorageDevice`: free-space/connectivity queries, the fake-async
`BeginOpenContainer`/`EndOpenContainer` and `BeginShowSelector`/`EndShowSelector` pattern, and
`DeleteContainer`.

## Executive Verdict
Needs attention — a confirmed HIGH-severity finding exists in this class, detailed fully in the
paired `.cpp` report since the header only declares the signature: `DeleteContainer(const
std::string& titleName)` (line 89) is documented here as "Removes the container directory tree for
the given title name entirely," and the implementation genuinely does this via a real recursive
filesystem delete — but with **zero path-containment validation** on the caller-supplied
`titleName`, and unlike every other missing-containment-check finding confirmed elsewhere this
session, **this is not an FNA-faithful gap**: FNA's real `DeleteContainer` is simply `throw new
NotImplementedException();` (`StorageDevice.cs` line 351). CNA chose to actually implement this
method — a reasonable enhancement over FNA's stub — but the implementation is a genuinely new,
CNA-introduced vulnerability: a caller-controlled string drives an unchecked recursive delete.

## Checklist Results

### HIGH: `DeleteContainer`'s doc comment and declared contract give no hint of the missing safety check
Line 89's doc comment ("Removes the container directory tree for the given title name entirely")
accurately describes what the method does, but gives no indication that `titleName` is not
validated to stay within the storage root before the recursive delete proceeds. Full evidence,
including confirmation that FNA itself never implements this method at all (making this a
CNA-original code path, not an FNA port), is in
`src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`.

### Positive: the fake-async pattern is correctly disclosed and verified against FNA's own "Private XNA Lies"
The class doc comment (lines 18-23: "Uses the XNA 4.0 fake-async pattern: BeginXxx completes
synchronously and the paired EndXxx extracts the result") is verified accurate against FNA's own
internal implementation, which literally names this pattern's private helper classes `NotAsyncLie`/
`ShowSelectorLie`/`OpenContainerLie` (`StorageDevice.cs` lines 139-196). This is a good example of
disclosing an FNA-verified behavior clearly.

## Detailed Findings
1. **[HIGH] `DeleteContainer` is a CNA-original (not FNA-faithful) recursive delete with no
   path-containment check on its caller-supplied argument** — declared line 89; full evidence in
   `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`.

## Cross-File Observations
- `StorageContainer`'s own missing-containment-check gap (audited separately) is confirmed
  FNA-faithful and thus lower severity; `DeleteContainer`'s gap is NOT FNA-faithful (FNA doesn't
  implement the method at all) and is a recursive delete rather than a create/open, making it
  materially more severe than the `StorageContainer` finding despite being the "same shape" of bug.

## Missing or Weak Tests
Not independently located in this pass. A test calling `DeleteContainer("../outside")` or an
absolute path and asserting the call is rejected (or at minimum confined to the storage root)
would directly catch finding #1; currently, per the `.cpp` report, no such test could pass.

## Positive Findings
The fake-async pattern's disclosure and the free-space/connectivity property implementations
(verified in the `.cpp` report) are faithful, well-matched ports of FNA's real behavior.

## Final Assessment
One HIGH finding: `DeleteContainer` performs a real, unchecked recursive delete driven by
caller-supplied input, a capability FNA's own implementation doesn't even provide (it throws
`NotImplementedException`). Recommend escalating to `AUDIT_CROSS_CUTTING_FINDINGS.md` given the
concrete data-loss/path-traversal implication.
