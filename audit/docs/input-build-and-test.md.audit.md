# Audit: docs/input-build-and-test.md

## Metadata
- Source file: `docs/input-build-and-test.md` (245 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (build/test how-to + authoritative test-count baseline)
- XNA/FNA relevance: build/CI/test-count reference for `Microsoft::Xna::Framework::Input`
- Related audit: `xna-input`/`tests-xna-input`/`cna-input` shards (this session)

## Purpose
Explains dependency bootstrap (submodules + sibling `sharp-runtime`/`easy-gl` repos), configure/
build/run commands, the canonical `ctest -L input` determinism gate, the authoritative test-count
baseline (524 passed, updated 2026-07-17), a fresh-clone reproducibility note, and an honest account
of a **known, disclosed, out-of-Input-scope full-suite crash**.

## Executive Verdict
Accurate and unusually transparent about a real, separate defect. Its own account of `plans/plan_input.md`
P9-031 finding the **unfiltered full `CnaTests` binary crashes with `double free or corruption
(fasttop)` (SIGABRT) inside `ENetBackendTest`** — explicitly confirmed unrelated to Input via
isolation testing, not present in any Input-filtered run including under ASan+UBSan — is a real,
significant, and honestly disclosed finding that this document correctly flags as "a real, separate,
out-of-Input-scope memory-safety defect requiring dedicated bisection," rather than silently omitting
it or downplaying it because it isn't this document's own subsystem's fault.

## Checklist Results
- The "single source of truth" claim (`CNA_INPUT_TEST_FILTER` in `CMakeLists.txt`; `ctest -L input`
  as the stable command) is a specific, checkable claim; this document explicitly warns against
  hand-copying a `--gtest_filter` string since it "drifts" — a real, well-reasoned caution.
- The test-count table's own footnote is a model of honest epistemic humility: "The unfiltered
  full-suite count above is deliberately NOT restated as a stable N-passed/N-failed figure, because
  it is no longer accurate to give one" — refusing to restate a now-unreliable number rather than
  quietly carrying forward a stale one.
- The headless run inventory (§"Headless run inventory") giving a precise breakdown of exactly which
  `MouseCursor` cases fail/skip under `dummy` vs. `x11` video drivers is specific and verifiable,
  consistent with SDL's own documented cursor-creation requirements.
- The troubleshooting table (missing submodule/sibling-repo remedies, ASan `libGLX_mesa` leak
  attribution, Wayland `SDL_GetGlobalMouseState` note) matches claims made independently in
  `docs/input-manual-verification-results.md` (read in this batch) about the same ASan/Wayland
  behaviors — cross-consistent.

## Detailed Findings
None in this document's own content — the ENet crash it discloses is itself the finding, and this
document handles the disclosure correctly (attributes it precisely, scopes it out of Input, points
to `plans/plan_input.md`'s P9-031 record and `NEXT.md` for the bisection).

## Cross-File Observations
- The ENet-subsystem full-suite crash this document discloses was not independently cross-checked
  against `tests-cna-internal`'s own `ENetBackendTests.cpp` audit (already completed this session,
  praised as "2083 lines, exhaustive pending-send-queue state-machine coverage," no HIGH finding
  recorded there) — worth flagging: the crash is described here as needing "~800 preceding tests'
  allocation history to manifest," so its absence from that dedicated `ENetBackendTests.cpp` audit is
  expected (a corruption bug surfacing only after a long allocation history wouldn't necessarily be
  caught by that file's own isolated test run) and is not a contradiction.
- Cross-consistent with `docs/input-manual-verification-results.md`'s ASan/Wayland claims (both read
  in this batch).

## Missing or Weak Tests
The document itself identifies the real test-coverage/reliability gap here: the full unfiltered
`CnaTests` suite cannot currently produce a trustworthy pass/fail count due to the disclosed
ENet-related corruption crash — a genuine, already-flagged gap, not one this audit is newly
surfacing.

## Positive Findings
Refusing to restate a stale "full suite N/N" figure once it's known to be unreliable, and precisely
attributing a crash to a different subsystem via isolation testing rather than either hiding it or
misattributing it to Input, are both excellent examples of documentation honesty under pressure to
just report *a* number.

## Final Assessment
No findings against this document itself. It correctly and transparently discloses a real,
significant, separately-tracked memory-safety defect (`ENetBackendTest`-adjacent full-suite
corruption crash) that is out of Input's own scope but affects any full-suite run — worth ensuring
this is tracked to resolution given its severity (SIGABRT, full-suite-blocking).
