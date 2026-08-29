# CNA compilation-performance plan

> **Status:** ACTIVE — the experiments and measured baseline are complete; final regression
> guardrails remain. This plan turns [`misc/cnacomp.md`](../misc/cnacomp.md) into an executable task
> sequence.
>
> **Goal:** shorten the edit/build/test loop without weakening XNA/FNA compatibility, diagnostics,
> platform coverage, installed-package compatibility, or reproducibility.
>
> **Status legend:** ✅ complete and verified; 🟨 implemented but not yet accepted; ⬜ pending;
> ⛔ blocked with the blocking dependency recorded in the task.

## 1. Current verified baseline

Commit `b3db5701b` established the first safe layer:

- focused `dev`, `unit`, `release-modules`, and `release-ipo` Ninja presets;
- target-scoped build configuration, project policy, instrumentation, Emscripten ABI, and linker
  policy instead of routine global `CMAKE_*_FLAGS` mutation;
- stable per-invocation `CCACHE_BASEDIR`, `cna_ccache_stats`, and compilation databases;
- validated sanitizer, optional linker, and guarded IPO/LTO controls;
- target-scoped Clang compatibility for vendored Draco.

The local measurements on 2026-08-28 are a starting point, not a portable performance promise:

| Configuration/measurement | Result |
|---|---:|
| Legacy `cmake-build-debug` `all` compile commands | 1,929 |
| `dev` target compile commands | 483 (75.0% fewer; about one quarter of the work) |
| `unit` / `CnaTests` compile commands | 1,126 (41.6% fewer) |
| Warm `dev` no-op build | 0.16 s |
| CMake regeneration observed after build-system edits | about 4.5 s |
| `cmake-build-debug` disk use | 29 GiB |
| `cmake-build-dev` disk use | 566 MiB |
| `cmake-build-unit` disk use | 1.9 GiB |

The developer subsequently increased the shared ccache limit from 20 GiB to 50 GiB and reset its
statistics. That is local machine state, not repository configuration. The old lifetime hit rate
must not be compared with the new post-reset counters.

## 2. Rules for every task

1. Run performance comparisons with the same compiler/version, renderer, target, optional
   dependencies, parallelism, build type, and cache state.
2. Record configure time, build wall time, peak RSS, executed compile/link edges, build-tree size,
   and ccache counters before and after the candidate.
3. Measure at least a clean build, warm no-op build, representative `.cpp` rebuild, public-header
   rebuild, and final-link-only rebuild when the task can affect them.
4. Run the affected tests. A faster build that changes behavior or loses tests is a regression.
5. Keep third-party targets, package consumers, sanitizers, Emscripten, and cross-compiles outside
   new policy unless the task explicitly verifies them.
6. Implement one task per commit. Update this plan's status and evidence in that same commit.
7. Do not make a measured experiment the default until its acceptance criteria are satisfied on
   at least native GCC and Clang builds.

## 3. Task index and order

| ID | Task | Depends on | Status |
|---|---|---|---|
| COMP-001 | Rebuild the benchmark and ccache evidence baseline | foundation commit | ✅ |
| COMP-002 | Split the monolithic unit-test iteration path | COMP-001 | ✅ |
| COMP-003 | Pilot target-specific precompiled headers | COMP-002 | ✅ |
| COMP-004 | Benchmark Mold and LLD final linking | COMP-001 | ✅ |
| COMP-005 | Reduce CMake configure/regeneration cost | COMP-001 | ✅ |
| COMP-006 | Reduce measured header and translation-unit cost | COMP-001 | ✅ |
| COMP-007 | Add an opt-in CI unity-build experiment | COMP-002, COMP-006 | ✅ |
| COMP-008 | Publish results and add regression guardrails | COMP-002–COMP-007 | ⬜ |
| COMP-009 | Add an opt-in fast-debug preset | COMP-001 | ✅ |

