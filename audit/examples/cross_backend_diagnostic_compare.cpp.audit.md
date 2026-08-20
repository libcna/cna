# Audit: examples/cross_backend_diagnostic_compare.cpp

## Metadata

- Source file: `examples/cross_backend_diagnostic_compare.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — standalone comparator tool for the cross-backend
  diagnostic pair (`plans/plan_software.md` SOFTWARE-61/SOFTWARE-84).
- File type: standalone `main()` executable (`cna_diag_compare`), **not** a `Game` subclass, **not**
  CTest-registered (`cmake/Harnesses.cmake:150-152`: `if(CNA_BUILD_TESTS) add_executable(cna_diag_compare
  examples/cross_backend_diagnostic_compare.cpp) endif()` — no `add_test`/`cna_register_backend_test`
  call anywhere for this target). Confirmed by `grep`: no occurrence of `cna_diag_compare` in any
  `add_test`/`cna_register_backend_test` call across `cmake/`.
- XNA/FNA relevance: none directly — this file has zero XNA/CNA/SharpRuntime includes or
  dependencies by design (see Purpose).
- FNA reference: N/A (tooling, not an XNA API surface file).
- Related production code: none — deliberately decoupled from `CNA`/`SHARP_RUNTIME` (confirmed:
  `add_executable(cna_diag_compare …)` has no `target_link_libraries` call at all in
  `Harnesses.cmake`, unlike every CNA-linked test in the same file). Consumes the raw dump format
  produced by the companion file `examples/cross_backend_diagnostic_scene.cpp` (audited separately).

## Purpose

A tiny, dependency-free CLI diff tool: reads two files each expected to be exactly
`64*64*4 = 16384` bytes (`kExpectedBytes`, `kSize=64`), computes the per-pixel/per-channel absolute
difference, and prints `max diff`, the `(x,y,channel)` location of that max, and the `mean diff`.
Exits `1` if `maxDiff > tolerance` (default `40`, overridable via `argv[3]`), `0` otherwise; exits
`2` on usage/IO errors (file open failure, wrong file size). Its own top-of-file comment states the
intent precisely: "standalone comparator for `cross_backend_diagnostic_scene`'s dumps... reads two
raw 64x64 RGBA8 files and reports the per-channel max/mean absolute difference, exiting 1 if the max
exceeds the given tolerance." This matches the implementation exactly — no XNA/CNA namespace
involvement is correct here since the file's whole point (per the doc's "no CNA/SHARP_RUNTIME
dependency" design note) is to compare backend output *independently* of the framework that produced
it, so a bug in a shared CNA header couldn't corrupt the comparator itself. Placement directly under
`examples/` (not `tools/`) matches the sibling `cross_backend_diagnostic_scene.cpp`'s placement and
the project's own `examples/` convention for backend-diagnostic executables noted in
`AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — a small, correctly-scoped, dependency-free diff utility that does exactly what its
header comment and `docs/software-backend.md` describe; no XNA/FNA surface to check parity against,
and the arithmetic is simple enough to verify by inspection with no defects found.

## Checklist Results

### API / XNA / FNA parity
N/A — pure standalone tool with no XNA namespace involvement, consistent with `AUDIT_CHECKLIST.md`'s
guidance to mark this section `N/A` for non-`Microsoft::Xna`/`Microsoft::Devices` files.

### Behavioral correctness
- `ReadFile()` (lines 20-38): opens in binary mode, allocates exactly `kExpectedBytes` up front,
  `fread`s that many bytes, and validates `read == kExpectedBytes` before returning — a short read
  (truncated file) or failed open both `exit(2)` with a diagnostic to `stderr`, not a silent
  zero-filled buffer. Correctly closes the file (`fclose`) before either success or the
  size-mismatch failure path, so no leaked `FILE*` on either branch.
