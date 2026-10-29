# Installed C++ SDK

`CNA_INSTALL_CXX=ON` adds an opt-in, relocatable CMake package for the configured
CNA modules. It does not change the default source-tree `add_subdirectory` workflow
or enable installation in embedded Sharp Runtime builds unless the CNA SDK is
explicitly requested. The SDK uses the actual renderer/platform/module selection
of its producer; installing does not enable unavailable modules.

The Sharp Runtime checkout must include its modular install support (published
`next` at `1dc8785b` or a compatible descendant). An older checkout declaring the
same 0.1.0 version is not sufficient evidence of that support.

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

The **whole prefix** can then be moved. Headers have separate module include roots,
so linking `CNA::Math` does not accidentally expose Graphics or System.Xml headers.
Private compiled dependencies and their needed headers travel with the SDK.
System dependencies remain system dependencies: install the development packages
required by the producer's configuration (for example FFmpeg, FreeType, Zlib,
Curl, Opus, nlohmann-json, Zstd or shaderc). Their paths are discovered on the
consumer, not frozen to the build machine. The package discovers the dependencies
of the configured SDK; linking a narrow module still retains its narrow link graph.
ALSA is loaded dynamically by CNA and does not add a link-time libasound requirement.

## Consume

```cmake
project(MyGame LANGUAGES CXX)
find_package(CNA CONFIG REQUIRED COMPONENTS
    Core Math Input GraphicsCore Content Runtime Audio)
add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE
    CNA::Core CNA::Math CNA::Input CNA::GraphicsCore
    CNA::Content CNA::Runtime CNA::Audio)
```

Configure the consumer with `-DCMAKE_PREFIX_PATH=/path/to/moved-sdk`. No CNA source
directory, manually assembled include paths or `add_subdirectory` fallback is
needed. `SharpRuntime::` component targets come from the installed Sharp Runtime
package. `CNA::CNA` (and the existing `CNA` spelling) is the umbrella. Targets named
`Internal_*` or `Dependency*` are implementation details, not stable public modules.

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

### Version contract

The existing `CNAConfigVersion.cmake` version remains the **C ABI version**
(currently 0.50.0, SameMajorVersion); it is not silently replaced with the C++
runtime's 0.1.0. Thus existing `find_package(CNA 0.1 CONFIG)` consumers keep their
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

Campaign validation currently covers native Linux shared and static SDKs. Static
SDK validation has C API disabled and does **not** claim mixed static C/C++ adapter
identity. MinGW's export/configure graph (DirectX9 and SDL_shadercross/SPIRV-Cross
included) is checked, but a relocated Windows SDK runtime has not yet been tested.
The WebAssembly/WebGPU export/configure graph also generates successfully; that
and the build-support helper's Emscripten test are not a full installed WebAssembly
SDK validation. These open validations are listed in `handoff.md` §5.2. The SDK was
built for cna-engine UP-001; its 2026-10-22 checkpoints are in Git history
(`git show 9c2b3e2d4:CNA_ENGINE_REPAIR_HANDOFF.md`).
