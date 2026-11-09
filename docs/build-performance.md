# Build performance and compiler policy

## Purpose

CNA has a large, modular C++ build: renderer selection, optional subsystems, the C API, tests, demo
applications, and the sibling Sharp Runtime can make a full integration configuration substantially
larger than a focused edit needs to be. This document defines the supported fast paths without
changing the coverage expected from CI and release configurations.

## Options at a glance

| Area | Default and switch | Reason for the default |
| --- | --- | --- |
| Focused test loop | Build a module-focused `Cna*Tests` target; `CnaTests` for the complete suite. | A one-file math test edit rebuilt and relinked in about a third of the time through `CnaMathTests`. |
| Configure audits | `CNA_CONFIGURE_AUDIT_CACHE=ON`; `OFF` forces every audit. | An unchanged reconfigure is about 80% faster. |
| Content-test PCH | `CNA_ENABLE_PCH=OFF`; `unit-pch` is the pilot. | 23.5% faster clean content-test compilation, limited to that object group. |
| Fast debug | `CNA_DEBUG_INFO=FULL`; `dev-fast-debug` opts into line tables. | Line tables lose local-variable and type data. |
| Clean-CI unity | `CNA_ENABLE_UNITY_BUILD=OFF`; `unit-unity` is leaf-only. | Faster clean four-job builds, slower one-source rebuilds. |
| Final linking | `CNA_LINKER=AUTO`; `DEFAULT` keeps the toolchain linker, `LLD`/`MOLD` request one explicitly. | LLD and Mold link a large Debug test executable about ten times faster than GNU ld. |
| Shared runtime | `CNA_SHARED_LIBRARY=ON` on native ELF GNU/Clang with CMake ≥ 3.30; `OFF` restores the static link. | A test executable is 0.5–0.9 MB instead of ~104 MB, and a rebuilt `libcna.so` relinks no umbrella consumer. |
| ccache | `CNA_USE_CCACHE=ON` when `ccache` is available; `OFF` disables it. | CNA never changes global sloppiness or direct-mode policy. |

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

Focused targets are `EXCLUDE_FROM_ALL`, so they add no work to a full build, with one exception:
`CnaInputModuleTests` is registered as the `CnaInputTests` CTest and is therefore part of `all`
(INP-AUD); it reuses the objects `CnaTests` compiles, adding one link. The split does not change
the compile commands or the `--gtest_list_tests` inventory of `CnaTests`, and its graph contains no
duplicate commands.

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

Adding or removing a module source produces Ninja's `GLOB mismatch`, regenerates CMake and
refreshes the audits. A forbidden reference (for example a new `SDL_CreateWindow` call) invalidates
the cache and still fails the strict ratchet gate, so caching does not weaken the check.

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

The pilot measurement (GCC, Debug/STUB, ccache off) cut clean content-test compilation by 23.5%,
above the pilot's 15% threshold, with about 3% more peak memory. Clang also compiles and links the
PCH target. The gain is confined to one object group, so the option stays opt-in.

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

Trace times are inclusive and can overlap: use them to rank translation units and headers, not to
add them together. The ranking identified `ContentReader.hpp` as the costliest widely parsed project
header. It therefore forward-declares the math types it returns instead of including seven complete
math headers, and `ContentReader.cpp` and the tests that construct those types include their real
dependencies explicitly. Keep it that way: re-adding those includes widens the rebuild fan-out of
every translation unit that includes `ContentReader.hpp`.

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

In the reference measurement line tables cut a controlled content rebuild by about 10%, peak memory
by about 17% and the build tree by about 47%. Split DWARF gave no meaningful compile gain and made
the tree larger, so it has no preset; both modes remain selectable.

## Shared runtime library

`CNA_SHARED_LIBRARY` (Vulkan parity VKPAR-0017) links the CNA runtime into one shared
library, `libcna.so`, instead of statically into every executable that links `CNA`.

**Why.** A static configuration repeats the runtime and its debug information in every test and
example executable: about 104 MB each in a Debug integration tree, mostly DWARF and symbol tables,
and an edit to one renderer `.cpp` relinked roughly 800 of them (~83 GB written). With `libcna.so`
the engine and its debug information exist once; a typical test executable is 0.5–0.9 MB.

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

Relink fan-out depends on the consumer graph: `CnaTests` links the `CNA` umbrella, most focused
groups, tools and probes use module composition, and `CnaRendererTests` uses the umbrella for
renderer-state identity. Consult the generated link lines rather than assuming a count. The
symbol-footprint probes deliberately remain static-only ([link boundaries](link-size-architecture.md)).

