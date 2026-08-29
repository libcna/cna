# CNA compilation performance and build-flag system review

> **Historical baseline:** this audit describes the tree before commit `b3db5701b`
> (`build: optimize compilation profiles and flag policy`). The implemented foundation and the
> remaining measured optimization work is tracked in
> [`../plan/plan_compilation.md`](../plan/plan_compilation.md).

Date: 2026-08-28
Scope: local build-system audit only. No CNA source, CMake, preset, or CI configuration was changed.

## Executive summary

CNA already has several sound foundations: C++23 is stated centrally, Ninja is used by the principal test presets, `ccache` is enabled before Sharp Runtime is added, the Sharp Runtime component closure is deliberately narrowed, renderer selection avoids building every renderer, and vendored SDL builds are persistently reused and separately parallelism-limited.

The greatest current opportunity is not a speculative compiler switch. It is to make the configuration intent explicit and to remove accidental build volume from everyday workflows. The examined debug configuration contains 1,827 C++ and 110 C object edges, 4,672 Ninja targets, and a 29 GiB build tree. It has the C API, tests, examples, networking, and FFmpeg enabled. This is a valid integration configuration but an expensive default for a focused edit.

The immediate, low-risk sequence is:

1. Restore ccache effectiveness and make it observable.
2. Add focused, composable developer/CI/release presets; do not change existing preset behaviour yet.
3. Separate public ABI/configuration requirements from private compiler policy, and centralize policy in target-scoped interface libraries.
4. Add opt-in linker and IPO/LTO controls, with safe exclusions for sanitizers, cross-compiles, vendored dependencies, and installed static libraries.
5. Measure before pursuing PCH, unity builds, source splitting, PGO, or CPU-specific tuning.

This ordering should improve normal iteration immediately while keeping XNA/FNA conformance, cross-platform support, consumer compatibility, and diagnostic quality intact.

## Observed baseline

The following observations are from the local checkout and its existing build directories; they are evidence for prioritization, not a portable benchmark.

| Observation | Result | Consequence |
| --- | ---: | --- |
| Host resources | 16 logical CPUs, 30 GiB RAM | Parallelism should be set intentionally after measuring memory, rather than left unlimited. |
| C++/C source inventory under `modules` and `tools` | 2,810 / 85 | Header parsing and many final executables dominate clean builds. |
| `cmake-build-debug` object edges | 1,827 C++ / 110 C | A full integration build is materially larger than a one-module edit. |
| `cmake-build-vulkan` C++ object edges | 1,646 | Renderer/dependency choices noticeably affect build volume. |
| `cmake-build-debug` Ninja targets | 4,672 | `all` is the wrong routine target for most work. |
| `cmake-build-debug` disk use | 29 GiB | Debug information and many test/example executables make storage and link time significant. |
| Largest debug outputs | C API archives about 430 MiB; `CnaTests` about 387 MiB; many demos about 70--75 MiB each | Avoid generating all demos and installed-consumer copies during an unrelated edit. |
| Largest source files | C API engine layer 829 KiB; Vulkan renderer 746 KiB; WebGPU renderer 607 KiB; EasyGL renderer 553 KiB | These are the first candidates for include profiling and carefully scoped source partitioning. |
| ccache state | 20.0/20.0 GiB (99.92%), 24.33% hit rate | Eviction is likely suppressing the intended reuse between configurations and branches. |
| Compile-command database | absent from the inspected builds | Dependency/include-time profiling is unnecessarily difficult. |

The checked debug cache has `CNA_BUILD_C_API=ON`, `CNA_BUILD_TESTS=ON`, `CNA_BUILD_EXAMPLES=ON`, `CNA_ENABLE_NET=ON`, and `CNA_ENABLE_VIDEO=ON`. Its cost must therefore not be attributed to the framework core alone.

## Current strengths to preserve