`COMP-003`, `COMP-004`, `COMP-005`, and `COMP-006` may proceed independently after their stated
dependencies. `COMP-007` must remain last among compilation-technique experiments because unity
builds can hide include hygiene and alter which translation units appear expensive.

## 4. COMP-001 — benchmark and ccache evidence baseline

### Work

- Add a repository-owned benchmark driver under `tools/build/` that records commands and results
  without deleting arbitrary paths. Disposable build directories must have a fixed
  `cmake-build-benchmark-*` prefix or be created with `mktemp -d`.
- Measure `dev`, `unit`, and the legacy full integration configuration with GCC and Clang at
  `--parallel 8`, `12`, and `16`. Stop a setting if it swaps or materially harms interactivity.
- Capture compiler/linker versions, CMake cache variables that affect the target closure, Ninja
  compile/link edge counts, wall time, peak RSS, output size, and build-tree size.
- Record ccache counters before and after three representative edit/build sequences. Keep direct
  mode enabled. Do not clear the 50 GiB cache and do not change global ccache policy from CMake.
- Add the concise results and exact reproduction commands to `docs/build-performance.md`.

### Acceptance

- Re-running the driver with the same inputs produces comparable machine-readable results.
- Fresh post-reset ccache statistics distinguish hits, misses, uncacheable calls, and evictions.
- The plan records a measured baseline rather than extrapolating elapsed time solely from command
  counts.

### Direct-mode diagnostic evidence (2026-08-28)

- The apparent 0% direct-hit snapshot was not caused by CNA: `ccache --show-config` reported
  `direct_mode = true`, and a one-file debug trace ended in `Result: direct_cache_hit`.
- The same trace reported that its statistics could not be finalized because the sandbox could not
  create ccache's configured temporary directory. Repeating the rebuild with writable global-cache
  state advanced the direct counter from zero to one.
- No unsafe `sloppiness`, timestamp relaxation, or project override was added. Documentation now
  requires per-workload counter deltas from a writable environment and shows the exact debug-log
  procedure.

### Completion evidence (2026-08-29)

- `tools/build/benchmark_clean_build.py` supports `dev`, `unit`, and legacy `all` profiles, creates
  or accepts only a new `cmake-build-benchmark-*` directory, never removes it, and emits the exact
  commands, cache values, tool versions, load averages, configure/build/no-op timings, aggregate
  Linux process-tree RSS, graph size, artifact/tree/cache sizes, and ccache counters as JSON.
- Eighteen empty-cache Debug/FULL-DWARF/Mold builds covered GCC 14.2.0 and Clang 19.1.7 at 8, 12,
  and 16 jobs. The exact compile graphs were stable at 483 (`dev`), 1,126 (`unit`), and 1,929
  (`legacy`). Unlike the focused STUB profiles, `legacy` reproduces the complete OPENGLES3 closure:
  tests, examples, C API, CNAEXT, compiled effects, networking, and video.
- At 12 jobs the complete legacy `all` build took 643.75 s with GCC and 605.18 s with Clang. Clang
  was 6.0% faster, peaked at 4.65 GiB versus GCC's 4.73 GiB, and produced a 20.80-GiB tree versus
  29.54 GiB. Clang gained only 3.8% from 12 to 16 jobs while RSS rose 58.8%; 12 jobs is therefore
  the balanced full-build default. Clang `-j16` was the shortest complete point at 582.34 s, while
  GCC `-j16` took 605.08 s.
- The host was shared and load-sensitive; one Clang `unit`/12 point was an explicit outlier. The
  report preserves observed values and load metadata rather than treating single-run differences as
  portable promises. Structural graph/cache facts and repeated compiler/resource trends remain
  comparable.
- Three writable global-cache edit sequences left direct mode and the 50 GiB policy unchanged. A
  one-test rebuild produced 1 direct hit, a previously uncached production source produced 1 miss,
  and touching `Vector3.hpp` produced 23 direct hits. All had zero preprocessed hits, uncacheable
  calls, and cleanups; the header rebuild completed in 0.36 s. This closes the earlier direct-mode
  diagnostic with real workload deltas rather than lifetime percentages.
