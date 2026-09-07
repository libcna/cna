# XNA 4.0 Content Pipeline final audit

> **Generated** by `tools/xna-pipeline-oracle/final_audit.py`. Twenty-six conditions, in the mission's own order and words, each checked against the file or the test that is its evidence. Do not edit by hand. The ctest `XnaPipelineFinalAuditIsGreen` runs it. Task log: `plans/plan_xnapipeline_parity.md` `XNAPP-330`.

A check answers whether a row's evidence exists and says what the row claims. Whether that evidence *passes* is the test suite's own question, and section 33.1 of the plan records the suite's baseline.

| # | Group | Condition | | Evidence |
|---:|---|---|---|---|
| 1 | API | 128/128 public and protected types, or a regenerated denominator | PASS | 128/128 (the mission named 128) |
| 2 | API | 705/705 relevant members, or a regenerated denominator | PASS | 705/705 (the mission named 705) |
| 3 | API | 27/27 enum values | PASS | 27/27 (the mission named 27) |
| 4 | API | MISSING API = 0 | PASS | 0 MISSING across types, members and enum values |
| 5 | Importers | 10/10 built-in importers | PASS | 10/10 (the mission named 10) |
| 6 | Importers | 18/18 declared source extensions IMPLEMENTED+TESTED | PASS | 18/18 extensions IMPLEMENTED+TESTED |
| 7 | Importers | `.xml` included via the generic registered-type architecture | PASS | 3 artefact(s), all present |
| 8 | Processors | 12/12 built-in processors | PASS | 12/12 (the mission named 12) |
| 9 | Processors | 47/47 processor properties | PASS | 47/47 (the mission named 47) |
| 10 | Processors | every default measured and tested | PASS | processor_defaults_gate.py exited 0 |
| 11 | Processors | processor test matrix complete | PASS | 3 named test(s), all declared |
| 12 | Product | `cna-content build Foo.contentproj` works | PASS | 2 named test(s), all declared |
| 13 | Product | copy items work | PASS | 1 named test(s), all declared |
| 14 | Product | representative content projects build end to end | PASS | 2 named test(s), all declared |
| 15 | Product | a custom C++ pipeline project works, including custom XML content | PASS | 2 named test(s), all declared |
| 16 | Differential | no OPEN differential cases | PASS | 1 named test(s), all declared |
| 17 | Differential | accepted decisions remain live only while differences remain | PASS | 2 artefact(s), all present |
| 18 | Differential | genuine XNA measurements preserved | PASS | 3 artefact(s), all present |
| 19 | Runtime | every locally executable output family tested in a genuine XNA 4.0 runtime | PASS | 3 ctest(s), all registered |
| 20 | Quality | the remaining parsers are fuzzed and sanitized | PASS | 3 named test(s), all declared |
| 21 | Quality | the determinism campaign is done | PASS | 3 named test(s), all declared |
| 22 | Quality | the performance campaign is done | PASS | 2 named test(s), all declared |
| 23 | Quality | the provenance gate is done | PASS | 2 ctest(s), all registered |
| 24 | Quality | all parity gates are wired to ctest | PASS | 7 ctest(s), all registered |
| 25 | Quality | the documentation and migration guide are complete | PASS | 8 artefact(s), all present |
| 26 | Quality | the final audit is complete | PASS | 1 ctest(s), all registered |

**26 of 26 conditions hold.**

