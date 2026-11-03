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
recipe names the same targets through the package. An installed blocking-loop consumer has still
not been linked and run, so final Emscripten executables must still establish their own qualified
blocking-loop/host-frame policy; see
[emscripten-mainloop-game-lifetime.md](emscripten-mainloop-game-lifetime.md).

The generated C API Wasm library uses its separate host-driven, non-Asyncify artifact contract in
[c-api/WASM_ARTIFACT.md](c-api/WASM_ARTIFACT.md); it is not the native CMake package.

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
state in both directions. The optional `--probe-static-mixed` diagnostic adds
`InstalledStaticIsolation_*` targets for the unsupported composition of
`CNA::CApiStatic` with the C++ SDK. On Linux this can fail to link because localized
COMDAT sections conflict with the consumer's C++ sections. If it links, the probe
checks separate runtime state; neither result establishes shared-state support.
Use the shared adapter/runtime for that contract, as described in
[C API consumption](c-api/CONSUMING.md).

`CnaInstalledRelocationAudit` tests path-leak refusals and the clean static-install
build prerequisite without compiling or executing an SDK. These checks alone do
not qualify any SDK runtime leg.

The consumer fixtures create no window, graphics device or audio device. In an
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
that composition remains unsupported. The private compositor could not bind a
socket in the execution sandbox, so this run makes no GPU/window claim.

Earlier campaign validation also covers an all-static C++ SDK with C API disabled;
it does **not** claim mixed static C/C++ adapter identity.
MinGW's export/configure graph (DirectX9 and SDL_shadercross/SPIRV-Cross
included) is checked, but a relocated Windows SDK runtime has not yet been tested.
The WebAssembly/WebGPU export/configure graph also generates successfully; that
and the build-support helper's Emscripten test are not a full installed WebAssembly
SDK validation. These open validations are listed in `handoff.md` §5.2. The SDK was
built for cna-engine UP-001; its 2026-10-22 checkpoints are in Git history
(`git show 9c2b3e2d4:CNA_ENGINE_REPAIR_HANDOFF.md`).