- The root project consistently requests C++23, requires it, and disables compiler extensions (`CMakeLists.txt`, lines 21--23).
- `CNA_USE_CCACHE` is enabled before Sharp Runtime is brought into the build, so cache coverage intentionally includes that sibling dependency (`CMakeLists.txt`, lines 42--60).
- Sharp Runtime tests are forced off and CNA selects a finite component closure instead of `All` (`CMakeLists.txt`, lines 132--140; `cmake/SharpRuntimeConsumption.cmake`). This is both a correctness and build-time win.
- Renderer selection is configuration driven; a single-renderer build does not need every renderer implementation.
- The modular structure makes the module targets natural units for target-scoped policy.
- SDL sub-builds are reused outside normal build directories and `CNA_MAX_VENDORED_BUILD_JOBS` prevents configure-time dependency builds from bypassing a parent `-j` limit (`cmake/ThirdPartySDL.cmake`, lines 5--20 and 440--444).
- The C API already has a target-local strict-warning helper and does not globally impose `-Werror` on unrelated code (`modules/c-api/CMakeLists.txt`, lines 5--11).

## Problems and risks in the current flag model

### 1. The one `cna_build_flags` target mixes different responsibilities

`CNA::BuildFlags` is public and transitive for every module. It correctly carries build facts that consumers must see, such as the renderer identity and feature macros. However, it also carries Emscripten compile/link options (`modules/CMakeLists.txt`, lines 227--252). This makes the name misleading and creates pressure to place all future flags in one public target.

Use three separate concepts instead:

| Target category | Visibility | Contents |
| --- | --- | --- |
| `CNA::BuildConfig` | `INTERFACE`, public | Only macros/options required to compile a consumer correctly against CNA's public headers or ABI. Emscripten ABI flags belong here only if they truly must be shared by every consumer. |
| `CNA::ProjectOptions` | `INTERFACE`, private to CNA-owned targets | Warnings, UTF-8/source encoding policy, non-ABI compile hygiene, and internally selected optimization/debug options. Never exported by installed packages. |
| `CNA::Instrumentation` | `INTERFACE`, private to explicitly selected targets | Sanitizer, coverage, profiling, and fuzzing compile **and** link options. It must be mutually compatible with the selected profile. |

This is a semantic split, not an argument for adding a large number of flags. Keep the public carrier minimal because every public option becomes a consumer compatibility commitment.

### 2. Global directory flags make ownership harder to prove

Emscripten exceptions and sanitizers are currently added with root-level `add_compile_options()`/`add_link_options()` (`CMakeLists.txt`, lines 71--74 and 121--130). The sanitizer placement intentionally instruments CNA, Sharp Runtime, and tests while leaving earlier SDL/ENet sub-builds alone. That scope is defensible, but it is implicit in directory ordering rather than visible in target relationships.

Avoid broad, later-added global flags for new policy. Model the intended target closure explicitly. The sanitizer solution needs a small coordination seam with Sharp Runtime because instrumentation must cover code that crosses allocation/exception boundaries. Do not move it blindly to `CNA::BuildConfig`: that would export diagnostics-only options to package consumers.

Also remove raw `CMAKE_CXX_FLAGS` mutation around the vendored Draco subdirectory when feasible (`modules/CMakeLists.txt`, lines 192--198). A compiler-specific header injection should be confined to the Draco target/subproject configuration or replaced by an upstream-compatible patch. Raw flag variables are stringly typed, configuration-insensitive, and easily leak into nested projects.

### 3. Sanitizer presets duplicate and bypass the project mechanism

The three device sanitizer presets directly set `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS` (`CMakePresets.json`, lines 25--64), while the root already exposes `CNA_SANITIZE`. The duplicate mechanisms can drift, do not cover C flags uniformly when the C API is enabled, and make a combined sanitizer configuration less obvious.

Make presets set `CNA_SANITIZE=address`, `thread`, or `undefined` and give the instrumentation target ownership of compiler-specific compile/link flags. Keep any intentional per-sanitizer optimization level as a documented profile decision. Add configure-time validation:

- reject sanitizers for unsupported compilers/platforms;
- reject ThreadSanitizer combined with AddressSanitizer;
- reject or explicitly document incompatibility with IPO/LTO, PGO-use, coverage, and Emscripten;
- apply the sanitizer link option to every final executable/shared library that contains instrumented objects;
- retain narrowly justified exclusions such as Skia's `-fno-sanitize=vptr` as target-local exceptions.

### 4. Presets describe product configurations, not a reusable build policy matrix