- Exercising the complete graph found and fixed configuration-only integration faults: focused
  test objects now inherit MojoShader's public definitions, Clang sees matching C API declaration
  tags and unambiguous example colors, pure-C math smokes link the platform math library, and shared
  example content copies are serialized instead of racing under high parallelism. Native GCC and
  Clang complete builds both finish successfully after these fixes.

## 5. COMP-002 — split the monolithic unit-test iteration path

`CnaTests` currently requires 1,126 compile commands and a large final link even when one module is
being edited. The goal is a focused developer target, while retaining a full compatibility path.

### Work

- Inventory every test source by owning module, renderer family, shared fixture, and genuinely
  cross-module integration suite.
- Extract shared fixtures and test utilities into narrowly linked support targets.
- Group module test sources into object libraries so a source is compiled once and can feed both a
  focused executable and the legacy full-suite executable. Do not use ordinary static libraries
  where GoogleTest registration objects could be discarded by the linker.
- Add focused executables for at least core/math, content, graphics, runtime, devices, and renderer
  contract tests. Give each target only the module dependencies it actually needs.
- Preserve the `CnaTests` executable and its existing full-suite behavior during the migration.
  Focused developer executables should not also be registered in default CTest if that would make
  CI execute every test twice.
- Document direct focused invocations and add a small number of useful build presets rather than
  one preset per module.

### Acceptance

- Building a focused module test target does not compile unrelated module or renderer test sources.
- The full `CnaTests` binary contains the same test inventory as before the split and passes.
- Test sources are not compiled twice when building `CnaTests`.
- Record focused `.cpp` rebuild and final-link times. Keep the split only if a representative
  module loop improves by at least 30% without increasing the full clean build by more than 10%.

### Completion evidence (2026-08-28)

- The build maps 17 possible module/renderer/integration groups to focused executables. The unit
  preset enables 15 of them (networking and gamer-services are disabled there) while retaining
  `CnaTests` as the complete compatibility executable for that configuration.
- The complete pre/post `--gtest_list_tests` output has 8,102 lines and an identical normalized
  SHA-256 (`373e1f4446a06ffd890f4b835d4b083d83ad2c1b77815957947834966f4b8136`).
  The normalization removes GoogleTest parameter comments containing process-specific pointer
  bytes; test names and order are unchanged.
- A `CnaMathTests` build requires 130 compile commands versus 1,126 for `CnaTests` (88.5% fewer),
  and its 840 tests pass. Core, content, and graphics require 90.7%, 43.9%, and 66.6% fewer commands
  respectively. No test source compile command is duplicated in the complete target graph.
- With a warm ccache and `--parallel 12`, a representative math-test source rebuild takes 0.24 s
  through `CnaMathTests` versus 0.84 s through `CnaTests` (71% less); a forced final link takes
  0.20 s versus 0.75 s (73% less). Focused executables are excluded from `all`, while the complete
  target retains its pre-split 1,126 compile commands, so the split adds no clean-build edges.
- The complete `CnaTests` target links successfully. Three renderer-capability content cases remain
  invalid under the STUB renderer (3D/cube texture storage); they predate and are independent of
  the source partitioning.
- Useful core, math, content, and graphics build presets plus direct invocation documentation were
  added without registering the focused binaries as duplicate CTest suites.

## 6. COMP-003 — target-specific PCH pilot

### Work

- Use COMP-001/002 traces to select one stable, high-cost CNA-owned target. Prefer a focused test
  target whose sources repeatedly parse standard-library and GoogleTest headers.
- Add `target_precompile_headers(... PRIVATE ...)` behind `CNA_ENABLE_PCH`, defaulting to `OFF`
  during the experiment.
- Keep volatile CNA public headers out of the first PCH. Do not expose a build-tree PCH to installed
  consumers or apply it to vendored dependencies.
