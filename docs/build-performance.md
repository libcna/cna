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
| `unit` | Portable unit-test iteration; builds `CnaTests` | Demos, C API, networking, FFmpeg, Draco |
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

Do not use `dev` as merge evidence for a renderer-, C API-, network-, media-, or Draco-specific
change. Existing renderer/platform/integration presets remain the required evidence for the code
they include.

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