**One copy of everything.** A symbol defined both in `libcna.so` and in an executable would give it
two copies of that code and of its static state. Naming the `CNA` interface first alone does not
guarantee ordering through STATIC/OBJECT wrappers and optional sidecars such as GamerServices,
Net and ContentPipelineBuild. The shared target therefore injects its DSO into consumers' direct
link dependencies and excludes direct module archives already contained in that DSO. A scoped
`--no-as-needed` link feature retains this early DSO even when the application's first CNA
reference comes from a later static framework; the previous linker state is restored immediately.
Module-only consumers do not inherit this shared-target interface and remain static.
Shared consumers inherit `-fPIC`, keeping exported objects
in the DSO, and `-z nocopyreloc -z text`, refusing incompatible precompiled inputs instead of
embedding a copy or dynamic text relocation. `CMAKE_POSITION_INDEPENDENT_CODE=ON` alone is
insufficient: executable objects use PIE, which permits copy relocations on x86-64 GCC.
Current mixed-consumer and C API runtime-identity tests protect the relevant singleton contracts.

**Incremental ABI changes (MTH-BUILD-001 / CNA-FIX-L34).** The tracked-header dependency graph
remains responsible for compiler ABI assumptions. A changed class layout used inline, or an added
virtual in a base class, recompiles and relinks consumers that include that header. A consumer
virtual in a base class, recompiles and relinks consumers that include that header. A consumer
using only opaque declarations need not recompile. Without PIC, such a consumer could still reserve
an exported object's size through an ELF copy relocation; growing the object then produced the
loader's `Symbol ... has different size in shared object` warning and truncated its initialization.
PIC removes that link-time size dependency, so ordinary incremental builds handle the covered cases
without deleting executables. Applying this policy to an existing tree requires its

The early DSO injection forwards the target's absolute path as a quoted linker argument. A bare
`$<TARGET_FILE:cna_shared>` link-library item defeats `LINK_DEPENDS_NO_SHARED` and relinks every
shared consumer even for an implementation-only edit; the fixture therefore tests both the complete
helper and, for reproduction, the plain shared-library policy that exposes the stale-copy defect.
A bare `-lcna` breaks installed static C consumers: the archive generator expands the runtime file
into its archive but keeps the name as an external dependency. The ordinary target dependency still
brings the DSO up to date before consumers, and the ordinary target path supplies the archive
generator's runtime closure. Installed C++ consumers resolve the relocated imported target's path.

