# Audit: include/CNA/Internal/CnjEnvelope.hpp

## Metadata
- Source file: `include/CNA/Internal/CnjEnvelope.hpp`
- Audit status: AUDITED (full read, 199 lines, header-only)
- Subsystem: `cna-internal-core` shard
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: N/A — this is CNA's own NOXNA content-metadata envelope format (`.cnj`, documented in
  `cnj.md`), not part of XNA 4.0
- Main related tests: not independently located in this pass

## Purpose
Parses and validates the top-level fields (`cnjVersion`/`type`/`sourceFile`) of CNA's own `.cnj` JSON content
sidecar format, used by `ContentManager`'s `.cnj`-based readers (e.g. `SkinnedModelTypeReader`) to dispatch to
the correct per-type reader and enforce a supported schema version.

## Executive Verdict
Healthy — correct, defensively written, and well-documented.

## Checklist Results

### Behavioral correctness
`ParseCnjEnvelope()` is a pure, non-throwing extraction function exactly as documented: a JSON parse failure
or non-object root is recorded in `parseErrorDetail` (not thrown), and each field's own `hasXxx` flag is only
set when the field is both present and of the expected type — a numeric `cnjVersion` sets both the truncated
`int` and the raw `double` (kept separately so `ValidateCnjEnvelopeBaseline()` can reject a non-integer
version like `1.5` rather than silently truncating it to `1`, a genuinely useful design choice called out
directly in the header comment).

`ValidateCnjEnvelopeBaseline()`/`ValidateCnjEnvelope()` correctly layer: baseline checks well-formedness +
supported version + presence of `type`; the outer function adds an exact `type` equality check against the
caller's expected type. The version check (`envelope.cnjVersionRaw != kSupportedCnjVersion` against the exact
literal `1.0`) is safe against floating-point-comparison concerns here specifically because the only value
that can produce this exact double is a JSON literal `1` or `1.0` parsed through `Json.hpp`'s own correct
number parser (verified in this shard's `Json.hpp` audit) — there is no accumulated floating-point arithmetic
upstream that could produce an inexact `1.0`.

### C++ correctness / Memory/resource lifetime / Thread safety / Portability / Maintainability / Robustness
No issues found — pure functions over value types, no shared/mutable state, no resource ownership.

## Detailed Findings
None.

## Cross-File Observations
Directly depends on `Json.hpp` (already audited this shard, confirmed correct) and
`ContentLoadException` (Microsoft::Xna::Framework::Content) for its only failure signaling mechanism — a
correct choice, since this NOXNA content-pipeline-adjacent code should still raise the same exception type
XNA content loading already uses.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `cnjVersion` truncated-int-vs-raw-double split is a small but genuinely thoughtful defensive design
choice, explicitly there to prevent a subtly-wrong version like `1.5` from being silently accepted as `1`.

## Final Assessment
No issues found.
