# Build performance and compiler policy

## Purpose

CNA has a large, modular C++ build: renderer selection, optional backends, the C API, tests, demo
applications, and the sibling Sharp Runtime can make a full integration configuration substantially
larger than a focused edit needs to be. This document defines the supported fast paths without
changing the coverage expected from CI and release configurations.

## Accepted results at a glance

These measurements deliberately separate clean compilation, incremental work, configuration,
linking, and cache behavior. Compile-command counts are structural scope evidence only; they are
not presented as elapsed-time improvements.

| Area | Retained measured result | Default or disable path |
| --- | --- | --- |
| Focused test loop | A math test-source rebuild fell 71% (0.84 to 0.24 s); its final link fell 73% (0.75 to 0.20 s). The 88.5% graph reduction is supporting structure, not a timing claim. | Build `CnaTests` for the complete suite. |
| Configure audits | Unchanged configure fell about 82% (4.39–4.72 to 0.83–0.84 s). | `-DCNA_CONFIGURE_AUDIT_CACHE=OFF` forces every audit. |
| Content-test PCH | Clean content-test compilation fell 23.5% (71.90 to 55.03 s). | Default `CNA_ENABLE_PCH=OFF`; use `unit-pch` only for the pilot. |
| Header hygiene | The controlled `ContentReader.hpp` consumer frontend fell 24.5%; seven-header parse count fell 30.6%. | Retained source cleanup; no compiler-mode dependency. |
| Fast debug | Controlled content compilation fell 9.7% and the build tree fell 47.2% with line tables. | `CNA_DEBUG_INFO=FULL` is default; `dev-fast-debug` opts in. |
| Clean-CI unity | Four-job core/math clean closure fell 25.6%; a one-source rebuild regressed 5.7%. | Default `CNA_ENABLE_UNITY_BUILD=OFF`; `unit-unity` is leaf-only. |
| Final linking | Large Debug `CnaTests` link fell 90.9% with LLD and 89.9% with Mold versus GNU ld. | `CNA_LINKER=DEFAULT`; `AUTO`, `LLD`, and `MOLD` are explicit alternatives. |
| ccache | A public-header rebuild recovered 23/23 compile edges as direct hits in 0.36 s, with no preprocessed hits or eviction. | `CNA_USE_CCACHE=OFF`; CNA never changes global sloppiness/direct-mode policy. |
| Complete clean build | At 12 jobs GCC took 10:44 and Clang 10:05 for all 1,929 compile edges; this is the post-optimization machine baseline, not an improvement inferred from command counts. | Use a focused profile for normal edits; keep complete GCC/Clang integration in CI. |

Split DWARF and broad default unity/PCH remain deferred: the measured tradeoffs did not justify
making them normal developer policy. IPO/LTO remains an opt-in release experiment rather than a
compilation-speed feature.

## Presets

All new performance-oriented presets use Ninja and write `compile_commands.json`.

| Preset | Intended use | Deliberately omitted |
| --- | --- | --- |
| `dev` | Fast local code/edit/link loop; builds `cna_tool_cnb_info` | Tests, demos, C API, networking, FFmpeg, Draco |
| `dev-fast-debug` | Same edit loop with line-table-only debug information | Same as `dev`; local-variable/type debugger detail |
| `unit` | Portable complete unit-test suite; builds `CnaTests` | Demos, C API, networking, FFmpeg, Draco |
| `unit-pch` | Opt-in content-test PCH pilot; builds `CnaContentTests` | Same as `unit`; PCH is limited to the content test object group |
| `unit-unity` | Opt-in clean-CI core/math unity pilot | Same as `unit`; unity is limited to core/math libraries and focused test objects |
| `release-modules` | Optimized module/content-tool validation | Tests, demos, C API, networking, FFmpeg, Draco |
| `release-ipo` | Explicit IPO/LTO measurement build | Same as `release-modules`; IPO is opt-in |

Examples:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 12

cmake --preset unit
cmake --build --preset unit --parallel 12
./cmake-build-unit/CnaTests
```

### Focused unit-test executables

The unit configuration compiles each physical module's tests once through an object library. The
objects feed both the complete `CnaTests` executable and a module-focused executable, so a developer
can avoid building and linking unrelated test code without changing the full-suite compatibility
path. Focused executables are intentionally not registered as duplicate CTest tests.

The common build presets cover the most useful edit loops:

```sh
cmake --preset unit
cmake --build --preset unit-math --parallel 12
SDL_AUDIODRIVER=dummy ./cmake-build-unit/CnaMathTests

