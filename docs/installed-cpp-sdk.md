# Installed C++ SDK

`CNA_INSTALL_CXX=ON` adds an opt-in, relocatable CMake package for the configured
CNA modules. It does not change the default source-tree `add_subdirectory` workflow
or enable installation in embedded Sharp Runtime builds unless the CNA SDK is
explicitly requested. The SDK uses the actual renderer/platform/module selection
of its producer; installing does not enable unavailable modules.

The Sharp Runtime checkout must provide modular installation and the exported Headers target.
CNA checks that support in addition to its configured Sharp Runtime version requirement;
matching a product version alone is insufficient. Current product versions and dependency
requirements come from the root CMake configuration, not a historical SDK campaign pin.

## Build and install

Use the existing build directory for the desired configuration. On this shared
development machine use the shared ccache and at most eight workers (one is shown):

```sh
cmake -S . -B cmake-build-debug -DCNA_INSTALL_CXX=ON
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv \
  cmake --build cmake-build-debug --target cna_cpp_sdk --parallel 1
cmake --install cmake-build-debug --prefix /path/to/sdk --component SharpRuntime
cmake --install cmake-build-debug --prefix /path/to/sdk --component CNACpp
# Only if CNA_BUILD_C_API was enabled:
cmake --install cmake-build-debug --prefix /path/to/sdk --component CNACApi
```

`cna_cpp_sdk` builds the configured module archives, shared runtime if enabled,
their bundled dependencies, Sharp Runtime components and the C adapter if enabled.
It does not require building every example or test. For multi-configuration
generators, use the same `--config` for build and install. Install into a fresh
prefix when changing the module set; an install does not remove older components.

If `CNA_C_API_BUILD_STATIC=ON`, also build `cna_c_api_static` before installing the CNACApi
component. The SDK aggregate currently depends on the ordinary C adapter, not that combined static
archive target. Installing only CNACpp/SharpRuntime does not publish the C adapter component.

The **whole prefix** can then be moved. Headers have separate module include roots,
so linking `CNA::Math` does not accidentally expose Graphics or System.Xml headers.
Bundled compiled dependencies and their needed headers travel with the SDK.
System dependencies remain system dependencies: install the development packages
required by the producer's configuration (for example FFmpeg, FreeType, Zlib,
Curl, Opus, nlohmann-json, Zstd or shaderc). Their paths are discovered on the
consumer, not frozen to the build machine. The package discovers the dependencies
of the configured SDK; linking a narrow module still retains its narrow link graph.
ALSA is loaded dynamically by CNA and does not add a link-time libasound requirement.
On Linux, vendored SDL3_mixer installs with `$ORIGIN` as its runtime search path,
so loading it independently resolves the SDK's neighboring SDL3. This is part of
the SDL prebuilt manifest; adopting the recipe selects a separate manifest directory
without replacing the prebuilt libraries used by already-configured trees.

## Consume

```cmake
project(MyGame LANGUAGES CXX)
find_package(CNA CONFIG REQUIRED COMPONENTS
    Core Math Input GraphicsCore Content Runtime Audio)
add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE
    CNA::Core CNA::Math CNA::Input CNA::GraphicsCore
    CNA::Content CNA::Runtime CNA::Audio)
cna_deploy_runtime(MyGame)
```

Configure the consumer with `-DCMAKE_PREFIX_PATH=/path/to/moved-sdk`. No CNA source
directory, manually assembled include paths or `add_subdirectory` fallback is
needed. `SharpRuntime::` component targets come from the installed Sharp Runtime
package. `CNA::CNA` (and the existing `CNA` spelling) is the umbrella. Targets named
`Internal_*` or `Dependency*` are implementation details, not stable public modules.

`cna_deploy_runtime()` adds the post-build copies required by the configured package: selected
SDL3/SDL3_mixer DLLs, renderer-owned runtime payloads such as wgpu-native, and dynamic MinGW
runtime DLLs where applicable. It is harmless when the selected configuration has no runtime
payload, and repeated calls for the same executable are ignored. This is the supported deployment
route for both source-tree and installed-SDK consumers; do not duplicate renderer-specific copy
rules in applications.

With `CNA_SHARED_LIBRARY=ON`, use CMake 3.30 or newer. The exported umbrella/module
combination retains the source build's single-runtime link-order contract. A Math-only
consumer does not pull in libcna/SDL/WebGPU. The consumer's own static/shared C++
standard-library choice is honored, rather than copied from the producer.

