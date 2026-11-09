# XNA 4.0 Content Pipeline final audit

> **Generated** by `tools/xna-pipeline-oracle/final_audit.py` from the campaign's twenty-six acceptance conditions (`XNAPP-330`). Do not edit by hand. `XnaPipelineFinalAuditIsGreen` checks this generated file and its evidence predicates.

A PASS means the row's **source/metadata predicate** succeeds. It does not declare that the recorded acceptance condition is currently fulfilled. The script neither runs the referenced gtests/interop/sanitizers nor verifies arbitrary semantic claims.

## What the check kinds establish

- `parity`: counts/statuses joined from the maintained parity map and inventory; no compiled API or runtime rediscovery.
- `inputs`: eighteen recorded IMPLEMENTED+TESTED flags; no source-asset execution.
- `files`: path existence, not content validity, freshness or documentation completeness.
- `tests`: TEST/TEST_F/TEST_P declaration presence, not compiled or passing cases.
- `ctests`: add_test name patterns, not configured registrations or passing jobs.
- `gate`: executes the named helper; here it compares processor-default annotations with recorded measurements, without running the C++ assertions.

Comments, string literals and literal `#if 0` branches are excluded from C/C++ scans; CMake scans exclude comments and literal `if(FALSE)`/`if(0)` branches. Unknown conditions, macros and build inclusion are not evaluated. Declaration presence is separate from configured CTest registration and actual execution.

Use `--configured-inventory configured.json` for registration candidates, and repeat `--execution-record /path/to/record.json` to join retained runs from [ctest_evidence.py](execution-evidence.md). `--evidence-report layers.json` writes the separate layers; `--require-execution` fails unless every named witness below (except this self-audit) has validated passing execution. C++ cases and Runtime-group CTests require runtime-kind captures; metadata captures cannot substitute. Aggregate gtest commands also need exact RUN/OK case output, not a zero-case exit. Entries labelled `metadata`/`declaration-presence` cannot substitute for runtime witnesses. All captures must describe the same source/build/configuration identity and configured inventory; record paths, hashes and identities are retained.

**Runtime qualification: not established by this audit**, even with passing execution. CTest does not count assertions or prove semantic acceptance: an empty test body can pass. Missing assertion evidence stays unknown, never a zero-assertion qualification. The helper gates, declarations and recorded status flags remain metadata; the self-audit is excluded from required execution to avoid circular evidence. Without optional artifacts, registration and execution are not established. Retained captures are historical observations, not proof that current binaries were rebuilt or that dirty source bytes are unchanged.

GPU/window runs must use the private runner described in [content-pipeline.md](content-pipeline.md). Current defects and external qualification limits remain in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), [the pipeline guide](content-pipeline.md) and [the media evidence](xna-content-pipeline-media.md). A green table does not close them.

## Recorded acceptance conditions and their witnesses

| # | Group | Recorded acceptance condition | Check kind | Evidence result | Evidence |
|---:|---|---|---|---|---|
| 1 | API | 128/128 public and protected types, or a regenerated denominator | `parity` | PASS | 128/128 (the mission named 128) |
| 2 | API | 705/705 relevant members, or a regenerated denominator | `parity` | PASS | 705/705 (the mission named 705) |
| 3 | API | 27/27 enum values | `parity` | PASS | 27/27 (the mission named 27) |
| 4 | API | MISSING API = 0 | `parity` | PASS | 0 MISSING across types, members and enum values |
| 5 | Importers | 10/10 built-in importers | `parity` | PASS | 10/10 (the mission named 10) |
| 6 | Importers | 18/18 declared source extensions IMPLEMENTED+TESTED | `inputs` | PASS | 18/18 extensions IMPLEMENTED+TESTED |
| 7 | Importers | `.xml` included via the generic registered-type architecture | `files` | PASS | 3 artefact(s), all present |
| 8 | Processors | 12/12 built-in processors | `parity` | PASS | 12/12 (the mission named 12) |
| 9 | Processors | 47/47 processor properties | `parity` | PASS | 47/47 (the mission named 47) |
| 10 | Processors | every default measured and tested | `gate` | PASS | processor_defaults_gate.py exited 0 |
| 11 | Processors | processor test matrix complete | `tests` | PASS | 3 named test(s), declaration patterns found |
| 12 | Product | `cna-content build Foo.contentproj` works | `tests` | PASS | 2 named test(s), declaration patterns found |
| 13 | Product | copy items work | `tests` | PASS | 1 named test(s), declaration patterns found |
| 14 | Product | representative content projects build end to end | `tests` | PASS | 2 named test(s), declaration patterns found |
| 15 | Product | a custom C++ pipeline project works, including custom XML content | `tests` | PASS | 2 named test(s), declaration patterns found |
| 16 | Differential | no OPEN differential cases | `tests` | PASS | 1 named test(s), declaration patterns found |
| 17 | Differential | accepted decisions remain live only while differences remain | `files` | PASS | 2 artefact(s), all present |
| 18 | Differential | genuine XNA measurements preserved | `files` | PASS | 3 artefact(s), all present |
| 19 | Runtime | every locally executable output family tested in a genuine XNA 4.0 runtime | `ctests` | PASS | 3 ctest(s), CMake declaration patterns found |
| 20 | Quality | the remaining parsers are fuzzed and sanitized | `tests` | PASS | 3 named test(s), declaration patterns found |
| 21 | Quality | the determinism campaign is done | `tests` | PASS | 3 named test(s), declaration patterns found |
| 22 | Quality | the performance campaign is done | `tests` | PASS | 2 named test(s), declaration patterns found |
| 23 | Quality | the provenance gate is done | `ctests` | PASS | 2 ctest(s), CMake declaration patterns found |
| 24 | Quality | all parity gates are wired to ctest | `ctests` | PASS | 7 ctest(s), CMake declaration patterns found |
| 25 | Quality | the documentation and migration guide are complete | `files` | PASS | 8 artefact(s), all present |
| 26 | Quality | the final audit is complete | `ctests` | PASS | 1 ctest(s), CMake declaration patterns found |

**26 of 26 source/metadata checks pass.**
