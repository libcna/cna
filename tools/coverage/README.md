# GCC coverage for focused HEADLESS tests

`collect.py` uses Python's standard library, Ninja, GCC 14/gcov JSON format 2,
CMake and GNU `timeout`. It measures one configured build and one freshly cleared
counter set. It does not import or merge independent coverage profiles.

Configure an existing assigned build tree with the HEADLESS renderer and platform,
`CMAKE_EXPORT_COMPILE_COMMANDS=ON`, `-O0 --coverage` for C/C++ compilation and
`--coverage` for executable/shared/module linking. Use Debug, enable tests, and
follow the repository's build-directory and ccache rules. Select actual focused
GoogleTest targets from `cmake/UnitTests.cmake`.

Example, with an already configured `build-probe` and a caller-owned artifact
directory (`$coverage_artifacts`):

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
python3 tools/coverage/collect.py prepare --build-dir build-probe \
    --output "$coverage_artifacts/prepared.json" \
    CnaMathTests CnaContentTests CnaRuntimeTests CnaAudioTests \
    CnaGraphicsTests CnaInputModuleTests CnaStorageTests CnaCoreTests CnaMediaTests
python3 tools/coverage/collect.py run \
    --prepared "$coverage_artifacts/prepared.json" \
    --output-dir "$coverage_artifacts"
```

Preparation hashes the selected compile sources and headers under their include
roots and source directories before building. The run builds these targets with at most four jobs,
checks the snapshot, clears only their `.gcda` files, and launches each binary
sequentially from the source root (for relative fixture paths), with
DISPLAY/WAYLAND_DISPLAY removed, SDL offscreen/dummy drivers,
and a 1,800-second timeout. Inherited `GTEST_*` and `GCOV_*` variables are removed
to avoid hidden filters, sharding, repeat settings or redirected counters. Each
binary's GoogleTest XML retains every case, outcome and skip reason. Disabled
cases are reported separately. No GPU qualification is implied.

A failed build, failed/empty/all-skipped test binary, missing instrumented object,
gcov warning other than an expected absent counter file or a translation unit
with no functions (for example a disabled platform's tests), source/configuration
change, mixed compiler version, or changed artifact/counter invalidates the run.
No coverage JSON/Markdown is published on failure; prior reports at those output
paths are removed. Execution failures retain logs, XML and `run.json` with a failed
status. Preparation/preflight failures may have only stderr/build logs. A failed
binary does not prevent the remaining selected binaries from running, but all
coverage publication is refused. Do not build or run other processes against this
build tree during collection.

Successful outputs:

- `coverage.json`: module/file metrics, uncovered arc clusters, explicit excluded
  paths and reasons, source/header hashes, compiler and CMake flags, revision and
  dirty flag, binary/library/object/note/counter hashes, exact commands and all test cases.
- `COVERAGE_BASELINE.md`: configuration, module summaries and test counts.
- `run.json`, `coverage-build.log`, per-binary logs and XML: execution evidence.

Module `src` and `include` metrics are separate. Lines are unioned by source path
and line; functions by source path, mangled name and start position. Branches are
**GCC arc instances per translation unit**, including exception arcs. Identical
inline/template branches emitted in multiple units remain distinct. This avoids
silently merging incompatible graphs, but is not a count of unique source-level
conditions. Unexecuted instrumented units remain in the denominator even when
no `.gcda` was written. Third-party/vendor, generated, test/tool, system and other
non-production paths are listed with exclusion reasons. Module source/header
files not emitted by the selected build are explicitly listed as unmeasured.
Prebuilt libraries cannot acquire coverage through this tool.

Source hashes provide the measured identity in a dirty shared checkout; start/end
Git revisions are provenance, not proof that the tree was clean. A commit in an
unrelated area does not invalidate unchanged measured sources. The collector does
not certify fixture semantics, external services/hardware, or correctness.
Review uncovered arcs against reachable public contracts before adding tests.

Run the collector's evidence/counting regression tests (set `TMPDIR` to your
assigned scratch directory if required by your lane):

```sh
python3 -m unittest discover -s tools/coverage -v
```

## Reviewed non-regression policy (MTH-TEST-012)

After a successful fresh collection, check the local gate immediately, before
another process changes the tree/artifacts/counters:

```sh
python3 tools/coverage/check.py --report "$coverage_artifacts/coverage.json"
```

`thresholds.json` pins GCC/gcov 14.2.0, the HEADLESS/HEADLESS Debug configuration,
shared runtime and the same nine targets as TEST-002. It gates **src** function,
line and branch percentages for math (94/94/53), storage (96/95/62) and runtime
(81/84/55). Exact baselines and margins are retained per metric: at least two
percentage points for functions/lines and three for branch instances. Each
metric must retain at least 95% of its baseline denominator. The checker uses
raw counts, rejects negative gcov counters in gated production areas and
missing/failed/empty/all-skipped target runs, and verifies
source/configuration and artifact/counter hashes. Stored rounded percentages
cannot hide a regression. A failed check exits 1; it never rewrites a floor.
Legitimate large source removal or compiler/selection changes require a reviewed
policy update and new compatible measurement.

All include/header metrics stay reported separately: template/exception branch
instances depend on the compilation-unit selection. Other modules remain
ungated in this first small policy: graphics/renderers/platform need renderer or
host qualification; audio/media/video depend on decoder/device configuration;
input retains native/injected-host differences; broad content has observed
negative gcov counters in its current profile and needs counter-quality qualification;
content-pipeline was not
selected; devices/diagnostics are disabled; core is consumer-driven; net,
services, C ABI and other modules lack a dedicated denominator in this profile.
Third-party, generated, system, tests/tools and unbuilt paths keep the collector's
explicit exclusions. Full per-area rationale and comparable values are in the
campaign's `test/FINAL_REPORT.md` and `test/COVERAGE_CURRENT.md`. None is silently
counted as covered. Coverage still measures execution, not correctness.

The coverage tool tests and reference recipe safety controls run in the existing
general CI job. Instrumented collection currently remains local: the comparable
nine-target shared-runtime closure is 1,401 units; the measured 672-step cached
rebuild took 133.60 seconds, and the existing baseline took 279.54 seconds of test
processes. A cold hosted instrumented-build budget has not been measured. A
separate coverage workflow would duplicate a broad runtime build; revisit after
measuring that budget or qualifying a smaller closure. There is no CI claim for
this local threshold check.

The external campaign checkout supplies a single final-refresh command, run
from CNA with exclusive ownership of the assigned build tree:

```sh
python3 ../cnaplan/test/refresh.py --source . --build-dir build-probe \
    --artifacts "$TMPDIR/final-refresh"
```

It settles Ninja/CMake regeneration before preparing the strict snapshot,
collects all nine suites, checks the policy, and refreshes the campaign inventory,
selected member execution map and final report. It preserves the original
baseline, independent goldens and thresholds. It logs exact commands, exits and
elapsed times. A failed/concurrent run does not replace a prior successful
snapshot. The campaign snapshot must be refreshed again before handover; owner
GPU/sanitizer/native-host and newly enabled C ABI CI runs are separate obligations.

To regenerate just the static campaign test/CI inventory (no build or test run):

```sh
python3 tools/coverage/inventory.py --source . --output-dir "$coverage_artifacts"
```

This records commit-added source definitions and C API CTest names, not executed
cases or member closure. Parameterized definitions are not multiplied. Pending
shared-checkout additions are explicitly marked. The reviewed CI lane rules
must be reconsidered if workflows, CMake guards or renderer selections change.