- `main()` (lines 41-86): `argc < 3` prints usage and returns `2` — correct minimum-arg guard for
  the two mandatory file paths. `tolerance` optionally parsed from `argv[3]` via `std::atoi`
  (no error handling for a non-numeric `argv[3]`, but `atoi`'s own contract of returning `0` for an
  unparsable string is a benign degenerate case here — a `tolerance=0` just makes the comparison
  strict rather than crashing).
- The nested triple loop (lines 56-73) walks `y`, `x`, `c` in that order and computes
  `idx = (y*kSize + x)*4 + c` — matches the RGBA-interleaved, row-major layout that
  `cross_backend_diagnostic_scene.cpp` actually writes (`rgba[i*4+0..3] = R,G,B,A` for
  `i = row-major pixel index`), so the two files' byte layouts genuinely line up; verified by
  reading both files rather than assuming.
- `maxDiff`/`maxDiffX/Y/channel` are only updated on a **strict** `diff > maxDiff` (line 66), so the
  reported location is the *first* occurrence of the maximum in scan order when there are ties —
  a reasonable, documented-by-omission tie-break (doesn't need documenting; it's the conventional
  "first max wins" behavior and does not affect the pass/fail verdict, only the diagnostic
  location string).
- `meanDiff` divides `sumDiff` (a `long`, avoiding the `int` overflow that could occur if it were
  `int` — `16384 * 255 = 4,177,920`, which actually still fits in a 32-bit `int`, but using `long`
  here is a safety margin, not a bug) by `kExpectedBytes` (cast to `double`) — correct mean-per-byte
  computation matching the comment's "mean diff" semantics (i.e. across all 16384 R/G/B/A byte
  samples, not per-pixel).

### Logic
Straightforward nested loop with no early-exit optimization (the whole 64×64×4 array is always
scanned even after a tolerance-busting diff is found) — appropriate for a diagnostic tool where the
full max/mean report is the point, not early-exit performance.

### C++ correctness
- `std::abs(static_cast<int>(a[idx]) - static_cast<int>(b[idx]))` (line 64) correctly widens the
  `uint8_t` operands to `int` before subtracting, avoiding the classic `unsigned - unsigned`
  underflow bug that would otherwise wrap a negative difference into a huge unsigned value.
- No raw owning pointers; `std::vector<uint8_t>` return-by-value from `ReadFile()` relies on
  guaranteed copy elision / move, no manual memory management.
- `std::FILE*` handles are always paired with `fclose` on every path examined (success and the
  short-read failure both close before returning/exiting).

### Memory/resource lifetime
No leaks found: the one dynamically-sized resource (`std::vector<uint8_t> data(kExpectedBytes)`)
is stack-scoped and returned by value; `FILE*` is closed on both the happy path and the
short-read-detected failure path (`std::fclose(f)` on line 30 executes unconditionally before the
subsequent `read != kExpectedBytes` check on line 31, so even the failure branch has already closed
the handle by the time it calls `std::exit(2)`).

### Performance
`likely-significant`: none — 64×64×4 = 16384 iterations is trivial and this is an offline
diagnostic tool, not a hot path. `theoretical`: the triple-nested loop with manual index arithmetic
could be flattened to a single loop over `kExpectedBytes` since `ReadFile` already treats both files
as flat byte buffers of that exact size — but the current form intentionally preserves `(x,y,channel)`
for the diagnostic printout, so the nesting is purposeful, not accidental inefficiency.

### Thread safety
N/A — single-threaded `main()`, no shared/global mutable state beyond `main`'s own locals.

### Architecture
Correctly decoupled from CNA/SharpRuntime per its own documented design goal — confirmed by
`Harnesses.cmake`'s `add_executable(cna_diag_compare examples/cross_backend_diagnostic_compare.cpp)`
having no `target_link_libraries` call at all (every other test target in the same file links `CNA`
and/or `SHARP_RUNTIME`). This means a hypothetical bug in `Color`'s packed layout, `GraphicsDevice`,
or any CNA header could never mask or fake this comparator's own diff result — a deliberate,
well-reasoned isolation choice for a tool whose entire job is to be an independent oracle over CNA's
output.