- Verify GCC and Clang; inspect ccache PCH counters. Do not add ccache `sloppiness` settings unless
  correctness and macro/time behavior are explicitly demonstrated.
- Measure clean, one-source, public-header, and peak-memory deltas with PCH both enabled and disabled.

### Acceptance

- At least 15% improvement on the selected target's clean compile or a clearly documented larger
  improvement in its normal edit loop.
- No material regression in one-source incremental time, no new warnings, and no test difference.
- Peak memory remains safe at the documented parallelism. Otherwise remove the pilot and record it
  as rejected evidence.

### Completion evidence (2026-08-28)

- `CNA_ENABLE_PCH`, default `OFF`, applies only to the 140-source content-test object target. Its
  private PCH contains GoogleTest and stable standard-library headers, never CNA public headers or
  third-party target policy. `unit-pch` / `unit-content-pch` provide an isolated build directory.
- With GCC 14.2.0, Debug/STUB, Ninja, Mold, ccache disabled, and `--parallel 12`, two clean object
  rebuilds averaged 71.90 s without PCH and 55.03 s with PCH: a 23.5% improvement. Peak RSS rose
  from about 533 MiB to 548 MiB (2.8%), remaining safe on the reference host.
- Clang 19.1.7 also compiled the PCH and linked `CnaContentTests`. GCC and Clang emitted no new
  PCH-specific warnings. The focused test inventory was preserved, and PCH-on/off runs retained the
  same known STUB renderer/shader capability failures rather than introducing a test difference.
- ccache PCH support would require relaxed `pch_defines,time_macros` correctness checks. CNA does
  not enable that sloppiness: when ccache and the pilot are both on, this one object target bypasses
  the launcher while all dependencies remain cacheable.

## 7. COMP-004 — Mold/LLD linker benchmark

The `CNA_LINKER` policy already detects and probes `MOLD` and `LLD`; neither was installed during the
foundation work, but both are now available on the reference host.

### Work

- Install Mold and/or LLD only in the benchmark environment, not from project configuration.
- Build identical `CnaTests` and release content-tool inputs with `DEFAULT`, `LLD`, and `MOLD`.
- Measure final-link-only wall time, peak RSS, executable size, startup, and test results.
- Verify that explicit unavailable linkers fail clearly and that unsupported platforms retain their
  default linker.
- Keep `AUTO` probe-based. Do not hard-code an ELF linker into Apple, MSVC, Android, Emscripten, or
  cross builds.

### Acceptance

- Publish the measurement table in `docs/build-performance.md`.
- A linker may become a recommended local dependency only if it improves the large test link by at
  least 20% and passes the same tests. The portable default remains valid.

### Completion evidence (2026-08-29)

- `tools/build/benchmark_final_link.py` extracts exactly one final-link command from an existing
  Ninja graph, runs it directly without rebuilding inputs, samples aggregate Linux process-tree
  RSS, and emits all repetitions plus versions and the exact command as JSON. Five large Debug
  links and seven Release tool links used GCC 14.2.0, GNU ld 2.44, LLD 19.1.7, and Mold 2.37.1.
- For the identical 7,427-test `CnaTests` input, median final-link time was 10.638 s with GNU ld,
  0.972 s with LLD (90.9% faster), and 1.073 s with Mold (89.9% faster). Peak process-tree RSS was
  1,566/1,855/1,943 MiB respectively; artifacts were 330,289,832/337,507,472/350,046,664 bytes.
  Interleaved 30-run no-test startup medians were 58.5/58.1/59.2 ms, showing no material startup
  difference.
- The Release `cna_tool_cnb_info` link medians were 0.123 s with GNU ld, 0.063 s with LLD (48.7%
  faster), and 0.048 s with Mold (60.9% faster). Peak RSS was 29/105/126 MiB and artifacts were
  158,008/153,064/167,800 bytes; startup stayed near 2 ms and every binary printed the expected
  usage text.