The current presets are useful named builds, but most duplicate compiler/cache choices and only `tests`, `multi-renderer`, and `cnaext` explicitly select Ninja. The inspected OpenGL and web build directories use Unix Makefiles. That loses Ninja's incremental UX and makes performance comparisons noisier.

Use hidden base presets and inheritance. Preserve the existing public names initially, but rebase them on a small orthogonal matrix:

| Preset layer | Responsibility |
| --- | --- |
| `base-ninja` | Ninja, export compile commands, ccache launcher if available, common path/environment settings. |
| `dev` | `Debug`, one low-dependency renderer such as `STUB` or `HEADLESS`, tests/examples/C API off, optional net/video/Draco off. |
| `unit` | `Debug`, target test renderer, tests on, examples off by default. |
| `integration` | Current test breadth: examples and the needed optional backends on. |
| `release` | `Release` or `RelWithDebInfo`, tests/examples off, package-oriented choices explicit. |
| `asan`, `ubsan`, `tsan`, `coverage`, `profile-generate`, `profile-use` | inherit a base and change only instrumentation. |
| platform/renderer leaves | choose toolchain, platform, and renderer; avoid restating policy flags. |

The change should be additive first. Do not silently flip `CNA_BUILD_TESTS` or `CNA_BUILD_EXAMPLES` defaults: existing local workflows and CI may rely on the historical default. Once all CI configure invocations explicitly select their scope, reconsider changing the top-level defaults to OFF for conventional consumer builds.

### 5. There is no first-class release link-time optimization policy

No CNA-wide IPO/LTO option was found. Add an opt-in `CNA_ENABLE_IPO` only after adding `CheckIPOSupported` validation. It should be:

- disabled for Debug, sanitizers, coverage, PGO generation, Emscripten unless specifically verified, and unsupported compiler/toolchain combinations;
- initially enabled only for CNA-owned final release executables or shared libraries, not blindly for exported static module archives;
- benchmarked for link time, executable size, startup time, and representative XNA workloads;
- clearly separate from `-O3`: IPO improves whole-program optimization but increases link cost and couples artifacts to the linker/compiler toolchain.

For installed static libraries, LTO objects can force downstream users to use a compatible linker/plugin. Defaulting them to IPO is therefore a packaging risk, not a free performance win.

### 6. Linker choice is uncontrolled

The host has GCC 14 and Clang 19 but neither `mold` nor `ld.lld` is currently installed. A faster linker can materially shorten the repeated final links of the test binary and many demos, but it must be optional and detected.

Expose `CNA_LINKER=AUTO|DEFAULT|LLD|MOLD` (or an equivalent toolchain-level choice). In `AUTO`, retain the platform default unless a known supported linker is found and a configuration probe succeeds. Apply `-fuse-ld=...` only where the compiler accepts it. Do not hard-code ELF flags into Apple, MSVC, MinGW, Android, or Emscripten builds.

### 7. ccache is enabled but currently capacity-bound

The local cache reports 99.92% occupancy and a 24.33% hit rate. Before introducing PCH or unity builds, fix this because it is the fastest reversible improvement.

Recommended local policy:

1. Increase the size only after confirming available persistent disk space; otherwise allocate a dedicated cache volume. A cache that constantly evicts large C++ objects cannot serve branch/configuration reuse.
2. Set `CCACHE_BASEDIR` to a stable common parent of CNA and Sharp Runtime for developer and CI shells. Inspect the resulting statistics rather than assuming a gain.
3. Keep direct mode enabled. Consider a carefully tested `CCACHE_NOHASHDIR=true` only together with reproducible debug path mapping and after checking `__FILE__` behaviour; do not enable it by guesswork.
4. Add `ccache -s` to CI summaries and cache the ccache directory where the CI provider supports it. Key it by OS, compiler/version, relevant CMake/toolchain files, dependency revisions, and configuration-affecting options; use a conservative restore prefix for partial reuse.
5. Add `CMAKE_EXPORT_COMPILE_COMMANDS=ON` to Ninja developer/analysis presets. It does not accelerate compilation, but enables reliable `clang-scan-deps`, `clang-tidy`, IWYU, and `-ftime-trace` investigations.