### Maintainability
Small (87 lines), single-responsibility, no magic numbers beyond the documented `kSize`/tolerance
default (both named/commented). No dead code, no TODO/FIXME markers.

### Portability
Uses only `<cstdio>`/`<cstdlib>`/`<cmath>`/`<cstdint>`/`<vector>` — no platform-specific APIs;
`std::fopen(path, "rb")` is portable (binary mode explicitly requested, avoiding Windows text-mode
CRLF translation issues if this tool is ever built there, even though it's currently only exercised
on Linux per `AUDIT_DECISIONS.md` D-P4).

### Robustness
Both `argc < 3` and per-file open/size-mismatch errors are handled with a clear `stderr` message and
a distinct non-zero exit code (`2`, distinguishing "usage/IO error" from "comparison failed" (`1`))
— a genuinely useful convention for a script that wraps this tool (per `docs/software-backend.md`'s
own invocation example).

### Testing
This file has no test of its own (it *is* a comparison tool, not something exercised by a `Game`
harness) and, per `AUDIT_DECISIONS.md`/`cmake/Harnesses.cmake`'s own comment, is deliberately **not**
registered as a CTest — it's designed to be invoked manually or by an external script per
`docs/software-backend.md`'s "Cross-backend diagnostic" section, which was independently read and
confirmed to describe exactly this file's actual behavior and invocation contract (3-command
sequence: build+dump backend A, build+dump backend B, run `cna_diag_compare` on the two dumps).
There is, notably, no automated regression test that this comparator itself computes the right
answer (e.g. a unit test feeding it two known-different synthetic buffers and checking the exit
code) — a legitimate, if minor, testing gap for a file whose own correctness (as verified above by
manual inspection) matters for every future cross-backend regression check that might rely on it.

### Cross-file consistency
Verified against its actual counterpart, `cross_backend_diagnostic_scene.cpp`: the RGBA byte order
this tool assumes (`R,G,B,A` per pixel, row-major, 4 bytes/pixel, 64×64) exactly matches that file's
own dump-writing loop (`rgba[i*4+0]=R, +1=G, +2=B, +3=A`), confirmed by opening and reading both
files rather than trusting either's comment in isolation.

## Detailed Findings

None found — no CRITICAL/HIGH/MEDIUM/LOW defects identified after full line-by-line reading.

## Cross-File Observations

- This file and `cross_backend_diagnostic_scene.cpp` are a matched producer/consumer pair; both were
  read to verify their byte-layout contract agrees (see Cross-file consistency above). Neither
  currently has any backend able to run both halves of a genuine A/B comparison automatically —
  only `SOFTWARE` and `EASYGL` currently build the scene-dump half (`cmake/Tests/SoftwareTests.cmake:45`,
  `cmake/Tests/EasyGLTests.cmake:38`), so a full cross-backend diagnostic today can only ever compare
  those two backends against each other, despite the design intending it to generalize to any
  backend (see that file's own audit for detail).

## Missing or Weak Tests

- No automated unit test exists for this comparator's own diff/exit-code logic (e.g. feeding it two
  synthetically-constructed 16384-byte buffers with a known planted difference and asserting the
  reported `maxDiff`/exit code). Low priority given the tool's simplicity and the fact this audit's
  own manual trace confirms its correctness, but worth noting since this tool is itself relied upon
  by the cross-backend diagnostic workflow described in `docs/software-backend.md`.

## Positive Findings

- Genuinely dependency-free as documented — verified via `Harnesses.cmake`, not just the file's own
  comment.
- Correct `uint8_t`→`int` widening before subtraction avoids an easy-to-miss unsigned-underflow bug.
- Clear, distinct exit codes (`0`/`1`/`2`) for pass/fail/usage-error, which is exactly what a
  calling script needs to distinguish "comparison failed" from "comparison couldn't even run."

## Final Assessment

A small, correct, well-isolated diagnostic tool. No defects found; its only weakness is the absence
of a self-test for its own diff logic, which is a minor, low-priority gap given how straightforward
the logic is and how thoroughly it was traced by hand in this audit.