- All three `CnaTests` binaries produced the exact same result profile: 7,427 run, 6,868 passed,
  490 skipped, and the same normalized set of 69 failures. The failures are baseline STUB/sandbox
  limitations, including an unwritable storage directory, rather than linker differences.
- The linker probe verifies Mold-to-LLD switching in one build tree, a clear error for an explicitly
  unavailable linker, and toolchain-default behavior for cross-build `AUTO`. Candidate-specific
  program-cache entries fix the stale-path issue exposed by this test. `AUTO` remains probe-based
  and prefers Mold, while both Mold and LLD qualify as recommended local dependencies; GNU ld
  remains the dependency-free portable choice.

## 8. COMP-005 — configure/regeneration cost

### Work

- Capture a CMake profiling trace for cold configure and regeneration.
- Attribute time to source globbing, renderer registration, configure-time audit scripts, vendored
  dependency checks, and other top-level work.
- Replace `GLOB_RECURSE CONFIGURE_DEPENDS` only where profiling shows a material cost and a generated
  or explicit source manifest can be maintained reliably.
- Move expensive diagnostics to explicit/CI targets or add input-aware caching only when doing so
  preserves the configure-time correctness gate.
- Do not weaken platform, renderer, descriptor, or API audit failures merely to improve a timer.

### Acceptance

- Reduce unchanged native `dev` regeneration time by at least 30%, or document with profiling why
  the remaining time is required correctness work.
- Adding/removing a source file still causes the correct target to regenerate and build.
- Existing audit checks remain executable in CI and fail on an intentionally invalid fixture.

### Completion evidence (2026-08-28)

- A Google Trace profile attributed 4.42 s of configure self time to 23 `execute_process()` calls.
  The platform ratchet, non-production SDL audit, and hot-path lint alone cost 1.45 s, 1.45 s, and
  1.30 s respectively. The physical module validator also performed about 181,000 nested module
  comparisons per configuration.
- Successful audits are now cached per build tree using SHA-256 over all inspected source content,
  scripts, budgets, Python version, strictness, and command. The three checks share one module-tree
  fingerprint pass. A failed result is never cached, fresh CI trees always execute the checks, and
  `CNA_CONFIGURE_AUDIT_CACHE=OFF` provides an explicit forced-audit path.
- The physical module ownership gate now performs two equivalent combined regex checks per
  translation unit instead of walking both module lists. Its profile cost fell from about 0.89 s
  of loop/condition work to about 0.03 s.
- Repeated unchanged `dev` configuration fell from 4.39–4.72 s to 0.83–0.84 s (about 82% less),
  exceeding the 30% acceptance threshold. Cached `unit` configuration measured 1.26 s; forcing
  audits with the cache disabled measured 3.99 s.
- Adding a temporary harmless module source triggered Ninja's glob mismatch, regenerated CMake,
  refreshed the audits, and compiled the new object; removal regenerated again. Adding a temporary
  forbidden `SDL_CreateWindow` reference invalidated the cache and failed the strict ratchet gate.
- The existing `cna_platform_ratchet`, `cna_platform_nonproduction_sdl_audit`, and
  `cna_platform_hot_path_lint` targets remain available for explicit execution.

## 9. COMP-006 — include and translation-unit cost

### Work

- Use `compile_commands.json`, Clang `-ftime-trace`, and `clang-scan-deps` (plus IWYU where useful)
  to rank expensive translation units and high-fan-out headers.
- Start with measured hotspots, including the large C API engine layer and large renderer sources;
  file size alone is not sufficient evidence.
- Remove unnecessary transitive includes, use forward declarations where ownership permits, and
  move implementation-only includes and non-template bodies from `.hpp` to `.cpp`.
- Split a large `.cpp` only at cohesive subsystem boundaries and only when traces show the split
  improves useful parallelism or incremental rebuilds.
- Preserve public XNA/FNA declarations, Doxygen coverage, exception behavior, and include spelling.

### Acceptance

