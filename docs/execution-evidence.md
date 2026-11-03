# CTest execution evidence

`tools/evidence/ctest_evidence.py` is a Python 3 standard-library adapter for one
actual CTest invocation. It reuses `tools/build/build_provenance.py` cache/hash/Git
helpers and CTest's own JSON inventory, JUnit and output log. Existing audit,
provenance and CTest reports remain available; this tool does not replace them.

```bash
python3 tools/evidence/ctest_evidence.py run \
  --build-dir cmake-build-software \
  --output-dir "$EVIDENCE_DIR/ctest-run" \
  -R '^ExecutionEvidenceToolTests$' -L tools \
  --evidence-kind metadata \
  --context 'CPU-only tool checks; no GPU qualification'
python3 tools/evidence/ctest_evidence.py validate "$EVIDENCE_DIR/ctest-run/record.json"
```

Set `EVIDENCE_DIR` to an external evidence directory. The output directory must not exist. Keep it outside the source tree or in an
ignored artifact location. Every invocation retains `record.json`, `configured.json`
(the full `ctest -N --show-only=json-v1` inventory), `selected.json` (the same with
`-R`/`-L`), `ctest.xml`, `ctest.log`, inventory stderr logs and `console.log`.
Commands run sequentially, with the same build/configuration for inventory and
execution. `-C` selects a configuration in a multi-configuration tree. The adapter
does not build targets or change CMake configuration. It inherits the environment;
GPU/window tests still require the repository's private display runner. The example
above selects a CPU-only test.

The small version-1 record contains:

- CNA source path, Git HEAD and dirty flag before/after execution; build path,
  CMake cache SHA-256, generator, build type, configured renderer(s), platform,
  CNA feature options and configured C++ compiler path/ID/version.
- Exact inventory/run commands, `-R`/`-L`/`-C` selection, return codes, UTC start and
  adapter wall time; CTest wall time is retained separately. The full inventory
  retains configured test names, commands and properties (including labels).
- Configured and selected counts, passed, failed, skipped and not-run counts,
  per-test results, skip/failure messages and CTest test output. `executed` counts
  tests with a pass/fail result: `passed + failed`. Skipped and not-run tests never
  contribute to it; `selected = passed + failed + skipped + not_run` for complete
  evidence. These count CTest test entries, not individual Google Test assertions.
- Absolute paths and SHA-256 hashes for both inventories, JUnit and CTest log;
  validation errors and the resulting outcome.

All selected tests are required; there is deliberately no skip allowlist. Outcomes
are evaluated in this order:

| Outcome | Meaning |
| --- | --- |
| `incomplete` | Missing/unreadable/malformed artifacts, inconsistent counts/names/statuses, unfinished invocation, changed source identity/cache during capture, or a missing/truncated log completion summary. |
| `zero-tests-selected` | Complete empty selection, even when CTest exits zero. |
| `fail` | A failed or not-run test, or nonzero CTest exit for a nonempty complete run. Disabled tests count as not-run. |
| `required-skip` | At least one required test skipped, with no failures or not-run tests. A missing device remains a skip with its reported reason, not a test failure or qualification. |
| `pass` | Every selected test passed, the artifacts reconcile, and CTest exited zero. |

Only `pass` exits zero from either `run` or `validate`. Validation reparses the XML
and inventories, reconciles suite counters, selected names, log result rows and
completion summary, checks retained hashes and recomputes the stored result. A
log that is missing or truncated cannot validate as a pass. Unsupported CTest
formats fail closed as `incomplete`; this adapter targets CTest's single
`testsuite` JUnit format (CTest with `--output-junit` support, verified on 3.31.6).

`--evidence-kind metadata` is the conservative default. Source/API audits and this
tool's own tests are metadata checks. Use `runtime` only for a selection that
actually exercises the runtime contract under review. A successful metadata run
is not runtime qualification. Repeat `--context` to retain explicit driver/device,
feature or unavailable-prerequisite notes. The adapter records configured facts
and relevant display/renderer environment overrides; it does not probe hardware
or infer a driver, executable build revision, assertion coverage or platform
qualification. Git HEAD/dirty state describes the source at capture time, not
proof that existing binaries were rebuilt from it. Hashes detect later changes;
they do not authenticate the producer. Keep the record and artifacts together;
if moving them, update their absolute paths before validation.

Run the regression suite directly or through its `tools`-labelled CTest entry:

```bash
python3 tools/evidence/test_ctest_evidence.py -v
ctest --test-dir cmake-build-software --no-tests=error -R '^ExecutionEvidenceToolTests$' --output-on-failure
```

The small checked-in XML/log fixtures are synthetic CTest-format examples. They
cover pass, failure, required skip, empty selection, unavailable executable,
count disagreement and truncation. Unit tests also exercise CLI exit codes,
missing logs, hash changes, selected-name disagreement and stale-output refusal;
no fixture builds or GPU runs are needed.

