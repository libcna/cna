# Build performance and compiler policy

## Purpose

CNA has a large, modular C++ build: renderer selection, optional subsystems, the C API, tests, demo
applications, and the sibling Sharp Runtime can make a full integration configuration substantially
larger than a focused edit needs to be. This document defines the supported fast paths without
changing the coverage expected from CI and release configurations.

## Retained historical results at a glance

The COMP-001–010 measurements below mostly date to 2026-08-28/29; the shared-runtime record is
2026-09-21. They describe the then-current tree/toolchains and are retained from the completed
campaign (including `78afd08ab`), not rerun timings or current graph/test counts. Source partitions,
dependencies, defaults and tests have changed since. Historical STUB/sandbox failures are disclosed
outcomes, not passing qualification.

These records separate clean compilation, incremental work, configuration, linking and cache
behavior. Compile-command counts are structural scope evidence, not elapsed-time improvements.
Current verification is listed at the end; reproduce a comparable measurement only with an
explicitly matched configuration.

| Area | Retained measured result | Default or disable path |
| --- | --- | --- |
| Focused test loop | A math test-source rebuild fell 71% (0.84 to 0.24 s); its final link fell 73% (0.75 to 0.20 s). The 88.5% graph reduction is supporting structure, not a timing claim. | Build `CnaTests` for the complete suite. |
| Configure audits | Unchanged configure fell about 82% (4.39–4.72 to 0.83–0.84 s). | `-DCNA_CONFIGURE_AUDIT_CACHE=OFF` forces every audit. |
| Content-test PCH | Clean content-test compilation fell 23.5% (71.90 to 55.03 s). | Default `CNA_ENABLE_PCH=OFF`; use `unit-pch` only for the pilot. |
| Header hygiene | The controlled `ContentReader.hpp` consumer frontend fell 24.5%; seven-header parse count fell 30.6%. | Retained source cleanup; no compiler-mode dependency. |
| Fast debug | Controlled content compilation fell 9.7% and the build tree fell 47.2% with line tables. | `CNA_DEBUG_INFO=FULL` is default; `dev-fast-debug` opts in. |
| Clean-CI unity | Four-job core/math clean closure fell 25.6%; a one-source rebuild regressed 5.7%. | Default `CNA_ENABLE_UNITY_BUILD=OFF`; `unit-unity` is leaf-only. |
| Final linking | Large Debug `CnaTests` link fell 90.9% with LLD and 89.9% with Mold versus GNU ld. | `CNA_LINKER=AUTO` by default; `DEFAULT` keeps the toolchain linker, while `LLD`/`MOLD` request one explicitly. |
| Shared runtime | A Debug `VULKAN;OPENGL33` tree (2026-09-21, renderer since retired) fell from 87 GB to 5.4 GB; a test executable from ~104 MB to 0.5–0.9 MB; a renderer `.cpp` edit relinked 29 targets (~1.7 GB) instead of ~800 (~83 GB), and a rebuilt `libcna.so` relinks none. | `CNA_SHARED_LIBRARY=ON` is the default on native ELF GNU/Clang with CMake ≥ 3.30; `-DCNA_SHARED_LIBRARY=OFF` restores the static link. |
| ccache | A public-header rebuild recovered 23/23 compile edges as direct hits in 0.36 s, with no preprocessed hits or eviction. | `CNA_USE_CCACHE=OFF`; CNA never changes global sloppiness/direct-mode policy. |
| Complete clean build | At 12 jobs GCC took 10:44 and Clang 10:05 for all 1,929 compile edges (2026-08-29, before the shared machine's 8-job limit); this is the post-optimization machine baseline, not an improvement inferred from command counts. | Use a focused profile for normal edits; keep complete GCC/Clang integration in CI. |

Split DWARF and broad default unity/PCH remain deferred: the measured tradeoffs did not justify
making them normal developer policy. IPO/LTO remains an opt-in release experiment rather than a
compilation-speed feature.

## Presets

The performance-oriented presets use Ninja and write `compile_commands.json`. They select STUB,
rather than a rendering qualification configuration. On a fresh cache they omit GamerServices
through the NET-dependent default, but do not explicitly override a pre-existing
`CNA_ENABLE_GAMER_SERVICES=ON`, platform choice or every optional tool/extension switch. Inspect
the effective cache when reusing a directory; a preset is not a reset of unspecified cache values.

| Preset | Intended use | Deliberately omitted |
| --- | --- | --- |
| `dev` | Fast local code/edit/link loop; builds `cna_tool_cnb_info` | Tests, demos, C API, networking, FFmpeg, Draco |
| `dev-fast-debug` | Same edit loop with line-table-only debug information | Same as `dev`; local-variable/type debugger detail |
| `unit` | Aggregate unit-test executable; builds `CnaTests` | Demos, C API, networking, FFmpeg, Draco |
| `unit-pch` | Opt-in content-test PCH pilot; builds `CnaContentTests` | Same as `unit`; PCH is limited to the content test object group |
| `unit-unity` | Opt-in clean-CI core/math unity pilot | Same as `unit`; unity is limited to core/math libraries and focused test objects |
| `release-modules` | Optimized module/content-tool validation | Tests, demos, C API, networking, FFmpeg, Draco |
| `release-ipo` | Explicit IPO/LTO measurement build | Same as `release-modules`; IPO is opt-in |

Examples:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 8

cmake --preset unit
cmake --build --preset unit --parallel 8
tools/platform/run_gpu_tests_private.sh --exec ./cmake-build-unit/CnaTests
```

### Focused unit-test executables

The unit configuration compiles each physical module's tests once through an object library. The
objects feed both the complete `CnaTests` executable and a module-focused executable, so a developer
can avoid building and linking unrelated test code without changing the full-suite compatibility
path. Focused executables are intentionally not registered as duplicate CTest tests.

The common build presets cover the most useful edit loops:

```sh
cmake --preset unit
cmake --build --preset unit-math --parallel 8
SDL_AUDIODRIVER=dummy ./cmake-build-unit/CnaMathTests

cmake --build --preset unit-content --parallel 8
tools/platform/run_gpu_tests_private.sh --exec env SDL_AUDIODRIVER=dummy ./cmake-build-unit/CnaContentTests
```

Depending on enabled modules/test groups, the focused targets are `CnaAudioTests`,
`CnaContentTests`, `CnaContentPipelineTests`, `CnaCoreTests`, `CnaDesignTests`,
`CnaDiagnosticsTests`, `CnaDevicesTests`, `CnaDevicesExtTests`, `CnaGamerServicesTests`,
`CnaGraphicsTests`, `CnaGraphicsExtTests`, `CnaInputModuleTests`, `CnaInspectorTests`,
`CnaIntegrationTests`, `CnaMathTests`, `CnaMediaTests`, `CnaNetTests`, `CnaPhoneTests`,
`CnaPlatformModuleTests`, `CnaRendererTests`, `CnaRuntimeTests` and `CnaStorageTests`.
The `unit-core`, `unit-math`, `unit-content` and `unit-graphics` build presets are shortcuts;
use `cmake --build cmake-build-unit --target <target> --parallel 8` for others.

`CnaTests` aggregates enabled GoogleTest groups. It is not every CTest-registered executable,
configuration check, C API smoke, reference oracle or renderer example; build the required separate
targets before selecting those CTest entries. Run any window/GPU-capable selection through the
private runner shown above, including STUB configurations whose fixtures can still create windows.

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

In that historical pre-limit run, with a warm ccache and 12-way Ninja parallelism, touching
`MathHelperTests.cpp` and rebuilding took 0.24 s through `CnaMathTests` versus 0.84 s through
`CnaTests` (71% less wall time). A forced final link took 0.20 s versus 0.75 s (73% less). The full
target retains the same 1,126 compile commands as before the split, and its graph contains no
duplicate commands. Focused targets are `EXCLUDE_FROM_ALL`, so they add no work to a full build,
with one exception: `CnaInputModuleTests` is registered as the `CnaInputTests` CTest and is
therefore part of `all` (INP-AUD); it reuses the objects `CnaTests` compiles, adding one link.

Do not use `dev` as merge evidence for a renderer-, C API-, network-, media-, or Draco-specific
change. Existing renderer/platform/integration presets remain the required evidence for the code
they include.

## Configure-time audit cache

`CNA_CONFIGURE_AUDIT_CACHE=ON` (the default) caches only successful platform ratchet,
non-production SDL, and hot-path audit results under the current build directory's `CMakeFiles/`.
The key is a SHA-256 fingerprint of the declared module source/header inputs, listed scripts and
budgets, helper implementation, Python version and exact command. File additions/removals and
changes within that declared set, including strict-mode command changes, invalidate it. New
transitive script/data inputs must be added explicitly; this is not automatic dependency discovery.
Failed audits are never cached. A fresh directory executes enabled audits when their scripts and
Python are available; disabled or unavailable checks are not evidence of a cache hit or pass.

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

In the original cache qualification, adding a temporary module `.cpp` produced Ninja's
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
cmake --build --preset unit-content-pch --parallel 8
tools/platform/run_gpu_tests_private.sh --exec env SDL_AUDIODRIVER=dummy ./cmake-build-unit-pch/CnaContentTests
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
inclusive `Source` time, and can emit text or JSON. A profiling build needs a configuration of its
own: the raw flag is intentionally confined to this diagnostic build, and ccache is off so the
traces measure real compilation. That makes it a clean, uncached build, so run it only with the
owner's approval, in the shared `build-probe/` directory rather than `/tmp`, and delete it once the
traces are recorded:

```sh
cmake -S . -B build-probe/clang-time-trace -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-ftime-trace -ftime-trace-granularity=500' \
  -DCMAKE_BUILD_TYPE=Debug -DCNA_GRAPHICS_RENDERER=STUB \
  -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF -DCNA_BUILD_C_API=OFF \
  -DCNA_ENABLE_NET=OFF -DCNA_ENABLE_VIDEO=OFF -DCNA_ENABLE_DRACO=OFF \
  -DCNA_USE_CCACHE=OFF
cmake --build build-probe/clang-time-trace --target cna_content --parallel 8
python3 tools/build/analyze_clang_time_trace.py \
  build-probe/clang-time-trace/modules/content/CMakeFiles/cna_content.dir --top 20
clang-scan-deps-19 -compilation-database=build-probe/clang-time-trace/compile_commands.json \
  -format=make -j 8 -o build-probe/clang-time-trace/dependencies.make
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
cmake --build --preset dev-fast-debug --parallel 8
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
larger, so it has no preset. Both modes remain selectable for other machines. The measured line-table tool
contained 18,922 decoded source-line rows and ran normally; Clang 19.1.7 also built and ran that
preset with `-gline-tables-only`.

## Shared runtime library

`CNA_SHARED_LIBRARY` (Vulkan parity VKPAR-0017) links the CNA runtime into one shared
library, `libcna.so`, instead of statically into every executable that links `CNA`.

**Why.** The historical integration tree had hundreds of separate test/example executables.
Its static configuration repeated a broad runtime/debug closure: measured on
`cmake-build-vulkan` (Debug, 2026-09-21), 798 executables at ~104 MB each —
`.debug_*` ≈ 77 MB, `.strtab`/`.symtab` ≈ 17.5 MB, `.text` 7 MB — 83 GB in total, and one edit to a
renderer `.cpp` relinked all of them, writing ~83 GB. On a machine whose SSD wear the repository
rules exist to limit, that is the dominant cost of an edit–build–test cycle.

**How.** `cna_shared` is linked from every CNA-owned static archive in the `CNA` closure with
`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` (a shared library has no `main`, so without it the linker would
pull in nothing), with `LINK_LIBRARY_OVERRIDE_<lib>` resolving the feature conflict that
inter-module dependencies otherwise cause. `CNA` then gives consumers `libcna.so`, the
**compile-only** usage requirements of the archives (`$<COMPILE_ONLY:...>`), and the libraries those
archives depend on that are not inside `libcna.so` (SDL, X11, wgpu-native, sharp-runtime, ...).
`cmake/SharedRuntimeLibrary.cmake` holds the closure walk; "CNA-owned" means a non-imported target
whose `SOURCE_DIR` is under `modules/`. The umbrella avoids passing that complete static closure
as final link inputs. Mixed explicit-module dependencies can still appear transitively; the DSO
ordering/exclusion policy below prevents extraction of a second runtime. Direct static-only
consumers retain archive relinks, while `CMAKE_LINK_DEPENDS_NO_SHARED` avoids relinking an umbrella
consumer solely because `libcna.so` was rebuilt. Actual relink fan-out depends on the consumer graph.

**Measured**, same machine and configuration (`VULKAN` was retired on 2026-10-13; the figures stand
as recorded):

| | Static | Shared |
| --- | --- | --- |
| `cmake-build-vulkan` | 87 GB (48 GB without the 341 EasyGL binaries VKPAR-0016 removed) | 5.4 GB |
| Typical test executable | ~104 MB | 0.5–0.9 MB |
| `libcna.so` | — | 182 MB (both renderers, 98 725 exported symbols, 0 unresolved) |
| Relinked by a Vulkan renderer `.cpp` edit | ~800 targets, ~83 GB | 29 targets, ~1.7 GB |
| Relinked by a rebuilt `libcna.so` | — | 0 |
| Full suite, private display | `Vulkan_*` 319 passed / 51 failed | `Vulkan_*` 319 passed / 51 failed |

The recorded 29 targets are the then-current direct archive consumers, not a current target list.
Today `CnaTests` links the `CNA` umbrella; most focused groups/tools/probes use module composition,
while `CnaRendererTests` uses the umbrella dependency for renderer-state identity. Consult the
generated link lines rather than applying the old count to today's graph. The symbol-footprint
probes deliberately remain static-only ([link boundaries](link-size-architecture.md)).

**One copy of everything.** A symbol defined both in `libcna.so` and in an executable would give it
two copies of that code and of its static state. Naming the `CNA` interface first alone does not
guarantee ordering through STATIC/OBJECT wrappers and optional sidecars such as GamerServices,
Net and ContentPipelineBuild. The shared target therefore injects its DSO into consumers' direct
link dependencies and excludes direct module archives already contained in that DSO. A scoped
`--no-as-needed` link feature retains this early DSO even when the application's first CNA
reference comes from a later static framework; the previous linker state is restored immediately.
Module-only consumers do not inherit this shared-target interface and remain static.
The original `nm` investigation recorded only copy-relocated public data
(`R_X86_64_COPY`, such as `BlendState::Opaque`) rather than independently live strong copies.
That is historical binary evidence. Current mixed-consumer and C API runtime-identity tests protect
the relevant singleton contracts, rather than proving every future consumer's symbol table.

**Scope.** The policy enables this by default only for native non-Apple Unix GNU/Clang with
CMake ≥3.30, excluding Android, Emscripten and cross compilation. Other toolchains keep the static
runtime; an unsupported explicit ON is refused. Windows has a separately exported C API, but no
equivalent whole-C++ runtime DSO policy. In shared-runtime mode the shared C API routes its runtime
state through `libcna.so`, with Net/GamerServices included there when the adapter needs them;
it is not an independent self-contained second runtime. The packaged static C API has its own
archive-combination path. Switching mode can require recompilation for PIC; reuse the configured
variant instead of repeatedly toggling it.

The shared-mode minimum is higher than CNA's overall CMake minimum: CMake 3.30
added the link-feature deduplication needed by cyclic `WHOLE_ARCHIVE` archives.
3.28 and 3.29 repeat whole archives and produce duplicate definitions, even though
they understand `COMPILE_ONLY`. Older CMake defaults to static mode; an existing
cache or explicit `CNA_SHARED_LIBRARY=ON` is refused during configuration with an
upgrade/static-mode diagnostic (CNA-DEMOS-ISSUE-001). Static builds retain their
existing minimum. `CnaSharedRuntimeLinkage` protects cyclic shared/static consumers
and the actual-version gate; it can also be run with an older CMake executable.

## Clean-CI unity pilot

`CNA_ENABLE_UNITY_BUILD=ON` is deliberately narrow and defaults to `OFF`. It merges only `cna_core`,
`cna_math`, and their two focused test object targets. Production math sources use six explicit
groups of at most three files so repeated anonymous helpers such as `FloatHash` never share a TU;
the eight core sources form one verified group. Focused tests use batches of at most eight files.
Several math tests independently call their tolerance `kEps`, so the generated unity wrapper uses
CMake's per-source unique identifier to rename that internal symbol only during unity compilation.
The pilot does not rewrite normal source files or public APIs.

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

The recorded 63 core and 840 math tests passed with GCC 14.2.0 and Clang 19.1.7, and under
GCC AddressSanitizer; leak detection alone was disabled because the execution sandbox runs under
ptrace, while address/ODR instrumentation remained active. Third-party targets, other modules,
cross-builds, Emscripten, installed consumers, and ordinary presets are unaffected.

## Compiler-policy layers

The CMake targets distinguish requirements imposed on a consumer from CNA's private build policy:

| Target | Visibility | Responsibility |
| --- | --- | --- |
| `CNA::BuildConfig` | Public | C++23, public build/ABI facts, final-link section collection and applicable runtime-first ordering. |
| `CNA::ProjectOptions` | Private to CNA-owned targets | Non-ABI compiler policy, currently deterministic MSVC UTF-8 source decoding. |
| `CNA::DebugInfoOptions` | Private to CNA-owned targets and Sharp Runtime | Optional Debug-only line-table or split-DWARF policy. |
| `CNA::Instrumentation` | Private to CNA-owned targets and Sharp Runtime | Sanitizer compile/link options. |
| `CNA::EmscriptenExceptionAbi` | Public where required, private internally | The JS-lowered C++ exception ABI shared by CNA, Sharp Runtime, and final artifacts. |
| `CNA::EmscriptenAsyncify` | Final application executables only | Blocking/sleep support for application-style `Game::Run()` loops. |
| `CNA::EmscriptenAbi` | Compatibility composition | Both interfaces above for application targets that used the former combined contract. |
| `CNA::LinkerOptions` | Private to CNA-owned targets | The native fast linker selected by AUTO or an explicit request. |
| `CNA::SizeOptions` | Private to CNA-owned targets and Sharp Runtime | Optional per-function/data sections; final-link collection propagates through BuildConfig. |

New warning, diagnostic, sanitizer, linker, coverage, or profiling flags belong in the narrowest
appropriate layer. Do not add routine policy to `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, or a global
`add_compile_options()` call.

`-Werror` is never project-wide. CNA builds sibling and vendored code with several compiler
families, so treating warnings as errors stays a target-local decision, as
`cna_c_api_enable_strict_warnings` makes it for the C API's own targets.

## ccache

`CNA_USE_CCACHE=ON` is the default when `ccache` is available. CNA invokes it with
`CCACHE_BASEDIR` set to `CNA_CCACHE_BASEDIR`. Its initial default is a nonempty environment
`CCACHE_BASEDIR`, otherwise CNA's parent directory; an explicit/cache value persists on reconfigure.
A nonempty custom compiler launcher is preserved. Choose the same containing base for all
participating source/worktree paths to normalize them consistently.
It also makes `__FILE__` relative to the build directory, so a test must never locate its
fixtures through `std::filesystem::path(__FILE__)`; `tests/support/CNA/TestSupport/TestPaths.hpp`
finds the repository root from the working directory instead (COMP-010).

Inspect the actual cache instead of assuming it helps:

```sh
cmake --build cmake-build-dev --target cna_ccache_stats
```

On the shared development machine there is exactly one ccache, and its own `ccache.conf` governs
its size and its `hash_dir = false` setting (the parent `CLAUDE.md`, Rule 1). Never point
`CCACHE_DIR` at a new directory, and never export `CCACHE_MAXSIZE`, `CCACHE_HASHDIR` or
`CCACHE_NOHASHDIR`: the environment overrides the cache's configuration for everyone using it. A
shared cache is most effective when developer worktrees use the same `CNA_CCACHE_BASEDIR`.

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
mkdir -p cmake-build-unit/ccache-debug
touch modules/math/tests/Microsoft/Xna/Framework/MathHelperTests.cpp
CCACHE_DEBUG=1 CCACHE_DEBUGDIR="$PWD/cmake-build-unit/ccache-debug" \
  cmake --build cmake-build-unit --target CnaMathTests --parallel 1
rg 'Result:|Error while finalizing stats' cmake-build-unit/ccache-debug
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
On GCC, `undefined` is widened to `undefined,float-cast-overflow`: GCC's `undefined` group does
not include the float-to-integer overflow check that Clang's does, and an unclamped
`static_cast<int>(float)` would otherwise pass the GCC UBSan jobs silently.

LeakSanitizer reports from Mesa's GLX/EGL client libraries (`libGLX_mesa`, `libgallium`, `libEGL`)
during real GPU setup are driver leaks reproduced without any CNA code; run GL tests under LSan with
`LSAN_OPTIONS=suppressions=tools/platform/lsan_x11_mesa.supp`, whose entries each name the program
and byte count that proved them. No CNA production code works around them.

The instrumentation option requires a GNU/Clang-family compiler (AppleClang included), rejects
MSVC/Emscripten, and checks the named incompatible pairs; it does not prequalify every compiler,
sanitizer or cross target. The native diagnostic presets are not release artifacts and cannot be
combined with `CNA_ENABLE_IPO`.

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
  --label unit-lld --iterations 5 --output cmake-build-unit/unit-lld-link.json

cmake -S . -B cmake-build-release-modules -DCNA_LINKER=LLD
python3 tools/build/benchmark_final_link.py \
  --build-dir cmake-build-release-modules --target cna_tool_cnb_info \
  --artifact cna_tool_cnb_info --label release-lld --iterations 7 \
  --output cmake-build-release-modules/release-lld-link.json
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

Both fast linkers exceeded the campaign's 20% acceptance threshold. LLD won that large Debug
test link and used less memory than Mold; Mold won the small Release link. `AUTO` remains
probe-based and prefers Mold; measure the relevant current target before deriving a local choice
from these old samples.
GNU ld remains a valid dependency-free fallback. All three `CnaTests` binaries ran the same 7,427
tests and produced an identical normalized result set: 6,868 passed, 490 skipped, and the same 69
baseline STUB/sandbox failures. Every Release tool emitted the expected usage output.

The accompanying historical minimal probe verified three policy edges: switching from Mold to
LLD without a stale cached path, refusing an unavailable explicit linker with a useful diagnostic,
and leaving cross-build `AUTO` without a linker option.

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

Build with at most 8 jobs: the development machine's 16 cores and 30 GiB are shared by several
sessions (the parent `CLAUDE.md`, Rule 5). Lower the count for one target that swaps rather than
raising it elsewhere. `CNA_MAX_VENDORED_BUILD_JOBS` controls the nested configure-time SDL build
separately; it is not the main Ninja parallelism limit.

### CI trend report and structural guard

`tools/build/check_build_performance_policy.py` resolves preset inheritance and fails
when the named focused presets lose their required cache values (for tests, examples, the C API
and selected optional subsystems). This is a preset/source-text guard, not a resolved CMake graph,
binary dependency or timing test. It also verifies that PCH, unity, and IPO remain opt-in
and rejects routine project policy expressed through global `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS`,
linker flags, `add_compile_options()`, or `add_link_options()`. An exception is one exact line in
one named file, listed with its reason in the checker, and one whose line has gone fails as stale.
The exact-line exceptions cover standalone installed-C-API consumer instrumentation, whole-module
Emscripten `-pthread`, runtime-linkage flag reads, and deliberate runtime-choice fixture settings.
Two whole-file exceptions are script-only SDL toolchain/fingerprint data readers. Toolchain and
lowercase `tests` directories are excluded from that source scan. The checker rejects stale named
exceptions, but does not parse all CMake semantics or arbitrary external project code.

The general Linux workflow records the actual configure and build commands with `/usr/bin/time`,
keeps the Ninja build log, and runs `tools/build/report_build_performance.py` after the build. Its
uploaded `build-performance-report.json` contains:

- supplied configure/build wall time and GNU-time maximum RSS (the clean-build driver instead
  samples an aggregate process tree; these are different estimators, not directly interchangeable);
- complete graph command and compile counts;
- compile/link edges recognized from the supplied Ninja console progress descriptions; these are
  not a parse of `.ninja_log` and depend on its standard English output format;
- selected artifact sizes; and
- the active cache's current direct/preprocessed hits, misses, selected uncacheable reasons,
  cleanups and size. These are cumulative counters, not automatically computed phase deltas.
  CI zeroes them before its build; on a shared developer cache, record before/after without resetting
  other sessions' statistics.

Schema 2 also records the complete CMake cache (including types), its SHA-256, the source directory
from that cache, and report-time provenance. This includes source/dependency Git revisions, dirty
state, tracked-diff and untracked-file hashes, recorded submodule pins, source-export tree hashes,
cache-resolved library/tool/toolchain file hashes, selected build environment variables and CPU
affinity. The source revision belongs to the measured build, even if the report script lives in
another checkout. `unversioned_source_dirs` identifies exports or missing checkouts; their tree
hashes identify present bytes without claiming that they match the recorded Git pin.

These values are retained as a 30-day CI artifact for trend inspection. Shared-runner timing has no
hard pass/fail threshold; only machine-independent preset and policy invariants are enforced.

### Reproducible clean-build matrix

`tools/build/benchmark_clean_build.py` runs one clean benchmark in a new directory and writes the
full evidence to `benchmark-result.json`. Every run is a complete, uncached build, which is exactly
the kind of rebuild the shared machine's rules exist to avoid, so the script refuses to start
without `--owner-approved`. It also refuses more than 8 jobs and any directory under `/tmp`; an
automatic directory goes into the repository's `build-probe/`. An explicit directory must not exist
and its basename must start with `cmake-build-benchmark-`. The script never deletes a build
directory: copy the result out and delete the directory yourself, because a `legacy` tree is
20–30 GiB.

`--ccache off` (the default) measures a cold compile without any cache. `--ccache isolated` gives
the build a private cache, so an empty directory is a verifiable cold-cache start with ccache
counters, but a second cache is what the one-shared-cache rule forbids; use it only when the owner
asks for those counters. Linux peak RSS is the sampled sum of the complete process tree, not only
the outer CMake/Ninja process. Schema 2 introduced load average before and after each measured
phase; schema 3 additionally records the effective configuration and provenance described above.

The driver currently selects these broad profiles. Command counts in this table belong to the
2026-08-29 record, not today's graph:

| Profile | Target | Configuration and enabled closure | Commands / compilations |
| --- | --- | --- | ---: |
| `dev` | `cna_tool_cnb_info` | Debug/STUB; focused tool only | 508 / 483 |
| `unit` | `CnaTests` | Debug/STUB; complete unit-test executable | 1,164 / 1,126 |
| `legacy` | `all` | Debug/OPENGLES3; tests, examples, C API, CNAEXT, compiled effects, networking, video | 2,483 / 1,929 |

All profiles disable PCH, unity, IPO, Draco, devices, diagnostics, the optional inspector and
microbenchmarks, and the configure-audit cache. They select one renderer explicitly. The `dev` and `unit`
profiles also disable examples, the C API, CNAEXT, compiled effects, networking, and video. The
`legacy` profile approximates the former broad `cmake-build-debug` feature selection and can reuse
an explicitly supplied FNA3D checkout for MojoShader. GamerServices follows the networking profile.
`--cna-library static|shared` defaults to `static` and explicitly sets `CNA_SHARED_LIBRARY`,
independently of `BUILD_SHARED_LIBS=OFF`. `--platform SDL3|HEADLESS|TERMINAL` and
`--audio-platform SDL3|NULL|ALSA` both default to `SDL3`; CMake still rejects unsupported
combinations. A different effective value for any of these three selections refuses the build
after configuration. The `configuration` object describes requests; `cmake_cache` contains every
effective cache entry, including future options not known to the driver.

The driver captures provenance after configuration and after the build/no-op phases, outside
their measured intervals. If the snapshots differ, it retains both in the JSON, sets
`inputs_stable_between_snapshots=false` and exits unsuccessfully. Equal snapshots do not prove
that no transient changes occurred between them. These identities allow comparison of new runs;
they do not pin the complete external environment or reconstruct missing historical inputs.
The 2026-08-29 reference used GCC
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
no-op invoked no compiler. The final campaign graph had one additional non-compile asset-copy
edge compared with the earliest GCC points, after the benchmark exposed and fixed a parallel copy
race; all six complete measurements contain the same 1,929 compiler calls.

At 12 jobs the complete build took 643.75 s (10 min 44 s) with GCC and 605.18 s (10 min 05 s) with
Clang. Clang was 6.0% faster, peaked at 4.65 GiB versus GCC's 4.73 GiB, and produced a 20.80-GiB
tree versus GCC's 29.54-GiB tree (29.6% smaller). Clang's 12→16 gain was only 3.8% while RSS rose
58.8%; GCC gained 6.0% while RSS rose 31.7%. On that day twelve jobs was the balanced point and 16
the fastest; Clang `-j16` was the fastest complete point at 582.34 s (9 min 42 s). The machine has
since become shared, and 8 jobs is now the limit, so the 12- and 16-job rows are a historical
record, not settings to use. GCC was fastest for the focused
`dev` and `unit` closures, while Clang used less disk and usually less memory below saturation.
Keep both compilers in CI rather than declaring one universal winner.

With the owner's approval for a clean benchmark, measure a current point at the job limit. Each run creates its own
`build-probe/cmake-build-benchmark-*` directory:

```sh
for profile in dev unit; do
  python3 tools/build/benchmark_clean_build.py --owner-approved \
    --label "comp001-${profile}-gcc-j8" --profile "${profile}" \
    --cxx-compiler /usr/bin/g++ --c-compiler /usr/bin/gcc \
    --parallel 8 --linker MOLD
  python3 tools/build/benchmark_clean_build.py --owner-approved \
    --label "comp001-${profile}-clang-j8" --profile "${profile}" \
    --cxx-compiler /usr/bin/clang++ --c-compiler /usr/bin/clang \
    --parallel 8 --linker MOLD
done

python3 tools/build/benchmark_clean_build.py --owner-approved \
  --label comp001-legacy-gcc-j8 --profile legacy \
  --cxx-compiler /usr/bin/g++ --c-compiler /usr/bin/gcc \
  --fna3d-source /path/to/FNA3D --parallel 8 --linker MOLD
```

The 2026-08-29 rows above used `--ccache isolated`; a new `--ccache off` run reports no cache
counters and performs uncached compilation for its actual current graph. An empty cache does not
make filesystem/OS page caches cold, and a 20 ms sum-of-RSS sampler can miss short peaks and count
shared resident pages more than once. The driver does not run the built test suites, archive dirty
source bytes/packages, or capture every transitive system header, environment variable and
external tool input. Keep the required source/package archives and test results separately.
The historical August matrix remains historical; the corrected defaults and current graph are
not an exact replay of those measurements.

## Deferred techniques

Precompiled headers, unity builds, PGO, source partitioning, `-march=native`, `-Ofast`, and
`-ffast-math` are intentionally not global defaults. They require a measured, target-specific case:
they can make clean builds faster while harming incremental rebuilds, portability, binary packaging,
or XNA numerical behaviour.

A generic PGO campaign waits for a deterministic workload that represents real client applications.
A C++ modules migration needs a separate design covering toolchains, dependency scanning, package
consumption and cross-compiles. Any measured experiment becomes a
default only after it meets its acceptance threshold on both native GCC and Clang builds.

## Current verification

The 2026-11-01 documentation audit checked current preset inheritance, test-group mappings,
compiler/debug/instrumentation/linker/IPO policy, configure-cache inputs, shared C/C++ runtime
composition, CI wiring and measurement helpers. The build-performance policy checker passes.
The existing focused `cna_tool_cnb_info` target builds, and the report helper emits valid current
graph/artifact JSON with no invented timing inputs. The related runtime-first/shared-runtime/export
configuration tests and six footprint gates also pass in this pass (9/9; see
[link-size verification](link-size-architecture.md#current-verification)). No full clean rebuild,
cache reset, PCH/unity/IPO/sanitizer/compiler matrix or fresh timing campaign was needed or run;
the historical tables above do not become current benchmark results through this source audit.

The 2026-11-02 `CNA-FIX-L12` regressions exercise real, compilation-free CMake/Ninja fixture graphs.
The original scripts fail both the static-default and source-revision controls. All eight corrected
test groups pass, covering static/shared selection, platforms, unanticipated cache entries,
dependency modifications, nested pins, source exports and changed-input/mismatched-selection
refusal. The existing focused tool builds, and its schema-2 report records its actual shared CNA,
SDL3 platform and dependency identities. The registered tool test and provenance/pipeline/C ABI
gates pass. No full clean CNA benchmark, cache reset or historical timing replay was run.
