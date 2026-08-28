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
| `unit` | Portable complete unit-test suite; builds `CnaTests` | Demos, C API, networking, FFmpeg, Draco |
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

## Compiler-policy layers

The CMake targets distinguish requirements imposed on a consumer from CNA's private build policy:

| Target | Visibility | Responsibility |
| --- | --- | --- |
| `CNA::BuildConfig` | Public | C++23 and preprocessor/build facts a public header or ABI requires, including the selected renderer and feature macros. |
| `CNA::ProjectOptions` | Private to CNA-owned targets | Non-ABI compiler policy, currently deterministic MSVC UTF-8 source decoding. |
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

## Deferred techniques

Precompiled headers, unity builds, PGO, source partitioning, `-march=native`, `-Ofast`, and
`-ffast-math` are intentionally not global defaults. They require a measured, target-specific case:
they can make clean builds faster while harming incremental rebuilds, portability, binary packaging,
or XNA numerical behaviour.