CI CTest commands must use `--no-tests=error` (or a test preset with
`execution.noTestsAction` set to `error`). This rejects an empty selection; it does
not reject required skips or prove that every intended test was registered. Use
the evidence adapter above when those distinctions matter. In particular, a
nonempty label selection can still lose one of its expected members, and a
registered aggregate can invoke a Google Test filter that matches no cases.
Compare the inventory and the test output with the required witnesses.

`cmake/UnitTests.cmake` runs the two display-global pointer assertions
`Sdl3InputTest.MouseReadsThePointerBesideTheWindowAsXnaDoes` and
`Sdl3InputTest.RecenteringAHiddenCursorKeepsItsWarpsReal` with `RUN_SERIAL`, as well
as their `CnaPlatformTests` aggregate. Each test warps and then reads the same
pointer; another window test can change that state between the two operations.
A lock on only the two readers would leave other window users free to interfere.
The rest of the suite retains its parallel schedule. This isolates one CTest
invocation; concurrent invocations must still have separate private displays.

After regenerating and building the selected tree, verify the registrations with
`ctest --test-dir cmake-build-software --show-only=json-v1`. For a private-display
parallel check, run:

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-software --no-tests=error \
  -R '^Sdl3InputTest\.(MouseReadsThePointerBesideTheWindowAsXnaDoes|RecenteringAHiddenCursorKeepsItsWarpsReal)$' \
  -j4 --output-on-failure