cmake --build --preset unit-content --parallel 12
SDL_AUDIODRIVER=dummy ./cmake-build-unit/CnaContentTests
```

The available focused targets are `CnaAudioTests`, `CnaContentTests`, `CnaCoreTests`,
`CnaDevicesTests`, `CnaDevicesExtTests`, `CnaGamerServicesTests`, `CnaGraphicsTests`,
`CnaGraphicsExtTests`, `CnaInputModuleTests`, `CnaIntegrationTests`, `CnaMathTests`,
`CnaMediaTests`, `CnaNetTests`, `CnaPlatformModuleTests`, `CnaRendererTests`,
`CnaRuntimeTests`, and `CnaStorageTests`. The `unit-core`, `unit-math`, `unit-content`, and
`unit-graphics` build presets are shortcuts; use `cmake --build cmake-build-unit --target <target>`
for the other modules.

On the 2026-08-28 reference STUB/GCC configuration, Ninja reported the following compile-command
graph sizes. These are structural measurements, not elapsed-time promises:

| Test target | Compile commands | Reduction from complete `CnaTests` |
| --- | ---: | ---: |
| `CnaTests` | 1,126 | baseline |
| `CnaContentTests` | 632 | 43.9% |
| `CnaGraphicsTests` | 376 | 66.6% |
| `CnaMathTests` | 130 | 88.5% |
| `CnaCoreTests` | 105 | 90.7% |

The normalized `--gtest_list_tests` inventory of the complete executable remained byte-identical
across the split (8,102 output lines). A focused `CnaMathTests` build ran all 840 math tests in 2 ms;
the STUB configuration's content suite retains three existing GPU-capability failures for 3D/cube
textures and is not evidence for those renderer-dependent cases.

With a warm ccache and 12-way Ninja parallelism on the same machine, touching
`MathHelperTests.cpp` and rebuilding took 0.24 s through `CnaMathTests` versus 0.84 s through
`CnaTests` (71% less wall time). A forced final link took 0.20 s versus 0.75 s (73% less). The full
target retains the same 1,126 compile commands as before the split, and its graph contains no
duplicate commands; focused targets are `EXCLUDE_FROM_ALL`, so they add no work to a full build.

Do not use `dev` as merge evidence for a renderer-, C API-, network-, media-, or Draco-specific
change. Existing renderer/platform/integration presets remain the required evidence for the code
they include.

## Configure-time audit cache

`CNA_CONFIGURE_AUDIT_CACHE=ON` (the default) caches only successful platform ratchet,
non-production SDL, and hot-path audit results under the current build directory's `CMakeFiles/`.
The key is a SHA-256 content fingerprint of every source/header the audits inspect, their scripts
and budgets, the Python version, and the exact command. File additions, removals, content changes,
strict-mode changes, or audit implementation changes therefore invalidate the result. Failed
audits are never cached, and a fresh CI build directory always executes every audit.

All three audits share one fingerprint pass over the module tree. Set
`-DCNA_CONFIGURE_AUDIT_CACHE=OFF` to force execution during every configure, or run the existing
`cna_platform_ratchet`, `cna_platform_nonproduction_sdl_audit`, and
`cna_platform_hot_path_lint` targets for an explicit check.

The 2026-08-28 reference `dev` measurement found 4.42 s of self time in configure-time
`execute_process()` calls: 1.45 s for the platform ratchet, 1.45 s for the non-production audit,
and 1.30 s for the hot-path lint. After content-aware caching and replacing the physical-module
validator's nested module scan with two equivalent combined regular expressions, repeated
unchanged configure time fell from 4.39–4.72 s to 0.83–0.84 s (about 82% less). A cached `unit`
configuration took 1.26 s. A changed audit input still runs the complete checks; forcing the cache
off took 3.99 s on the same machine.

The source glob behavior is unchanged. Adding a temporary module `.cpp` produced Ninja's
`GLOB mismatch`, regenerated CMake, refreshed the audits, compiled the new object, and removing it
regenerated again. A temporary forbidden `SDL_CreateWindow` reference invalidated the cache and
made configuration fail at the strict ratchet gate, proving that caching does not weaken the
correctness check.

## Content-test PCH pilot

`CNA_ENABLE_PCH=ON` enables the COMP-003 pilot only for the `cna_content_test_objects` target.
The private precompiled header contains GoogleTest and stable standard-library headers; CNA public
headers deliberately remain textual. The option defaults to `OFF`, does not affect installed
consumers or third-party targets, and has a dedicated preset:

```sh
cmake --preset unit-pch
cmake --build --preset unit-content-pch --parallel 12
SDL_AUDIODRIVER=dummy ./cmake-build-unit-pch/CnaContentTests
```

ccache's safe defaults do not fully support PCH reuse. The documented ccache PCH path requires
relaxing `pch_defines` and `time_macros` checks, so CNA does not change global sloppiness. When both
features are requested, only the content-test object target bypasses ccache; its dependency graph
continues to use the configured launcher.

The 2026-08-28 GCC 14.2.0 measurement rebuilt only the 140 content-test translation units after
prebuilding an identical dependency graph. Both configurations used Debug/STUB, Ninja, Mold,
`--parallel 12`, and ccache disabled. Each mode was run twice:

| Mode | Run 1 | Run 2 | Mean | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| PCH off | 69.83 s | 73.96 s | 71.90 s | 533 MiB |
| PCH on | 54.18 s | 55.88 s | 55.03 s | 548 MiB |

The PCH reduced clean content-test compilation by 23.5%, exceeding the 15% pilot threshold, while
peak memory rose by about 2.8%. Clang 19.1.7 also compiled and linked the same PCH target. The
focused executable preserved its test inventory; its STUB run retained only the renderer/shader
capability failures also observed without PCH.

## Clang header and translation-unit traces

`tools/build/analyze_clang_time_trace.py` aggregates Clang `-ftime-trace` JSON without modifying
the build tree. It ranks translation units by `Total ExecuteCompiler` and project headers by
inclusive `Source` time, and can emit text or JSON. A profiling build can be reproduced with an
isolated build directory (the raw flag is intentionally confined to this diagnostic build):

```sh
cmake -S . -B /tmp/cna-trace -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-ftime-trace -ftime-trace-granularity=500' \
  -DCMAKE_BUILD_TYPE=Debug -DCNA_GRAPHICS_RENDERER=STUB \
  -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF -DCNA_BUILD_C_API=OFF \
  -DCNA_ENABLE_NET=OFF -DCNA_ENABLE_VIDEO=OFF -DCNA_ENABLE_DRACO=OFF \
  -DCNA_USE_CCACHE=OFF