- Record before/after parse time and public-header fan-out for each modified hotspot.
- Each retained change improves the chosen metric by at least 10% or removes a documented large
  rebuild fan-out, with affected tests passing on GCC and Clang.
- No new cyclic includes, public API changes, or implementation bodies moved into public headers.

### Completion evidence (2026-08-28)

- A repository-owned `tools/build/analyze_clang_time_trace.py` reports text or JSON rankings from
  modern Clang begin/end `Source` events and `Total ExecuteCompiler`. Clang 19.1.7 `-ftime-trace`
  ranked `ContentManager.cpp` (13.31 s), `GltfImportCore.cpp` (9.70 s), and `CnbModelCodec.cpp`
  (6.73 s) as the leading content TUs; `ContentReader.hpp` led project headers at 23.71 s across
  20 production parses. `clang-scan-deps-19` independently found 24 dependants in the complete
  focused build closure.
- `ContentReader.hpp` no longer transitively includes seven full math definitions used only as
  return declarations. Forward declarations preserve the API; implementation and test users now
  include the definitions they construct. This reduces those math-header parse occurrences across
  46 production content TUs from 173 to 120 (30.6%) and inclusive trace time from 3.67 s to 2.27 s
  (38.1%).
- Three controlled consumer-probe runs reduced mean Clang frontend time from 1,308 ms to 988 ms
  (24.5%), exceeding the 10% acceptance threshold. Whole-target single samples were noisy
  (83.80 s before, 95.53 s after), so no clean-target wall-time gain is claimed; peak RSS remained
  about 490 MiB.
- GCC 14.2.0 and Clang 19.1.7 both build and link `CnaContentTests`; all 21 directly affected
  `ContentReader` and external-reference tests pass under both. No API signature, implementation
  placement, or runtime behavior changed.

## 10. COMP-009 — opt-in fast-debug preset

### Work

- Keep the ordinary Debug configuration unchanged and add a separate preset with reduced debug
  information for local compile/link loops.
- Compare line tables (`-g1` on GCC, `-gline-tables-only` on Clang) with full and split DWARF using
  identical target closures, cache state, linker, and parallelism.
- Preserve source-level stacks and breakpoints; reject combinations that would weaken sanitizer
  diagnostics or silently do nothing on unverified toolchains.

### Acceptance

- Retain only an opt-in preset. Record compile, memory, object/tree size, relink, no-op, and basic
  debugger-line evidence; full Debug must remain the default.

### Completion evidence (2026-08-28)

- `CNA_DEBUG_INFO=FULL|LINE_TABLES|SPLIT` is target-scoped to CNA-owned and enabled Sharp Runtime
  targets. `FULL` remains the default. Reduced modes are native GNU/Clang Debug-only and are refused
  with sanitizers; vendored libraries and consumers do not inherit the flags.
- `dev-fast-debug` selects line tables in an isolated build directory. GCC emits `-g -g1`; Clang
  emits `-g -gline-tables-only`. Both build and run `cna_tool_cnb_info`, whose line-table executable
  retains 18,922 decoded source-line rows.
- With GCC 14.2.0, Debug/STUB, Mold, ccache off, and 12 jobs, rebuilding the 46-source content module
  after identical dependencies took 38.76 s full versus 34.99 s line tables (9.7% faster). Peak RSS
  fell 16.6%, content objects 42.3%, the complete tree 47.2%, and the final tool 52.5%. Two
  single-job `ContentManager.cpp` rebuilds averaged 9.20 s versus 8.30 s (9.8% faster). Mold relinks
  and no-op builds were effectively unchanged.
- Split DWARF took 38.32 s for the controlled module but grew the full tree to 605.4 MB versus
  590.6 MB full because 40.3 MiB of `.dwo` files accompany the objects. It remains an explicit
  experiment without a preset. Full clean-closure samples were load-sensitive and contradicted the
  controlled result, so the documentation discloses them and makes no clean-closure speed claim.

## 11. COMP-007 — opt-in unity build for clean CI builds

### Work

