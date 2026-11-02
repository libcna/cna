# CTest execution evidence

`tools/evidence/ctest_evidence.py` is a Python 3 standard-library adapter for one
actual CTest invocation. It reuses `tools/build/build_provenance.py` cache/hash/Git
helpers and CTest's own JSON inventory, JUnit and output log. Existing audit,
provenance and CTest reports remain available; this tool does not replace them.

```bash
python3 tools/evidence/ctest_evidence.py run \
  --build-dir cmake-build-software \
  --output-dir /tmp/cna-evidence-unique-run \
  -R '^ExecutionEvidenceToolTests$' -L tools \
  --evidence-kind metadata \
  --context 'CPU-only tool checks; no GPU qualification'
python3 tools/evidence/ctest_evidence.py validate /tmp/cna-evidence-unique-run/record.json
```

The output directory must not exist. Keep it outside the source tree or in an
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
ctest --test-dir cmake-build-software -R '^ExecutionEvidenceToolTests$' --output-on-failure
```

The small checked-in XML/log fixtures are synthetic CTest-format examples. They
cover pass, failure, required skip, empty selection, unavailable executable,
count disagreement and truncation. Unit tests also exercise CLI exit codes,
missing logs, hash changes, selected-name disagreement and stale-output refusal;
no fixture builds or GPU runs are needed.