cmake --build /tmp/cna-trace --target cna_content --parallel 12
python3 tools/build/analyze_clang_time_trace.py \
  /tmp/cna-trace/modules/content/CMakeFiles/cna_content.dir --top 20
clang-scan-deps-19 -compilation-database=/tmp/cna-trace/compile_commands.json \
  -format=make -j 12 -o /tmp/cna-trace-dependencies.make
```

The 2026-08-28 Clang 19.1.7 baseline identified these leading production content TUs:
`ContentManager.cpp` (13.31 s), `GltfImportCore.cpp` (9.70 s), `CnbModelCodec.cpp` (6.73 s),
`CnbModelFromCnj.cpp` (5.66 s), and `CnjToCnb.cpp` (5.45 s). The leading project headers were
`ContentReader.hpp` (23.71 s across 20 parses), `SkinnedModelEXT.hpp` (9.77 s across six), and
`ContentManager.hpp` (9.40 s across 13). These are inclusive trace times and can overlap; they are
ranking evidence, not values to add together.

The retained include-hygiene change removes seven complete math headers from `ContentReader.hpp`
and forward-declares their return types. `ContentReader.cpp` and the one test that constructs
`Color`/`Vector4` now include their actual dependencies explicitly. Across the 46 production
content TUs, parses of those seven headers fell from 173 to 120 (30.6%); their inclusive trace time
fell from 3.67 s to 2.27 s (38.1%). A three-run, single-job consumer probe's mean Clang frontend
time fell from 1,308 ms to 988 ms (24.5%). `clang-scan-deps` reports 24 TUs in the complete focused
build closure depending on `ContentReader.hpp`, so the reduction also narrows public-header rebuild
fan-out rather than optimizing only its own implementation TU.

Single clean traced target samples varied from 83.80 s before to 95.53 s after while peak RSS stayed
essentially flat at 490 MiB, so they are not used as a whole-target speed claim. The accepted metric
is the controlled header probe plus the deterministic parse-count reduction. GCC 14.2.0 and Clang
19.1.7 both build `CnaContentTests`, and all 21 directly affected `ContentReader` tests pass on both.

## Fast debug information

`CNA_DEBUG_INFO` controls debug information only for CNA-owned and enabled Sharp Runtime targets:

- `FULL` is the default and preserves the toolchain's normal Debug information;
- `LINE_TABLES` appends `-g1` on GCC or `-gline-tables-only` on Clang;
- `SPLIT` appends `-gsplit-dwarf` as an explicit experiment.

`dev-fast-debug` selects `LINE_TABLES` in its own build directory:

```sh
cmake --preset dev-fast-debug
cmake --build --preset dev-fast-debug --parallel 12
```

Line tables retain source-level stacks, breakpoints, symbols, and address-to-line resolution, but
not the full local-variable and type inspection data. Use the regular `dev` preset when a debugger
session needs that information. Non-full modes are restricted to native GNU/Clang Debug builds and
cannot be combined with sanitizers; vendored targets remain outside this project policy.

The 2026-08-28 GCC 14.2.0 Debug/STUB/Mold measurement used ccache off and `--parallel 12` after
prebuilding an identical dependency graph:

| Mode | 46-source `cna_content` rebuild | Peak RSS | Content objects | Build tree | Final tool |
| --- | ---: | ---: | ---: | ---: | ---: |
| `FULL` | 38.76 s | 865 MiB | 68.9 MiB | 590.6 MB | 1.30 MB |
| `LINE_TABLES` | 34.99 s | 721 MiB | 39.8 MiB | 312.1 MB | 0.62 MB |
| `SPLIT` | 38.32 s | 865 MiB | 42.7 MiB + 40.3 MiB `.dwo` | 605.4 MB | 1.07 MB |

Line tables improved that controlled compile by 9.7%, reduced peak memory by 16.6%, content object
size by 42.3%, the complete build tree by 47.2%, and the final tool by 52.5%. Two single-job
`ContentManager.cpp` rebuilds averaged 9.20 s with full debug information and 8.30 s with line tables
(9.8% faster). Mold made the final relink effectively identical (0.21 s versus 0.20 s), and no-op
builds were 0.14 s versus 0.13 s.

The first full clean-closure samples varied in the opposite direction (104.94 s full, 126.72 s line
tables, 166.84 s split) as the host load changed, so they are disclosed but not used as the accepted
speed metric. Split DWARF provided no meaningful controlled compile gain and made the total tree
larger, so it has no preset. Both modes remain selectable for other machines. The line-table tool
contains 18,922 decoded source-line rows and runs normally; Clang 19.1.7 also builds and runs the
same preset with `-gline-tables-only`.

## Clean-CI unity pilot

`CNA_ENABLE_UNITY_BUILD=ON` is deliberately narrow and defaults to `OFF`. It merges only `cna_core`,
`cna_math`, and their two focused test object targets. Production math sources use six explicit
groups of at most three files so repeated anonymous helpers such as `FloatHash` never share a TU;
the eight core sources form one verified group. Focused tests use batches of at most eight files.
Several math tests independently call their tolerance `kEps`, so the generated unity wrapper uses
CMake's per-source unique identifier to rename that internal symbol only during unity compilation.
Normal source files and public API remain unchanged.

The dedicated preset is intended for a clean, constrained CI worker, not the normal edit loop:

```sh
cmake --preset unit-unity
cmake --build --preset unit-core-math-unity --parallel 4
SDL_AUDIODRIVER=dummy ./cmake-build-unit-unity/CnaCoreTests
SDL_AUDIODRIVER=dummy ./cmake-build-unit-unity/CnaMathTests
```

Two clean GCC 14.2.0 Debug/STUB/Mold builds with ccache off and four jobs measured 45.67/46.08 s
without unity and 34.14/34.09 s with unity. The means are 45.88 s versus 34.12 s, a 25.6%
improvement that meets the pilot threshold. Compile edges fell from 145 to 101 (30.3%), selected
object size from 40.75 MB to 23.18 MB (43.1%), and the build tree from 197.57 MB to 173.26 MB
(12.3%). Peak RSS rose from about 465 MiB to 502 MiB (8.0%), within the reference worker limit.

At 12 jobs the same clean closure improved only from 28.51 s to 23.07 s (19.1%): wide ordinary
parallelism reduces unity's benefit. Rebuilding just the 25 production core/math sources improved
from 7.51 s to 1.65 s (78.0%). A one-source `Vector3.cpp` edit was slightly worse (0.87 s to
0.92 s), while a public `Vector3.hpp` rebuild improved from 1.74 s to 1.47 s. Final relinking
(0.19/0.20 s) and no-op builds (0.13 s each) were unchanged. These tradeoffs are why unity remains
opt-in and why the preset documents four-way clean CI rather than replacing `unit`.

All 63 core and 840 math tests pass with GCC 14.2.0 and Clang 19.1.7. The same suites pass under
GCC AddressSanitizer; leak detection alone was disabled because the execution sandbox runs under
ptrace, while address/ODR instrumentation remained active. Third-party targets, other modules,
cross-builds, Emscripten, installed consumers, and ordinary presets are unaffected.

## Compiler-policy layers

The CMake targets distinguish requirements imposed on a consumer from CNA's private build policy:

| Target | Visibility | Responsibility |
| --- | --- | --- |
| `CNA::BuildConfig` | Public | C++23 and preprocessor/build facts a public header or ABI requires, including the selected renderer and feature macros. |
| `CNA::ProjectOptions` | Private to CNA-owned targets | Non-ABI compiler policy, currently deterministic MSVC UTF-8 source decoding. |
| `CNA::DebugInfoOptions` | Private to CNA-owned targets and Sharp Runtime | Optional Debug-only line-table or split-DWARF policy. |
| `CNA::Instrumentation` | Private to CNA-owned targets and Sharp Runtime | Sanitizer compile/link options. |
| `CNA::EmscriptenAbi` | Public where required, private internally | The Emscripten exception/Asyncify ABI shared by CNA, Sharp Runtime, and final applications. |
| `CNA::LinkerOptions` | Private to CNA-owned targets | An explicitly selected native fast linker. |

New warning, diagnostic, sanitizer, linker, coverage, or profiling flags belong in the narrowest
appropriate layer. Do not add routine policy to `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, or a global
`add_compile_options()` call.

