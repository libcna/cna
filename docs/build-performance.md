# Build performance and compiler policy

## Purpose

CNA has a large, modular C++ build: renderer selection, optional backends, the C API, tests, demo
applications, and the sibling Sharp Runtime can make a full integration configuration substantially
larger than a focused edit needs to be. This document defines the supported fast paths without
changing the coverage expected from CI and release configurations.

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

This setting is intentionally inactive on Apple, Windows/MSVC, Android, Emscripten, and cross-builds.

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

### Reproducible GCC versus Clang clean build

`tools/build/benchmark_clean_build.py` runs one clean benchmark in a new directory and writes the
full evidence to `benchmark-result.json`. An explicit directory must not exist and its basename
must start with `cmake-build-benchmark-`; the script never deletes a build directory. With
`--ccache isolated`, the cache and temporary directory are private to that build, so an empty
directory is a verifiable cold-cache start.

The 2026-08-29 comparison at commit `28056b129` used Debug, full debug information, the STUB
renderer, Mold, 12 jobs, and the default `cna_tool_cnb_info` target. Tests, examples, C API,
networking, video, Draco, PCH, unity, and IPO were disabled in both configurations:

```sh
python3 tools/build/benchmark_clean_build.py \
  --label gcc-14.2.0 --cxx-compiler /usr/bin/g++ --c-compiler /usr/bin/gcc \
  --parallel 12 --linker MOLD --ccache isolated \
  --build-dir /tmp/cmake-build-benchmark-gcc14

python3 tools/build/benchmark_clean_build.py \
  --label clang-19.1.7 --cxx-compiler /usr/bin/clang++ --c-compiler /usr/bin/clang \
  --parallel 12 --linker MOLD --ccache isolated \
  --build-dir /tmp/cmake-build-benchmark-clang19
```

| Metric | GCC 14.2.0 | Clang 19.1.7 |
| --- | ---: | ---: |
| Configure wall time | 4.27 s | 4.67 s |
| Clean target build | **83.45 s** | 90.02 s |
| Peak build RSS | 864 MiB | **486 MiB** |
| No-op build | 0.12 s | 0.12 s |
| Ninja commands / compilations | 508 / 483 | 508 / 483 |
| Artifact size | 1,299,360 B | **999,752 B** |
| Build tree excluding ccache | 590,767,494 B | **382,313,116 B** |
| Isolated ccache size | 71,246,162 B | **49,074,291 B** |
| ccache misses / hits | 483 / 0 | 483 / 0 |

GCC was 7.3% faster for this clean workload. Clang used 43.8% less peak memory, produced a 35.3%
smaller build tree and a 23.1% smaller executable. The zero direct and preprocessed hits are
expected here: both isolated caches began empty, and the measured second invocation was a no-op
with no compiler calls. On this machine GCC is therefore the faster default for clean development
builds, while Clang remains valuable for its lower resource use, diagnostics, `-ftime-trace`, and
independent CI coverage. This single target is not evidence that either compiler wins every CNA
configuration.

## Deferred techniques

Precompiled headers, unity builds, PGO, source partitioning, `-march=native`, `-Ofast`, and
`-ffast-math` are intentionally not global defaults. They require a measured, target-specific case:
they can make clean builds faster while harming incremental rebuilds, portability, binary packaging,
or XNA numerical behaviour.