Pure-C consumers can use the shared C API or the encapsulated single-file static C archive.
Mixed C/C++ programs use the shared C API and shared C++ SDK (`libcna_c_api.so` + `libcna.so`)
to share one runtime. Combining `CNA::CApiStatic` with C++ SDK targets (including Math) or the
shared-runtime C adapter is unsupported and refused during CMake configure/generate with a
`CNA_RUNTIME_FORM` conflict. Pkg-config/plain linker consumers must follow the same rule.

An explicit `find_package(CNA CONFIG REQUIRED COMPONENTS CApi)` works in both C and
C++ projects without loading the C++ SDK or finding Sharp Runtime. A later request
for C++ modules loads them normally and establishes the shared C/C++ runtime link
contract. A default component-free request loads the C++ SDK when C++ is enabled,
and only the C adapter in a C-only project. Missing required components fail;
unknown optional components remain not found. `QUIET` discovery of an unavailable
dependency reports `CNA_FOUND=FALSE` with `CNA_NOT_FOUND_MESSAGE`, not an unconditional
configuration error.

### Emscripten boundary

The source-build aliases `CNA::EmscriptenExceptionAbi`, `CNA::EmscriptenAsyncify` and their
`CNA::EmscriptenAbi` composition are distinct. Since CNA-FIX-L19 (2026-11-02) the installed SDK
exports all three imported targets with their link options (`-fexceptions`,
`-sDISABLE_EXCEPTION_CATCHING=0`; `-sASYNCIFY=1` with its stack size), as the generated
`CNACppTargets.cmake` of a WebAssembly tree shows, so the source-build blocking `Game::Run()` link
recipe names the same targets through the package. On 2026-11-03 (MTH-PLAT-004) an application
built only from the moved Release WEBGPU SDK of `cmake-build-wasm-webgpu`, linking `CNA::CNA` and
`CNA::EmscriptenAbi`, ran blocking `Game::Run()` in headless Chrome 155 through the private runner:
three frames read back from the backbuffer, `Run()` returned after `Exit()`, the game was destroyed
(see the regression gate below). That run fixed the SDK's Storage `--pre-js`, which had named this
source tree; the SDK now ships `share/cna/emscripten/WebStoragePre.js`. Consumers must name the
SDK as a `CMAKE_FIND_ROOT_PATH`; see [emscripten-mainloop-game-lifetime.md](emscripten-mainloop-game-lifetime.md)
for the loop contract.

The generated C API Wasm library uses its separate host-driven, non-Asyncify artifact contract in
[c-api/WASM_ARTIFACT.md](c-api/WASM_ARTIFACT.md); it is not the native CMake package. The same
installed SDK also offers `CNA::CApiStatic`, but a pure-C consumer cannot link it: its targets file
carries no Emscripten link options (exception ABI, C++ runtime, WebGPU port), so wasm-ld reports
undefined exception/C++ runtime symbols (open, `handoff.md` §5.2).

### Version contract

The existing `CNAConfigVersion.cmake` version remains the **C ABI version**
(currently 0.52.0, SameMajorVersion); it is not silently replaced with the C++
runtime's product version. Thus existing `find_package(CNA 0.1 CONFIG)` consumers keep their
contract. `CNA_RUNTIME_VERSION` exposes the separate runtime version after loading
the C++ SDK. C++ consumers still require a compatible compiler, standard library,
architecture and build configuration; the C ABI version is not a promise of C++
binary ABI stability between arbitrary builds.

## Regression gate and coverage

With `CNA_INSTALL_CXX=ON` and native tests enabled, `CnaInstalledCppPackage` builds,
installs, moves and consumes the SDK. It uses one compiler worker and runs serially.
On Linux it invokes the private GPU/window test runner; it never uses the live
desktop. It checks runtime behavior, both shared C/C++ link orders, narrow Math
headers/link closure, static C++ runtime selection, repeated/function-scoped package
lookup, missing dependencies, unknown components, incompatible versions, C-only
consumption and C API followed by C++ consumption.

```sh
ctest --test-dir cmake-build-debug -R '^CnaInstalledCppPackage$' --output-on-failure
```

Logs and `audit.json` remain under the producer's `installed-cpp-probe/run-*`.
Failed artifacts are retained. Successful test-owned artifacts are removed;
run `python3 tools/build/check_installed_cpp_package.py --build-dir <build>
--keep-artifacts` to retain them. No caller-owned source/build/prefix is deleted.

For an assigned consumer workspace, add `--work-dir build-consumer/<fresh-name>`.
The directory must not already exist. The driver copies the consumer sources into
that workspace, moves the installed prefix, clears inherited header/library search
variables, and checks include and link inputs against the producer paths. On Linux,
`readelf -d` and `ldd` logs cover the installed ELF libraries and consumer executables;
package-owned libraries must resolve from the moved prefix without `LD_LIBRARY_PATH`.
Installed `.cmake` and `.pc` files are also checked for producer paths. This is a
path/dependency audit; it does not rename or unmount the shared source/build trees.