## ccache

`CNA_USE_CCACHE=ON` is the default when `ccache` is available. CNA invokes it with
`CCACHE_BASEDIR` set to `CNA_CCACHE_BASEDIR`, which defaults to the common parent of the CNA and
Sharp Runtime source directories. This makes source paths stable across their separate build trees.

Inspect the actual cache instead of assuming it helps:

```sh
cmake --build cmake-build-dev --target cna_ccache_stats
```

If eviction is frequent, increase the cache only after confirming persistent disk capacity, for
example with `ccache --max-size 40G`. A shared cache is most effective when developer worktrees use
the same `CNA_CCACHE_BASEDIR`. Do not enable `CCACHE_NOHASHDIR` globally: it needs a separate
reproducible-debug-path and `__FILE__` compatibility check.

Interpret direct/preprocessed hit counters as workload deltas, not as a lifetime percentage. A
2026-08-28 investigation of an apparent 0% direct-hit rate showed `direct_mode = true` and a debug
log result of `direct_cache_hit`. The counter did not advance because that sandboxed build could
read cached objects but could not create ccache's statistics file under its configured temporary
directory; the log ended with `Error while finalizing stats`. Repeating the same rebuild with a
writable cache advanced the direct-hit counter from zero to one. This was an observation-environment
problem, not a CNA launcher or ccache configuration defect, so CNA does not override the user's
global direct-mode or sloppiness settings.

