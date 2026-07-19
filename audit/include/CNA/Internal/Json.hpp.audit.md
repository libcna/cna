# Audit: include/CNA/Internal/Json.hpp

## Metadata
- Source file: `include/CNA/Internal/Json.hpp`
- Audit status: AUDITED (full read of the recursive-descent parser, string escape/unescape, number
  parsing, and serializer — 595 lines, header-only)
- Subsystem: `cna-internal-core` shard
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: internal JSON parse/serialize utility backing `LocalGamerServicesStore.cpp` (already
  audited, confirmed correct consumer) and presumably other local-persistence use sites
- Main related tests: not independently located in this pass

## Purpose
A minimal, hand-rolled, header-only JSON parser (`ParseJson`) and compact serializer (`WriteJson`) covering
"just enough of the grammar" for this codebase's own local-persistence needs (objects/arrays/strings/numbers/
booleans/null).

## Executive Verdict
Healthy — a correct, carefully-implemented parser and serializer, independently verified against the JSON
spec in the areas checked.

## Checklist Results

### Confirmed correct: string escape/unescape
Every standard JSON escape (`\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`) is handled; unescaped control
characters (`< 0x20`) are correctly rejected during parsing, and correctly re-escaped as `\u%04x` during
writing — a genuine round-trip-safety property, not just a one-directional convenience.

### Confirmed correct: `\uXXXX` surrogate-pair handling
High-surrogate (`0xD800..0xDBFF`) detection, tentative low-surrogate lookahead, and the
`codepoint = 0x10000 + ((high-0xD800)<<10) + (low-0xDC00)` combining formula were independently verified
against the standard UTF-16-surrogate-pair-to-codepoint algorithm — exact match. A high surrogate not
followed by a valid low surrogate correctly backs off (`pos_ = save`) rather than consuming/misinterpreting
the next token.

### Confirmed correct: number parsing
Strictly follows the JSON number grammar (optional `-`, no leading zeros except a bare `0`, optional `.`
fraction requiring ≥1 digit, optional `e`/`E` exponent requiring ≥1 digit) and additionally rejects a number
immediately followed by another alnum/`.`/`_` character — a deliberate anti-truncation safeguard (so
`"1abc"` is a clean parse error, not a silently-truncated `1` followed by unconsumed garbage that would
otherwise surface as a confusing downstream error).

### Confirmed correct: serializer round-trip fidelity
`WriteJsonValue()`'s number formatting writes exact-round-tripping integers without a spurious trailing
`.0` (checked via `n == (double)(long long)n && abs(n) < 1e18`, a safe bound within both `long long`'s and
double's exact-integer-representable ranges) and falls back to `%.17g` — the standard IEEE-754-double
round-trip-safe precision — for everything else.

### C++ correctness / Memory/resource lifetime / Performance / Portability / Maintainability / Robustness
No issues found. `JsonParser` holds `text_` by reference (the caller's string must outlive parsing) — a
reasonable, standard pattern for a synchronous, non-escaping parse call (`ParseJson()`'s own signature takes
the text by `const std::string&` and returns before any reference could dangle).

## Detailed Findings
None.

## Cross-File Observations
Already confirmed as a correct, safely-used dependency of `LocalGamerServicesStore.cpp` (audited earlier in
this shard) — that file's own "never throws on corrupt data" contract relies on `JsonParseException` being
thrown (and caught) rather than any silent-wrong-data failure mode, consistent with what's confirmed here.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Independently verified correct against the JSON spec for string escaping (including the trickier surrogate-
pair case), number grammar, and round-trip-safe serialization; a thoughtful anti-truncation safeguard on
number parsing not required by the spec but genuinely useful for catching malformed input early.

## Final Assessment
No issues found.