- Add a separate `CNA_ENABLE_UNITY_BUILD` experiment or dedicated CI preset; default `OFF`.
- Enable CMake `UNITY_BUILD` only on measured, stable CNA-owned leaf targets and set a bounded batch
  size. Exclude third-party targets, generated sources, platform-sensitive translation units, and
  files with known anonymous-namespace/include-order collisions.
- Compare clean build time, incremental `.cpp` and public-header rebuilds, peak RSS, diagnostics,
  and test behavior.
- Run at least GCC and Clang configurations. Include one sanitizer configuration to expose hidden
  ODR and instrumentation interactions.

### Acceptance

- Keep the preset only if clean CI compilation improves by at least 25%, memory stays within the
  documented worker limit, and the complete selected test suite passes.
- Unity remains opt-in unless a later project-owner decision accepts its incremental-build and
  diagnostic tradeoffs.

### Completion evidence (2026-08-29)

- `CNA_ENABLE_UNITY_BUILD`, default `OFF`, is limited to `cna_core`, `cna_math`, and their focused
  test object targets. Production math sources use six collision-aware groups of at most three;
  core uses one verified eight-source group; test batches contain at most eight sources. Repeated
  test-local `kEps` names are isolated by CMake's generated per-source unity identifier rather than
  by changing production or test source semantics.
- On two clean GCC 14.2.0 Debug/STUB/Mold builds with ccache off and four jobs, the complete
  `CnaCoreTests` + `CnaMathTests` closure averaged 45.88 s without unity and 34.12 s with unity:
  25.6% faster. Compile edges fell 30.3%, selected objects 43.1%, and tree size 12.3%; peak RSS rose
  8.0% to about 502 MiB. The 25 production module sources alone improved 78.0% (7.51 to 1.65 s).
- At 12 jobs the clean-closure gain was only 19.1%, so `unit-unity` is documented for constrained
  four-job clean CI and does not replace `unit`. A one-source edit regressed 5.7%, a public-header
  rebuild improved 15.5%, and final relink/no-op time was unchanged, reinforcing the opt-in scope.
- GCC 14.2.0 and Clang 19.1.7 both pass all 63 core and 840 math tests. GCC AddressSanitizer also
  passes both suites with only leak detection disabled for the ptrace-based sandbox; address and
  ODR instrumentation remain enabled. Emscripten/cross builds reject the experiment, and all
  third-party targets, other CNA modules, consumers, and default presets remain non-unity.

## 12. COMP-008 — results and regression guardrails

### Work

- Consolidate accepted measurements in `docs/build-performance.md` and mark every experiment here
  ✅, rejected with evidence, or explicitly deferred.
- Add a lightweight CI report for configure time, executed compile/link edges, artifact sizes, and
  ccache summary. Use trend reporting before enforcing hard timing thresholds on shared runners.
- Add deterministic structural guards where possible: focused presets must keep tests/examples/C
  API and optional backends at their documented values; raw routine policy must not return to
  `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, or global linker flags.
- Re-run the full integration build and relevant platform/renderer configurations before closing
  the plan.

### Acceptance

- Every retained optimization has reproducible before/after evidence and a documented disable path.
- Full integration tests and installed-consumer checks pass.
- The final report distinguishes clean-build, incremental-build, configure, link, and cache gains;
  it does not present compile-command reduction as an elapsed-time benchmark.

## 13. Explicit non-goals

- No global `-march=native`, `-Ofast`, or `-ffast-math`; distribution portability and XNA numerical
  behavior take priority.
- No project-wide PCH or unity switch without the target-specific pilots above.
- No default IPO/LTO for installed static archives. The existing guarded release experiment stays
  opt-in.
- No generic PGO campaign until CNA has a deterministic workload representative of real client
  applications.
- No C++ modules migration as a build-speed shortcut; toolchain, dependency scanning, package
  consumption, and cross-compile support require a separate design.
- No global ccache configuration changes from CMake and no misleading hit-rate target independent
  of the actual workload.