When the producer enables the standalone static C archive, the driver builds
`cna_c_api_static` before installing CNACApi and requires its exported target.
The mixed probes compile separate C and C++ translation units and check logger
state in both directions. On Linux the installed-consumer gate also requires the
`CNA_RUNTIME_FORM` configure/generate diagnostic for static C API combinations with the
umbrella, Core, Math and, when present, Shared and the shared C API, in both orders.
`--probe-static-mixed` requests the same negative checks on other native platforms.
The supported pure-C shared/static, C++ SDK alone and shared mixed forms still build and run.

`CnaInstalledRelocationAudit` tests path-leak refusals and the clean static-install
build prerequisite without compiling or executing an SDK. These checks alone do
not qualify any SDK runtime leg.

For a cross-compiled producer the driver hands each consumer the producer's
`CMAKE_TOOLCHAIN_FILE` and `CMAKE_CROSSCOMPILING_EMULATOR` instead of its compilers, and names
the moved prefix as `CMAKE_FIND_ROOT_PATH`. Emscripten's toolchain sets
`CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY`, so an application must do the same: an SDK outside
every find root is not searched for the bundled Sharp Runtime/SDL3 packages. On an Emscripten
producer the consumer CTest cases run each `.js` under that Node emulator (no browser, no
graphics), there is no ELF loader audit (WebAssembly links statically; link fragments are still
audited), and the C API package cases are skipped because Emscripten exports no `CNA::CApi`
(only `CNA::CApiStatic`). The runtime consumer also links `InstalledBrowserFrame`
(`browser.cpp`: `CNA::CNA` + `CNA::EmscriptenAbi`, blocking `Game::Run()`), which the driver
loads in headless Chrome with `scripts/run_browser_tests.mjs` inside
`run_gpu_tests_private.sh --exec`, with `TMPDIR` removed and `CNA_BROWSER_RENDERER` set to the
producer's renderer. It must report the producer's renderer, three frames whose cleared colour
and sprite are read back from the backbuffer, `Run()` returning after `Exit()` and the game's
destruction. `NODE_PATH` must resolve Playwright (for example emsdk's
`node/<version>/lib/node_modules`); `--browser-node` overrides the Node used. `--host-only`
skips this browser leg and says so in `audit.json`.

Apart from that browser page, the consumer fixtures create no window, graphics device or
audio device. In an
environment that cannot start a private compositor, `--host-only` runs these probes
with display variables cleared and SDL dummy video/audio drivers. The audit records
this mode; it provides no GPU/window qualification. The default Linux gate retains
the private display runner.

The 2026-11-01 documentation audit reran `CnaInstalledCppPackage` successfully in
`cmake-build-multi`: the current shared Linux SDK builds, installs, relocates and passes its
runtime/configuration/consumer checks. That producer has `CNA_C_API_BUILD_STATIC=OFF`; this run
does not cover the combined static C archive install prerequisite above.

The 2026-11-03 MTH-PLAT-004 run used the Debug Linux SDLGPU shared SDK with the
standalone static C archive enabled. After fixing SDL3_mixer's standalone loader
path, the host-only gate passed 12 consumer tests, both shared C/C++ state-check
orders, package lookup cases, and installed metadata/link/loader audits. Both
static mixed diagnostic link orders failed with localized COMDAT/typeinfo conflicts;
the owner decided to keep that composition unsupported and refuse it during CMake configuration.
The private compositor could not bind a
socket in the execution sandbox, so this run makes no GPU/window claim.

The 2026-11-03 MTH-PLAT-004 follow-up ran three more Release producers through the driver in its
default private-display mode. All-static (`CNA_SHARED_LIBRARY=OFF`, C API off, SOFTWARE): the
moved prefix holds archives plus the vendored SDL3 DSOs only, consumers need no CNA shared object,
12 consumer tests passed. Video OFF (shared, C API and static archive on, SOFTWARE): no FFmpeg
`NEEDED` entry or package requirement, 18 consumer tests including both mixed C/C++ orders passed,
10 static-mixed refusals. Emscripten (`cmake-build-wasm-webgpu`): see the Emscripten boundary above;
its consumer CTest cases ran under Node only.
MinGW's export/configure graph (DirectX9 and SDL_shadercross/SPIRV-Cross
included) is checked, but a relocated Windows SDK runtime has not yet been tested.
These open validations are listed in `handoff.md` §5.2. The SDK was
built for cna-engine UP-001; its 2026-10-22 checkpoints are in Git history
(`git show 9c2b3e2d4:CNA_ENGINE_REPAIR_HANDOFF.md`).