```

Check the two pointer assertions individually with the same runner and their
exact names as well. A skip is an unavailable prerequisite, not a successful
interference regression. Keep the JUnit/log evidence from the owner's display
run; the scheduling properties alone do not prove runtime stability.

## Compatibility campaign captures (MTH-CONF-007)

The [bounded family map](fna-reference-harness.md#campaign-evidence-map-mth-conf-001007)
and [lifecycle decisions](xna-fna-behavior-decisions.md#mth-conf-004-lifecycle-inspection)
distinguish **declared**, **tested behaviour**, **platform-qualified** and **unassessed** surface.
Campaign evidence retained outside the repository includes the following dumps, logs, commands,
source/binary hashes and results. Recipes below reproduce the named witnesses from a checkout;
counts describe historical runs, not promised totals at a later revision. No tests ran for this
MTH-CONF-007 documentation reconciliation.

| Capture / integrated commit | Retained evidence / executable reproduction | Source/platform qualification |
|---|---|---|
| CONF-001 / `a31835b10` | Eight CLI outcomes: five former false passes now refuse, three legacy fixture self-comparisons pass; 36 Python cases. Run comparator checks below (`FnaReferenceComparisonToolTests`). | Initial source `24e84090c` plus patch; Linux CPU/tool metadata. Fixture self-comparison is acceptance only; strict provenance introduced later rejects the old goldens. |
| CONF-002 / `aa0a7364e` | Two byte-identical value dumps, three original categories compared at tolerance 0, five runtime reference refusals; final comparator 49 and recipe 5 pass. Run the pinned value recipe and tool checks below. | Initial source `dcac546f3` plus patch; Linux/.NET SDK 8.0.425; FNA `00498b0b81425b14bf8da23e68655215b9b5daa7`, assembly 26.10.0.0 SHA-256 `f973ec1edd32290f19750d6d3d26fba23ab1a813fedd71f1ea026edf03c9647c`. Neither native library loaded. |
| CONF-003 through `ee5c7b4b4` | Two aligned FNA dumps, CNA dump, strict comparison, exact-disposition audit, 30 real-dump checks, math and packing logs. Run the value recipe below and the [bounded edge comparison commands](fna-reference-harness.md#bounded-value-and-exception-edges-mth-conf-003); native suite names are in the family map. | Linux x86_64 Release/SOFTWARE/shared; same FNA pin. Twelve repeatability categories; 1,802 retained fields at this stage; math 1,031/0/2 (missing XML), CPU packing 154/0/0. Shared working-tree builds, not integrated-commit rebuilds. |
| Scalar division / `661925667` | Fresh CNA dump, audit of 1,859 retained fields, independent rational binary32 proof of 57 new pairs, 32 real-dump checks and native logs. Reuse the same value recipe; `VectorScalarDivisionContractTest/*.*` and `Vector3CompoundAssignmentTest.*` are the native witnesses. | Observed HEAD `8b3dcb679` plus patch; Linux Release/SOFTWARE/shared. Reused aligned FNA dump SHA-256 `bd529a90d8c6878e49e47711883b9ec9be826e41739dc913c68019d8a46dc9d7`; no FNA rerun. Math 1,084/0/2, packing 172/0/0, Python 32/0/0. Eleven scalar instances failed before correction. |
| CONF-004 / `3671c7ee0` | Native runtime/content/graphics result JSON, exact commands/exits and source/binary hashes. The HEADLESS recipe below replays the historical selections, including all 26 new cases. | Starting HEAD `077a6981c`, final observed `f79f074ff` plus patch; Linux Debug/HEADLESS platform+renderer/shared, GCC 14.2.0. Runtime 137/0/1, content 188/0/3, graphics 102/0/1: **427 pass / 0 fail / 5 skips**. Skips: window/filesystem/audio/render-target capabilities. |

Pass/fail/skip triples count Google Test cases, not CTest entries. Shared HEAD changed during
implementation; dumps lack automatic CNA producer revision metadata. Retained source/binary
hashes bound those observations; an implementation commit alone cannot prove executable
provenance. Shipped-XNA IL identities/offsets and local regeneration commands are in the
[decision ledger](xna-fna-behavior-decisions.md#mth-conf-004-lifecycle-inspection),
[half notes](../tools/fna-reference/xna-half/README.md) and
[Curve notes](../tools/fna-reference/xna-curve/README.md). No successful original-XNA runtime
execution is claimed.

From the checkout root, set `EVIDENCE_DIR` to a fresh external directory and `FNA_SOURCE_DIR`
to the clean, provisioned external FNA checkout; use the existing configured build trees.
The [harness setup](fna-reference-harness.md#building-and-generating-values) defines prerequisites.
The commands below are reproduction instructions, not additional runs:

```sh
python3 -B scripts/test_compare_fna_reference.py -v
python3 -B tools/fna-reference/test_run_reference.py -v
export FNA_EXPECTED_REVISION=00498b0b81425b14bf8da23e68655215b9b5daa7
python3 -B tools/fna-reference/run-reference.py --output-dir "$EVIDENCE_DIR/fna-values"
cmake --build cmake-build-software --target cna_reference_dump CnaMathTests CnaGraphicsTests --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/cna_reference_dump "$EVIDENCE_DIR/cna-values.json"
export FNA_EDGE_REFERENCE="$EVIDENCE_DIR/fna-values/values-1.json"
export CNA_EDGE_CANDIDATE="$EVIDENCE_DIR/cna-values.json"
python3 -B tools/fna-reference/check-value-edges.py "$FNA_EDGE_REFERENCE" "$CNA_EDGE_CANDIDATE"
python3 -B tools/fna-reference/test_value_edges.py -v
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/CnaMathTests --gtest_output=json:"$EVIDENCE_DIR/math.json"
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/CnaGraphicsTests \
  '--gtest_filter=PackedVectorEdgeReferenceTest.*:XnaHalfConversionTest.*' \
  --gtest_output=json:"$EVIDENCE_DIR/packing-witnesses.json"
```

The last command replays the named packing edge/IL witnesses, not the wider historical
154/172-case selections. Current audit success requires 1,859 exact dispositions with an equal
remainder at tolerance zero. The owning harness also gives the raw comparator command, whose
expected exit is 1; disposition success is not FNA parity.

For the original lifecycle selections (HEADLESS platform and renderer; no window qualification):

```sh
cmake --build cmake-build-headless --target CnaRuntimeTests CnaContentTests CnaGraphicsTests --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 900 \
  cmake-build-headless/CnaRuntimeTests '--gtest_filter=GameComponentCollectionTest.*:GameComponentEnumerationContractTest.*:GameServiceContainerTest.*:GameTest.*:NonBlockingTickTest.*:ComponentLifecycleCompatibilityTest.*:GameEventSemanticsTest.*:GraphicsDeviceManagerTest.*:GameComponentOrderingTest.*:UpdateAndDraw/ComponentPassOrderingTest.*:GameLifecycleOrderingTest.*:GraphicsDeviceManagerLifecycleContractTest.*:GameDisposalDispatchContractTest.*' \
  --gtest_output=json:"$EVIDENCE_DIR/lifecycle-runtime.json"
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 900 \
  cmake-build-headless/CnaContentTests '--gtest_filter=*ContentManager*:*Ownership*' \
  --gtest_output=json:"$EVIDENCE_DIR/lifecycle-content.json"
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 900 \
  cmake-build-headless/CnaGraphicsTests '--gtest_filter=GraphicsResourceEventOrderTest.*:GraphicsDeviceDisposalHookTest.*:GraphicsDeviceLifecycleTest.*:EffectParameterCollectionTest.*:EffectTechniqueCollectionTest.*:EffectPassCollectionTest.*:Model*Collection*Test.*' \
  --gtest_output=json:"$EVIDENCE_DIR/lifecycle-graphics.json"
```

Integration commit messages additionally report private SOFTWARE graphics (3,115 pass /
83 capability/environment skips, `ee5c7b4b4`), multi runtime (299/0/5), content (2,161/0/0)
and SDLGPU C API (152 CTest entries passed, `3671c7ee0`). Their raw private captures were not
located in this reconciliation: these remain **commit-reported results**, not independently
reconciled platform qualification here. Retain the owner's JUnit/log, cache and driver identities
before relying on them. The separate C adapter one-shot disposal witness can be replayed with
`tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu --no-tests=error -R '^CApi_GraphicsDeviceManagerSmoke$' --output-on-failure`;
native repeat notifications do not establish that contract. CONF-005/006 and unassessed inputs
retain their [precise next experiments](fna-reference-harness.md#campaign-evidence-map-mth-conf-001007).