Do not set a developer's global ccache policy from CMake. Preset environments or documented wrapper scripts are clearer and avoid mutating unrelated projects.

## Recommended implementation plan

### Phase A — high confidence, no behavioural break

1. Add hidden `base-ninja`, `dev`, `unit`, and `release` configure presets plus corresponding build presets. Existing named presets should initially inherit them unchanged.
2. Add `CMAKE_EXPORT_COMPILE_COMMANDS=ON` to the developer/CI bases.
3. Document focused invocations, for example `cmake --build --preset unit --target CnaTests --parallel <measured-value>`, instead of routine `all` builds.
4. Capture baseline metrics in a small script or CI step: configure duration, clean target build duration, one-file incremental duration, peak RSS, link duration, output sizes, ccache hits/misses, and build-tree size.
5. Configure sufficient ccache storage and add the statistics to the build report.

Acceptance criterion: no existing preset changes its effective compiler command except for the explicitly adopted generator/exported compilation database; focused builds are documented and measurable.

### Phase B — flag-system normalization

1. Create the three target-scoped carriers described above (`BuildConfig`, `ProjectOptions`, `Instrumentation`).
2. Move only public, ABI-relevant requirements to `BuildConfig` and document each one. Keep target include paths where they are.
3. Convert sanitizer presets from raw `CMAKE_*_FLAGS` to `CNA_SANITIZE` and central instrumentation logic. Test a C API-enabled sanitizer build too, because it introduces C objects.
4. Centralize CNA-owned warning policy in `ProjectOptions`, preserving toolchain branches and target-local exceptions. Start at a warning level that is green across supported compilers; do not force third-party targets or consumers through CNA's `-Werror` policy.
5. Replace direct Draco `CMAKE_CXX_FLAGS` mutation with the smallest target-scoped solution supported by its source tree. Verify both GCC and Clang configurations.