For a trustworthy one-file diagnosis, record stats before and after the same rebuild in an
environment that can write both `cache_dir` and `temporary_dir`. When the aggregate counters are
surprising, enable ccache's per-invocation debug log temporarily:

```sh
mkdir -p /tmp/cna-ccache-debug
touch modules/math/tests/Microsoft/Xna/Framework/MathHelperTests.cpp
CCACHE_DEBUG=1 CCACHE_DEBUGDIR=/tmp/cna-ccache-debug \
  cmake --build cmake-build-unit --target CnaMathTests --parallel 1
rg 'Result:|Error while finalizing stats' /tmp/cna-ccache-debug
```

Three writable-cache workloads on 2026-08-29 used the existing 50 GiB global cache with its
configuration and accumulated contents unchanged. Each row is the before/after counter delta for
one `cmake-build-unit` invocation:

| Edited input and target | Wall time | Direct hits | Preprocessed hits | Misses | Uncacheable | Cleanups |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `MathHelperTests.cpp` → `CnaMathTests` | 0.27 s | 1 | 0 | 0 | 0 | 0 |
| `ContentReader.cpp` → `CnaContentTests` | 4.07 s | 0 | 0 | 1 | 0 | 0 |
| `Vector3.hpp` → `CnaMathTests` | 0.36 s | 23 | 0 | 0 | 0 | 0 |

The production-source miss is legitimate: that exact object and current command were absent from
the cache, and its time includes eight dependent archive/executable link steps. The public-header
case is the stronger direct-mode check: all 23 invalidated compile edges were recovered without
running the preprocessor and without growing or cleaning the cache.

## Sanitizers

Set `CNA_SANITIZE` instead of writing raw CMake flag variables. The existing device presets use:

```sh
cmake -S . -B cmake-build-asan -G Ninja \
  -DCNA_GRAPHICS_RENDERER=OPENGLES3 \
  -DCNA_SANITIZE=address \
  -DCNA_SANITIZE_OPTIMIZATION=O0
```

`CNA_SANITIZE_OPTIMIZATION` accepts `DEFAULT`, `O0`, `O1`, `O2`, or `O3`. The instrumentation
target applies both compile and final-link options to CNA-owned targets and the enabled Sharp Runtime
component closure; vendored SDL and ENet sub-builds intentionally remain uninstrumented. Address and
Thread sanitizers, and Thread and Memory sanitizers, are rejected as incompatible combinations.

Sanitizer configurations are native GNU/Clang builds only. They are diagnostics, not release
artifacts, and cannot be combined with `CNA_ENABLE_IPO`.

## Linker selection and IPO/LTO

`CNA_LINKER` has these values:

- `AUTO` (default): choose Mold, then LLD, only when a native GNU/Clang ELF probe succeeds;
- `DEFAULT`: always use the toolchain default;
- `MOLD` or `LLD`: require the named linker and fail at configure time if it is unavailable.

`AUTO` retains the toolchain default on Apple, Windows/MSVC, Android, Emscripten, and cross-builds;
an explicit `MOLD` or `LLD` request is rejected there rather than silently ignored.

### Final-link benchmark

`tools/build/benchmark_final_link.py` reads an existing Ninja graph, requires exactly one matching
final-link command, and invokes that command directly. It does not touch source/object inputs or
mix compilation into the timer. On Linux it samples the complete compiler-driver/linker process
tree through `/proc`, avoiding the incomplete RSS values produced by timing a nested
`cmake --build` wrapper. Each JSON report includes every repetition, the exact command, artifact
size, Git revision, platform, and tool versions.

After building the target once, reproduce a linker variant with:

```sh
cmake -S . -B cmake-build-unit -DCNA_LINKER=LLD
python3 tools/build/benchmark_final_link.py \
  --build-dir cmake-build-unit --target CnaTests --artifact CnaTests \
  --label unit-lld --iterations 5 --output /tmp/unit-lld-link.json

cmake -S . -B cmake-build-release-modules -DCNA_LINKER=LLD
python3 tools/build/benchmark_final_link.py \
  --build-dir cmake-build-release-modules --target cna_tool_cnb_info \
  --artifact cna_tool_cnb_info --label release-lld --iterations 7 \
  --output /tmp/release-lld-link.json
```

Repeat with `DEFAULT` and `MOLD`. The 2026-08-29 reference used GCC 14.2.0, GNU ld 2.44, LLD
19.1.7, Mold 2.37.1, STUB, full Debug information for tests, and ordinary Release for the tool.
All variants reused identical object files.

| `CnaTests` metric | GNU ld | LLD | Mold |
| --- | ---: | ---: | ---: |
| Median final link (5 runs) | 10.638 s | **0.972 s** | 1.073 s |
| Improvement from GNU ld | baseline | **90.9%** | 89.9% |
| Peak process-tree RSS | **1,566 MiB** | 1,855 MiB | 1,943 MiB |
| Executable size | **330,289,832 B** | 337,507,472 B | 350,046,664 B |
| Interleaved startup median | 58.5 ms | **58.1 ms** | 59.2 ms |

| Release `cna_tool_cnb_info` metric | GNU ld | LLD | Mold |
| --- | ---: | ---: | ---: |
| Median final link (7 runs) | 0.123 s | 0.063 s | **0.048 s** |
| Improvement from GNU ld | baseline | 48.7% | **60.9%** |
| Peak process-tree RSS | **29 MiB** | 105 MiB | 126 MiB |
| Executable size | 158,008 B | **153,064 B** | 167,800 B |
| Startup median | 2.0 ms | 1.9 ms | 2.1 ms |

Both fast linkers exceed the 20% acceptance threshold. LLD won the large Debug test link and used
less memory than Mold; Mold won the small Release link. `AUTO` remains probe-based and prefers
Mold, a good general local choice, with LLD equally recommended when large Debug links dominate.
GNU ld remains a valid dependency-free fallback. All three `CnaTests` binaries ran the same 7,427
tests and produced an identical normalized result set: 6,868 passed, 490 skipped, and the same 69
baseline STUB/sandbox failures. Every Release tool emitted the expected usage output.

The accompanying minimal probe also verifies three policy edges: one build directory can switch
from Mold to LLD without a stale cached program path, an explicitly unavailable linker fails with
an actionable diagnostic, and cross-build `AUTO` contributes no linker option.

