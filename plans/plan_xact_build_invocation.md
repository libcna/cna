# XACTBUILD-001 — genuine XactBld3 host invocation (SAMPLE-122)

Status: **complete**, 2026-10-03. Bounded correction exposed by Catapult's unchanged XACT2 input.
No new XNA API, runtime audio subsystem, Sharp Runtime or sample implementation.

Current BuildXact passed `/X:Windows`, which the SDK treats as an invalid exclusion option, and
passed native absolute Linux paths to a Windows CLI under Wine. Both fail with status87 even on
an intact XACT3 positive control. The genuine SDK usage advertises `/WINDOWS`/`/XBOX360`.
Direct compiler probes distinguish those faults from compiler absence and old input compatibility.

- [x] Map Windows/Xbox360 to the SDK architecture switches; refuse unsupported named targets before
  launching. Preserve the existing unset-property behavior.
- [x] Share the effect compiler's measured Wine path spelling in `CNA::Internal::HostProcess`;
  ask the configured Wine launcher for winepath -w once for compiler/project/output. Other launchers
  and failed/incomplete mappings retain native paths exactly as before. No guessed Z: prefix or shell.
- [x] Keep the effect compiler on the same shared mapping without changing its prior behavior.
- [x] Pin both architectures, intact paths with spaces, CRLF Wine mappings and unsupported-platform
  refusal. Add an optional genuine SDK gate driven by `CNA_XACT_TEST_PROJECT`, `CNA_XACTBLD` and
  `CNA_XACTBLD_LAUNCHER`; copy its entire input directory and prove the project remains unchanged.
- [x] Baseline: 42 existing focused cases pass; all three new invocation regressions fail.
  Final: 45/45 focused task/effect cases pass on private OPENGLES3; the SDK-dependent case is skipped
  only in that generic run, then passes separately on both unchanged ShipGame XACT3 and Catapult XACT2
  inputs (2/2), with native host paths and real XactBld3 9.28.1886.0 under Wine/private Xvfb.

Build: fresh Release OPENGLES3 `CnaContentPipelineTests`, explicit sibling Sharp checkout, shared
ccache/all16 cores, then incremental rebuild. Retained pre-existing optimizer/nodiscard warnings
are in baseline/after logs; no new warning attributed to changed implementation/test code.
The first private runner path exceeded the Unix socket limit; a shorter temp path inside the same
artifact root succeeds. No source/game workaround or inherited-warning cleanup.

Genuine XactBld3 auto-imports Catapult's unchanged Signature=XACT2, Version16, ContentVersion43
project into all three version46 banks. Those outputs and the control's outputs are retained.
Official XNA4 BuildXact itself still rejects the old project. This qualifies the host invocation
and establishes a modern bank route; it does **not** qualify authentic XACT2 binaries, XNA2 game
execution, exact cue playback, original console compilation or a native/browser Catapult product.
The Xbox360 switch is argument-contract tested, not a genuine console build/runtime claim.

Evidence/artifacts/scripts: `/rv/tmp/samples/SAMPLE-122-Catapult_ARCHIVE_2_0/`, particularly
`evidence/current-head-analysis-20261003/`, `cna-native-opengles3-analysis/` and
`scripts/current-head-20261003/{probe-xact3.py,genuine-task-gates.py}`. Preserve all older generations.
See `cna-samples/samples/CatapultArchive/missing.md` for the independent owner product decision.
