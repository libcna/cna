# AUDIT_CHECKLIST.md — Master Per-File Inspection Checklist

Every per-file audit report (`audit/<path>.audit.md`) must show evidence against the applicable sections below.
Use `PASS` / `WARNING` / `FAIL` / `N/A` / `NOT VERIFIED` per section. A `PASS` must cite something concrete
(a symbol, a line region, a test name, an FNA file path) — never bare boilerplate. Sections that don't apply to a
given file type (e.g. "Memory/resource lifetime" for a `.cmake` file) are `N/A`, not silently omitted.

This is a condensed, file-report-facing version of the full checklist in the audit prompt (sections A–O there);
nothing here narrows that scope, it's an operational checklist for writing each report consistently.

## Sections (map to per-file report headings)

1. **Purpose** — one paragraph: what is this file responsible for, is placement/namespace correct.
2. **API / XNA / FNA parity** *(Microsoft::Xna / Microsoft::Devices files only, else N/A)* — names, overloads,
   signatures, return/param types, defaults, enum/const values, visibility, static/instance, property mapping,
   events/delegates, interfaces/inheritance vs. the FNA reference tree at
   `/rv/data/library/github.com/FNA-XNA/FNA`. Distinguish XNA-compatible API / FNA implementation detail / CNA
   implementation detail / intentional `NOXNA` extension.
3. **Behavioral correctness** — normal, boundary, invalid-argument, empty/zero-sized, disposed-object,
   repeated-call, ordering, exception, state-transition, lifetime, device-loss/reset behavior, as applicable.
4. **Logic** — conditions, loops, state transitions, cache invalidation, enum/switch completeness, coordinate
   conversion, math, bounds, resource sync, unreachable paths, silently-ignored errors.
5. **Memory/resource lifetime** — ownership, RAII, raw pointers, UAF/double-free risk, leaks, GPU/native handle
   lifetime, shutdown/destruction order, exception-path safety, move/copy behavior.
6. **C++ correctness** — UB, lifetime, aliasing, casts, overflow, signed/unsigned, uninitialized values, virtual
   destructors, slicing, `noexcept`, constness, span/view lifetime, static-init order.
7. **Performance** — allocations in hot paths, unnecessary copies, missed moves, O(N²)+, redundant GPU
   upload/sync/readback, redundant state changes. Separate likely-significant / potentially-significant /
   theoretical.
8. **Thread safety** *(where applicable, else N/A)* — races, lock ordering, callback threading, shutdown races.
9. **Architecture** — layering (XNA API vs. CNA internal vs. backend), backend leakage, coupling, global
   state/singleton use, dependency direction.
10. **Maintainability** — size/complexity vs. legitimate cohesion, duplication, magic numbers, dead/commented-out
    code, `TODO`/`FIXME`/stub markers, naming.
11. **Portability** *(where applicable)* — platform/compiler/endian/pointer-size assumptions, conditional
    compilation correctness.
12. **Robustness** — input validation, malformed data, device/allocation failure handling, error propagation vs.
    silent fallback.
13. **Testing** — what test file(s) cover this, whether they validate semantics or just compile/run-without-crash,
    missing boundary/error/parity/lifecycle coverage. (Tests themselves get audited as their own file too — this
    section is about coverage *of* the file being reported on.)
14. **Cross-file consistency** — header/impl pair, callers/callees, base/derived, sibling backends, FNA equivalent.

## Severity scale (for Detailed Findings)

`CRITICAL` (sparingly — data corruption, crash-always, security) · `HIGH` (wrong behavior on a common path) ·
`MEDIUM` (wrong behavior on an edge case, or a real architectural/perf risk) · `LOW` (minor correctness/style/perf)
· `INFO` (observation, not a defect).

## Confidence scale

`HIGH` (read the code and can state the failing input/sequence) · `MEDIUM` (strong pattern match to a known-bad
shape, not fully traced) · `LOW` (plausible concern, needs runtime verification to confirm).

## Verdict vocabulary (Executive Verdict)

`Healthy` · `Mostly healthy` · `Needs attention` · `Significant correctness risk` · `Major compatibility gap` ·
`Major architectural concern` · `Incomplete/stub implementation`. Always justified with a one-line reason tied to
a concrete finding or the absence of one after genuine inspection.

## Anti-boilerplate rule

A report is invalid if it could have been written without opening the file. For any non-trivial C++ file: name the
actual classes/methods inspected, quote or paraphrase the actual logic for at least the file's primary
responsibility, and cite line regions or symbol names for every finding. Trivial files (e.g. a 15-line enum header)
may have a proportionally short report — brevity there is correct, not a shortcut.