`CNA_ENABLE_IPO=ON` enables CMake IPO/LTO only for supported native `Release`, `RelWithDebInfo`, or
`MinSizeRel` CNA-owned targets after `CheckIPOSupported` succeeds. It is off by default because it
increases compile/link time and is not a general iteration-speed optimization. It is refused for
sanitizer builds and while `CNA_C_API_BUILD_STATIC=ON`, since exporting an LTO static C API archive
would force downstream consumers to use a compatible LTO linker.

## Measurement

Compare a candidate only against a build with the same compiler, renderer, optional dependencies,
cache state, and target. Record:

1. configure time;
2. cold target build time and peak memory at a stated `--parallel` value;
3. no-op build time and `cna_ccache_stats` before/after;
4. one-source and one-public-header incremental rebuild time;
5. final-link time, output size, and relevant tests.

Start local parallelism experiments at 8, 12, and 16 jobs on a 16-logical-CPU host, and keep the
largest value that does not cause memory pressure. `CNA_MAX_VENDORED_BUILD_JOBS` controls the nested
configure-time SDL build separately; it is not the main Ninja parallelism limit.

### CI trend report and structural guard

`tools/build/check_build_performance_policy.py` resolves preset inheritance and fails
deterministically if the focused `dev`, `unit`, or `release-modules` closures regain tests,
examples, the C API, or optional backends. It also verifies that PCH, unity, and IPO remain opt-in
and rejects routine project policy expressed through global `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS`,
linker flags, `add_compile_options()`, or `add_link_options()`. The isolated sanitizer flags used
to build the standalone C API consumer are the only narrow, explicit exception.

The general Linux workflow records the actual configure and build commands with `/usr/bin/time`,
keeps the Ninja build log, and runs `tools/build/report_build_performance.py` after the build. Its
uploaded `build-performance-report.json` contains:

- configure/build wall time and GNU-time maximum RSS (the reproducible clean-build driver uses the
  stricter aggregate process-tree RSS measurement);
- complete graph command and compile counts;
- compile and link edges actually observed in that fresh build's Ninja log;
- selected artifact sizes; and
- direct/preprocessed ccache hits, misses, uncacheable reasons, cleanups, and cache size.

These values are retained as a 30-day CI artifact for trend inspection. Shared-runner timing has no
hard pass/fail threshold; only machine-independent preset and policy invariants are enforced.

### Reproducible clean-build matrix

`tools/build/benchmark_clean_build.py` runs one clean benchmark in a new directory and writes the
full evidence to `benchmark-result.json`. An explicit directory must not exist and its basename
must start with `cmake-build-benchmark-`; the script never deletes a build directory. With
`--ccache isolated`, the cache and temporary directory are private to that build, so an empty
directory is a verifiable cold-cache start. Linux peak RSS is the sampled sum of the complete
process tree, not only the outer CMake/Ninja process. Schema 2 also records load average before and
after each measured phase.

The three profiles select these reproducible closures:

| Profile | Target | Configuration and enabled closure | Commands / compilations |
| --- | --- | --- | ---: |
| `dev` | `cna_tool_cnb_info` | Debug/STUB; focused tool only | 508 / 483 |
| `unit` | `CnaTests` | Debug/STUB; complete unit-test executable | 1,164 / 1,126 |
| `legacy` | `all` | Debug/OPENGLES3; tests, examples, C API, CNAEXT, compiled effects, networking, video | 2,483 / 1,929 |

All profiles disable PCH, unity, IPO, Draco, and the configure-audit cache. The `dev` and `unit`
profiles also disable examples, the C API, CNAEXT, compiled effects, networking, and video. The
`legacy` profile intentionally reproduces the complete former `cmake-build-debug` closure and uses
an existing FNA3D checkout for reproducible MojoShader sources. The 2026-08-29 reference used GCC
14.2.0, Clang 19.1.7, Mold 2.37.1, CMake 3.31.6, Ninja 1.12.1, ccache 4.11.2, full debug
information, and a 16-logical-CPU/30-GiB host. Each of the 18 points had its own initially empty
cache. Times are single observed runs on a shared host, so the table is a machine baseline, not a
portable timing promise. `Clang unit/j12` is a disclosed load outlier: its configure phase alone
took 16.25 s while comparable focused points took about 5–7 s.