Acceptance criterion: no normal CNA preset writes `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, or global linker flags for routine policy; sanitizers compile and link the intended CNA/Sharp Runtime/test closure.

### Phase C — measured performance features

1. Add a detected optional linker setting and benchmark DEFAULT versus LLD/Mold where available.
2. Add opt-in IPO/LTO for selected final release artifacts. Publish measured size/runtime/link-time deltas before making any default decision.
3. Produce compiler time traces for the largest translation units and use include analysis to remove unnecessary transitive headers or move heavy implementation-only includes into `.cpp` files.
4. Pilot `target_precompile_headers()` on one stable, high-fan-out CNA-owned target only. Compare clean builds, one-header incremental rebuilds, peak memory, ccache hit rate, and all supported compilers/platforms. Do not add a project-wide PCH first.
5. Split only measured hot translation units at cohesive subsystem boundaries. The C API engine layer and the largest renderer files are candidates, but a split must preserve behaviour and not create gratuitous internal headers.

Acceptance criterion: a feature stays only if it improves the target metric on at least the supported GCC and Clang desktop builds without degrading incremental time or consumer build compatibility beyond an agreed threshold.

### Phase D — optional specialized optimization

1. PGO: use separate generate/use build directories and a deterministic, representative workload. Generic framework PGO is risky because a demo's hot path may not match client applications.
2. CPU tuning: never use `-march=native` for redistributable CNA artifacts. Permit an explicitly named local profile or well-defined package baseline only after recording the minimum ISA and testing fallback distribution builds. TinyGL's upstream host-specific choice should remain isolated.
3. Unity builds: keep them off globally. They can improve clean builds but damage incremental rebuilds, use more RAM, surface ODR/include-order bugs, and interact poorly with the deliberately broad cross-platform/renderer matrix. Pilot only on a proven leaf target.
4. C++ modules: do not make them a compilation-speed project until toolchain support, dependency scanning, consumer packaging, and all cross-compiles have a separately approved design.

## Proposed flag policy

The table distinguishes goals that should not be conflated.

| Goal | Recommended mechanism | Default | Notes |
| --- | --- | --- |
| Language level | `target_compile_features(... cxx_std_23)` plus central C++23 requirement | on | The current global requirement is good; target features make library requirements visible to consumers. |
| ABI/configuration macros | `CNA::BuildConfig` public interface | on when applicable | Renderer/feature macros only when a public header needs them. |
| Warnings | `CNA::ProjectOptions` private interface | on for CNA-owned code | Toolchain-specific; no global `-Werror`; preserve C API's stricter boundary. |
| Debugging | CMake Debug/RelWithDebInfo configuration | profile-selected | Prefer standard CMake configuration flags to hand-written global strings. |
| Sanitizers | `CNA::Instrumentation` | off | Compile and link options must travel together over the tested target closure. |
| Coverage | separate instrumentation target/preset | off | Exclude optimized release/IPO and unneeded third-party code. |
| Linker | detected toolchain option | default/AUTO | LLD/Mold only after a probe and per-platform gate. |
| IPO/LTO | target property after `check_ipo_supported()` | off initially | Final release artifacts only; never silently impose LTO on static-library consumers. |
| PGO | distinct generate/use presets | off | Requires representative workload and separate artifact directories. |
| CPU ISA | explicit distribution/local profile | portable baseline | Do not use host-native tuning by default. |

## What not to do

- Do not add a global `-march=native`, `-Ofast`, `-ffast-math`, or `-flto`. They can alter floating-point semantics, break distribution compatibility, or make debugging/linking substantially worse. XNA numerical behaviour is more important than a synthetic benchmark win.
- Do not make `-Werror` global. CNA integrates sibling and vendored code and supports multiple compiler families; warnings must be owned and tested at a target boundary.
- Do not turn on unity builds or PCH project-wide to mask header cost. Measure a target-specific pilot first.
- Do not select an ELF linker on non-ELF platforms or set raw flag variables in every preset.
- Do not compare build speed with different renderer, optional dependency, test, example, C API, or cache states and call it a compiler-flag result.
- Do not use a single build tree for Debug, Release, sanitizers, coverage, PGO, and multiple renderers. The existing separate directories are correct; keep artifact classes isolated.

## Measurement protocol

Use a dedicated, disposable benchmark build directory and preserve cache state in the result. For each candidate, collect at least three runs where appropriate:

1. cold configure (SDL cache state reported separately);
2. cold target build with a stated `--parallel N`;
3. warm no-op build;
4. one `.cpp` touch in a representative module;
5. one public-header touch with its fan-out recorded;
6. final-link-only rebuild;
7. relevant unit/integration tests.

Record wall time, peak resident memory, CPU utilization, ccache statistics before/after, number of executed Ninja edges, executable/library sizes, compiler/linker versions, generator, renderer, feature switches, and the exact compiler commands from `compile_commands.json`.

For this host, start experiments at `--parallel 8`, `12`, and `16`, measuring peak RSS. Keep a lower value when concurrent browser/IDE/renderer work or configure-time SDL builds would make the machine swap. `CNA_MAX_VENDORED_BUILD_JOBS` should remain independently bounded; it controls a nested build at configure time and is not the main Ninja job limit.

## Suggested first milestone

Implement only Phase A and the sanitizer-preset consolidation from Phase B in one reviewable change set, then benchmark it. This yields a clear developer fast path, restores cache visibility, and removes the most obvious duplicate flag route without committing CNA to LTO, a linker, PCH, unity builds, PGO, or non-portable CPU flags.

## Evidence locations

- Root language/cache/sanitizer configuration: `CMakeLists.txt`, lines 21--23, 42--60, 71--74, 121--140.
- Shared public build flag target and module helper: `modules/CMakeLists.txt`, lines 222--272.
- Vendored Draco raw flag mutation: `modules/CMakeLists.txt`, lines 185--200.
- Existing sanitizer presets: `CMakePresets.json`, lines 25--64.
- Existing C API warning policy: `modules/c-api/CMakeLists.txt`, lines 5--11 and 280--294.
- Vendored SDL reuse and nested parallelism: `cmake/ThirdPartySDL.cmake`, lines 5--20 and 374--458.
- Sharp Runtime's C++23 and target-local warning policy: sibling `../sharp-runtime/CMakeLists.txt`, lines 21--23; `../sharp-runtime/cmake/SharpRuntimeCommon.cmake`, lines 16--42.
