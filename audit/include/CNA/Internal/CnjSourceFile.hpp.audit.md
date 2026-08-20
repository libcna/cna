# Audit: include/CNA/Internal/CnjSourceFile.hpp

## Metadata
- Source file: `include/CNA/Internal/CnjSourceFile.hpp`
- Audit status: AUDITED (full read, 140 lines, header-only)
- Subsystem: `cna-internal-core` shard
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: N/A — NOXNA `.cnj` content-metadata support (plans/plan_cnj.md CNB-32)
- Main related tests: not independently located in this pass

## Purpose
Safely resolves a `.cnj` envelope's `"sourceFile"` field (naming another asset the `.cnj` document is
metadata for) to a root-relative logical asset name, enforcing that the resolved target cannot escape the
content root and cannot itself be another `.cnj` file (no sidecar chaining).

## Executive Verdict
Healthy — a genuinely careful path-traversal defense, correctly implemented.

## Checklist Results

### Path-traversal defense: correctly implemented, not just claimed
The containment check (`canonicalRoot` vs `canonicalJoined`) walks both paths component-by-component via
`std::filesystem::path` iterators rather than comparing raw path strings. This deliberately avoids the classic
prefix-check bug where a raw-string `starts_with()` comparison would wrongly treat `/content/foo-secret` as
"inside" root `/content/foo` (see Positive Findings) — verified correct by hand-tracing both the matching and
non-matching cases.

`weakly_canonical()` (rather than `canonical()`) is the right choice for a path whose leaf may not exist yet
at this point in the pipeline: it resolves symlinks for the longest existing prefix and lexically normalizes
the remaining (non-existent) suffix, so a symlink placed anywhere along an *existing* portion of the path is
still correctly followed before the containment check runs.

Absolute-path rejection, empty-string rejection, explicit-`.cnj`-extension rejection, and the implicit-sibling
check (appending `.cnj` to the resolved path and checking existence, to catch `sourceFile: "foo"` silently
resolving to a `foo.cnj` sidecar through the "normal resolver") are all present and match the header's own
documented invariants exactly.

### Minor observation (not scored as a defect): TOCTOU window
Because this function only resolves and validates a path (returning strings), the actual file open happens
later in the caller. A filesystem race (e.g. an attacker swapping a symlink between this validation and the
later open) could in principle defeat the check. This is a generic, hard-to-close-without-`openat`/`O_NOFOLLOW`
race inherent to any validate-then-open pattern, and is consistent with this project's trust model (a local
desktop game engine loading its own bundled content, not an adversarial multi-tenant service) — noted here for
completeness, not scored as an actionable defect.

### Minor observation: case-sensitive component comparison
The component comparison (`*joinedIt != *rootIt`) is case-sensitive even on platforms with case-insensitive
filesystems (Windows, default macOS). This can only produce a false-negative (rejecting a legitimately-inside
path that differs only in case) — a portability/robustness nit, not a security gap, since it never
loosens the containment check.

## Detailed Findings
None rising to actionable severity — see the two minor observations above (documented for completeness).

## Cross-File Observations
Complements `CnjEnvelope.hpp` (parses the raw `sourceFile` string out of the JSON document; this file safely
resolves it) — a clean separation of "parse" from "validate-and-resolve."

## Missing or Weak Tests
Not independently located in this pass; given the security-sensitive nature of this function (path-traversal
defense), dedicated tests for each rejection path (absolute path, `..` escape, symlink escape, explicit `.cnj`,
implicit `.cnj` sibling) would be valuable if not already present.

## Positive Findings
Correctly avoids the classic raw-string-prefix path-containment bug by using `std::filesystem::path`
component iteration instead of string comparison — a detail that is easy to get wrong and was gotten right
here.

## Final Assessment
No issues found; two minor, non-actionable observations documented above.