| Profile | Compiler | Jobs | Clean build | Peak process-tree RSS |
| --- | --- | ---: | ---: | ---: |
| `dev` | GCC | 8 / 12 / 16 | 106.5 / 98.4 / **85.5 s** | 2.61 / 3.39 / 4.33 GiB |
| `dev` | Clang | 8 / 12 / 16 | 108.2 / 97.3 / **94.3 s** | 1.80 / 2.59 / 3.23 GiB |
| `unit` | GCC | 8 / 12 / 16 | 412.8 / 379.1 / **362.8 s** | 2.75 / 3.74 / 4.72 GiB |
| `unit` | Clang | 8 / 12 / 16 | 453.5 / 520.2* / **370.0 s** | 1.95 / 2.73 / 3.54 GiB |
| `legacy` | GCC | 8 / 12 / 16 | 742.4 / 643.8 / **605.1 s** | 4.55 / 4.73 / 6.23 GiB |
| `legacy` | Clang | 8 / 12 / 16 | 662.9 / 605.2 / **582.3 s** | 3.42 / 4.65 / 7.39 GiB |

Focused no-op builds took 0.15–0.34 s; complete no-op builds took 0.39–1.00 s. Full-profile
configure phases took 7.90–8.92 s. Cold-cache counters matched the compile graph: 483 misses for
`dev`, 1,125 misses plus one intra-build preprocessed hit for `unit`, and 1,929 misses for the
complete profile. There were no direct hits because each isolated cache began empty; the subsequent
no-op invoked no compiler. The current complete graph has one additional non-compile asset-copy
edge compared with the earliest GCC points, after the benchmark exposed and fixed a parallel copy
race; all six complete measurements contain the same 1,929 compiler calls.

At 12 jobs the complete build took 643.75 s (10 min 44 s) with GCC and 605.18 s (10 min 05 s) with
Clang. Clang was 6.0% faster, peaked at 4.65 GiB versus GCC's 4.73 GiB, and produced a 20.80-GiB
tree versus GCC's 29.54-GiB tree (29.6% smaller). Clang's 12→16 gain was only 3.8% while RSS rose
58.8%; GCC gained 6.0% while RSS rose 31.7%. Twelve jobs is therefore the balanced full-build
default on this host, while 16 jobs is an opt-in choice when shortest wall time matters. Clang
`-j16` was the fastest complete point at 582.34 s (9 min 42 s). GCC was fastest for the focused
`dev` and `unit` closures, while Clang used less disk and usually less memory below saturation.
Keep both compilers in CI rather than declaring one universal winner.

Reproduce the full matrix with new output paths (the safety check intentionally rejects a rerun into
an existing directory):

```sh
for profile in dev unit; do
  for jobs in 8 12 16; do
    python3 tools/build/benchmark_clean_build.py \
      --label "comp001-${profile}-gcc-j${jobs}" --profile "${profile}" \
      --cxx-compiler /usr/bin/g++ --c-compiler /usr/bin/gcc \
      --parallel "${jobs}" --linker MOLD --ccache isolated \
      --build-dir "/tmp/cmake-build-benchmark-repro-${profile}-gcc-j${jobs}"
    python3 tools/build/benchmark_clean_build.py \
      --label "comp001-${profile}-clang-j${jobs}" --profile "${profile}" \
      --cxx-compiler /usr/bin/clang++ --c-compiler /usr/bin/clang \
      --parallel "${jobs}" --linker MOLD --ccache isolated \
      --build-dir "/tmp/cmake-build-benchmark-repro-${profile}-clang-j${jobs}"
  done
done

python3 tools/build/benchmark_clean_build.py \
  --label comp001-legacy-gcc-j12 --profile legacy \
  --cxx-compiler /usr/bin/g++ --c-compiler /usr/bin/gcc \
  --fna3d-source /path/to/FNA3D --parallel 12 --linker MOLD --ccache isolated \
  --build-dir /tmp/cmake-build-benchmark-repro-legacy-gcc-j12

python3 tools/build/benchmark_clean_build.py \
  --label comp001-legacy-clang-j12 --profile legacy \
  --cxx-compiler /usr/bin/clang++ --c-compiler /usr/bin/clang \
  --fna3d-source /path/to/FNA3D --parallel 12 --linker MOLD --ccache isolated \
  --build-dir /tmp/cmake-build-benchmark-repro-legacy-clang-j12
```

## Deferred techniques

Precompiled headers, unity builds, PGO, source partitioning, `-march=native`, `-Ofast`, and
`-ffast-math` are intentionally not global defaults. They require a measured, target-specific case:
they can make clean builds faster while harming incremental rebuilds, portability, binary packaging,
or XNA numerical behaviour.