A global dynamic-symbol-size stamp was not selected: it would relink unrelated consumers and
cannot replace header tracking for same-size field or virtual-slot changes. Turning off the
optimization also relinks unrelated consumers on every implementation edit. The chosen rule has
no symbol-scanning step and no new production tool dependency. GNU ld documents the
[copy-relocation and text-relocation guards](https://sourceware.org/binutils/docs/ld/Options.html).

`CnaSharedRuntimeIncremental` runs a tiny fixture using the production CMake policy/helper. On
Linux x86-64 it checks exported-data growth, inline layout, derived virtual dispatch, opaque
consumers, mixed module/shared ordering, installed usage requirements, non-PIC refusal and three
no-op builds. The installed checks include the production C API static-archive generator and
package template, separate C++/C API component installs, relocated prefixes (including a space),
and pure-C shared and static consumers. The static target and executable must have no CNA DSO
dependency, and installed programs run with `LD_LIBRARY_PATH` unset. In the fixture an
implementation-only edit relinks no shared consumer (only the static control), a header edit
relinks exactly the affected shared consumer plus the static control, and a no-op build changes no
output. These are fixture-scale checks, not a performance qualification of a real CNA tree.

The fixture requires `ccache` (point `CCACHE_DIR`/`CCACHE_BASEDIR` at your usual cache) and does
not configure the full runtime; run it with an existing work directory:

```bash
cmake -DCNA_SOURCE_DIR="$PWD" -DCNA_WORK_DIR=<fixture-workdir> \
      -P cmake/Tests/SharedRuntimeIncrementalCase.cmake
```

`commands.json`, per-command logs and `measurements.json` retain exits, timings, relink sets and
changed output sizes. `CNA_FIXTURE_ARGS` accepts `--compiler;clang++`; `--policy-only` deliberately
isolates the unsafe old dependency policy, and fails on the x86-64 GCC copy-relocation witness.

**Failure triage.** `c9c53769a` removes stale exported-data copy relocations and the
forced implementation-only relink; `5f0255c51` fixes installed consumers by forwarding the DSO
by path. For `Symbol ... has different size in shared object`, reconfigure and rebuild the
consumer once to acquire `-fPIC` and `-z nocopyreloc -z text`; rebuild incompatible precompiled
inputs too. For `ld: cannot find -lcna` in an installed static C consumer, regenerate/reinstall
the current package and reconfigure the consumer against that prefix. Adding a global `-L`
workaround conceals the obsolete export. The fixture above checks relocated C/C++ installs;
it requires native ELF GNU/Clang, CMake 3.30+, Python 3 and Ninja or Make.

**Boundary.** This is not a C++ ABI compatibility promise for already-built external binaries,
untracked/incorrect headers, ABI-changing flags outside the build graph, or replacing libraries
without rebuilding their dependent sources. An ABI fingerprint could not make those compiler
assumptions safe by relinking alone. The whole CNA-tree rebuild/test and its real I/O cost remain
integration checks; the fixture above uses tiny CPU-only consumers.

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

On a clean four-job GCC build the pilot cut the core/math closure by about 26%, meeting its
threshold, but at 12 jobs the gain fell to about 19% and a one-source edit became slightly slower.
That is why unity stays opt-in and its preset targets four-job clean CI rather than replacing
`unit`. Third-party targets, other modules, cross-builds, Emscripten, installed consumers and
ordinary presets are unaffected.

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

Do not export `CCACHE_MAXSIZE`, `CCACHE_HASHDIR` or `CCACHE_NOHASHDIR` against a shared cache: the
environment overrides the cache's own configuration for everyone using it. A shared cache is most
effective when developer worktrees use the same `CNA_CCACHE_BASEDIR`.

Interpret direct/preprocessed hit counters as workload deltas, not as a lifetime percentage. A
build that can read the cache but cannot write ccache's statistics file under its
`temporary_dir` (for example a sandbox) gets real hits that never advance the counters; the debug
log then ends with `Error while finalizing stats`. That is an observation-environment problem, not
a launcher defect, so CNA does not override the user's direct-mode or sloppiness settings.

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

A public-header edit is the strongest direct-mode check: every invalidated compile edge should come
back as a direct hit without running the preprocessor or growing the cache.

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

Repeat with `DEFAULT` and `MOLD`; every variant must reuse identical object files. In the reference
measurement both fast linkers linked the large Debug `CnaTests` about ten times faster than GNU ld
(LLD slightly ahead and using less memory than Mold), Mold was fastest on the small Release tool,
and all three binaries produced identical test results. `AUTO` is therefore probe-based and
prefers Mold; GNU ld remains a valid dependency-free fallback. Measure the relevant current target
before deriving a local choice.

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

State the `--parallel` value with every measurement, and on a shared machine lower it for one target
that swaps rather than raising it elsewhere. `CNA_MAX_VENDORED_BUILD_JOBS` controls the nested
configure-time SDL build separately; it is not the main Ninja parallelism limit.

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

The driver selects these broad profiles:

| Profile | Target | Configuration and enabled closure |
| --- | --- | --- |
| `dev` | `cna_tool_cnb_info` | Debug/STUB; focused tool only |
| `unit` | `CnaTests` | Debug/STUB; complete unit-test executable |
| `legacy` | `all` | Debug/OPENGLES3; tests, examples, C API, CNAEXT, compiled effects, networking, video |

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

A complete `legacy` clean build takes about ten minutes at 12 jobs on a 16-CPU host with either GCC
or Clang. Clang produced a roughly 30% smaller tree and usually used less memory; GCC was faster for
the focused `dev` and `unit` closures. Keep both compilers in CI rather than declaring one
universal winner.

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

A `--ccache off` run reports no cache counters and performs uncached compilation for the actual
current graph. An empty cache does not make filesystem/OS page caches cold, and a 20 ms
sum-of-RSS sampler can miss short peaks and count shared resident pages more than once. The driver
does not run the built test suites, archive dirty source bytes/packages, or capture every transitive
system header, environment variable and external tool input. Keep the required source/package
archives and test results separately.

## Deferred techniques

Precompiled headers, unity builds, PGO, source partitioning, `-march=native`, `-Ofast`, and
`-ffast-math` are intentionally not global defaults. They require a measured, target-specific case:
they can make clean builds faster while harming incremental rebuilds, portability, binary packaging,
or XNA numerical behaviour.

A generic PGO campaign waits for a deterministic workload that represents real client applications.
A C++ modules migration needs a separate design covering toolchains, dependency scanning, package
consumption and cross-compiles. Any measured experiment becomes a
default only after it meets its acceptance threshold on both native GCC and Clang builds.

## Focused coverage collection

The [coverage collector recipe](../tools/coverage/README.md) owns setup,
commands and metric definitions. It requires a dedicated, already configured HEADLESS platform
and renderer tree, GCC 14 coverage instrumentation, matching gcov JSON format 2, Ninja,
Python 3 and GNU `timeout`. For a prepared snapshot and caller-owned artifact directory:

```sh
python3 tools/coverage/collect.py run --prepared "$coverage_artifacts/prepared.json" \
  --output-dir "$coverage_artifacts" --gcov gcov-14
```

`Missing instrumented build artifact`, `Mixed compiler versions in gcov inputs`, or a changed
snapshot/counter refuses publication, removing prior coverage reports at the output paths.
Use matching GCC/gcov, configure coverage flags for compilation and linking, then prepare again
and rerun with exclusive use of the tree. A failed/empty/all-skipped binary also refuses coverage;
inspect its log/XML and `run.json` when present. Do not hand-repair generated reports.
