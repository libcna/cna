# Consuming the CNA C API

Everything else under `docs/c-api/` describes what the ABI *is*. This describes how a program that
is not part of this repository gets hold of it, and it is written against a mechanism that is
tested rather than one that sounds right: `CApi_InstalledConsumer` installs the package, configures
`modules/c-api/examples/c` as a standalone project that knows nothing but a prefix path, builds it
and runs it in applicable native ELF and Mach-O test trees. It exercises the shared target and,
when that tree built it, the static target. Windows currently has header-compilation evidence but
no equivalent installed-package execution gate. Emscripten builds the separately documented
WebAssembly artifact and also installs this package with its static target only; see
[Emscripten](#emscripten).

## The example

`modules/c-api/examples/c/hello_cna.c` is the program to copy. In one file, checked call by checked
call, it does the eight things a first CNA program has to do:

1. check the ABI version before anything else;
2. create a game with C callbacks;
3. borrow the graphics device inside a callback, one of the scopes in which that loan is legal;
4. ask the renderer what it can do instead of assuming;
5. use the count-then-copy idiom every string in this ABI uses;
6. read the diagnostic after a deliberate failure — twice, because an argument error and a handle
   error are different things and the ABI says which;
7. create a texture, upload pixels and draw them;
8. shut down in the documented order: children first, then the game.

It is built at **C99**, the floor the public headers are held to, with `-Wall -Wextra -Wpedantic
-Werror`.

## Building against an installed CNA

```sh
cmake --build <cna-build-dir> --parallel 8
cmake --install <cna-build-dir> --component CNACApi --prefix /opt/cna
```

`cmake --install` does not build missing artifacts, so build the tree first. The `CNACApi` component
contains the native C ABI library, public C headers, CMake package and CNA-built runtime libraries,
plus the static archive when that tree supports and enables it. External system dependencies remain
external, as described below. With `CNA_SHARED_LIBRARY=ON` it also ships `libcna.so`, shared with
C++ consumers; do not deploy the C adapter without that runtime. Keep the adapter and runtime from
the same build: the C ABI version is not a promise of binary compatibility between arbitrary C++
runtime builds. Installing without `--component` also installs unrelated C++ SDK, tool and
dependency material that a C-only consumer does not need.

This section describes native packages. An Emscripten build installs the same `CNAConfig.cmake`
with `CNA::CApiStatic` and no `CNA::CApi`; see [Emscripten](#emscripten). The ready-made browser
library (an ES module plus its `.wasm`) is described in [`WASM_ARTIFACT.md`](WASM_ARTIFACT.md).

Then, in a project that has never heard of this repository:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_game LANGUAGES C)

find_package(CNA 0.1 CONFIG REQUIRED COMPONENTS CApi)

add_executable(my_game main.c)
target_link_libraries(my_game PRIVATE CNA::CApi)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/cna
cmake --build build
```

The shared C target is `CNA::CApi`. Its implementation dependencies remain private: a C consumer
needs neither C++ module targets nor Sharp Runtime headers. In a shared-runtime build the adapter
links `libcna.so` instead of embedding a second copy of CNA's process-wide state. In a static-runtime
build it embeds the modules as before. The adapter itself exports only the C ABI in either mode.
Use `#include <CNA/C/cna.h>` and the one target.

The package version **is** the C ABI version, read out of `abi.h` at configure time so the two
cannot drift. `find_package(CNA 0.1 CONFIG)` accepts **any same-major version at or above the one
requested**. Request the version you wrote against, and ask for one at all: a package request
without a version accepts anything.

That is the whole check from ABI `1.0` on, where every minor within a major is additive. It is not
enough while the ABI is `0.x`: there any minor may break ([`ABI_VERSIONING.md`](ABI_VERSIONING.md)),
and the package will still accept a newer one. A `0.x` program therefore also checks
`cna_get_abi_version()` at startup and refuses a minor other than the one it was built against, as
step 1 of the example does.

## Without CMake

The library is a plain shared object with C linkage and no C++ in its interface, so any build
system can consume it:

```sh
cc -std=c99 -I/opt/cna/include main.c -L/opt/cna/lib \
  -Wl,-rpath,/opt/cna/lib -lcna_c_api -o my_game
```

That example is the ELF spelling. `-L` is only a link-time search path; without the executable
RUNPATH (or a system loader configuration) the program will build but `libcna_c_api.so` will not be
found when it starts. The CMake target supplies the corresponding runtime search path automatically.
Other build systems must reproduce both the platform's link and loader settings. For the static
archive, prefer consuming the generated CMake target: its configuration-specific external link
closure is deliberately more than a single `-l` flag.

## Shared and static

Every native package ships `CNA::CApi`. A Unix tree also ships `CNA::CApiStatic` when Python is
available and `CNA_C_API_BUILD_STATIC=ON` (the default); current Windows packages do not generate
that archive. The two targets expose the same callable C ABI.

```cmake
target_link_libraries(my_game PRIVATE CNA::CApi)        # the shared library
target_link_libraries(my_game PRIVATE CNA::CApiStatic)  # the static C adapter archive
```

`CNA::CApiStatic` defines `CNA_C_API_STATIC` for you, which is what makes the export macro expand
to nothing; nothing else about your source changes. `CApi_InstalledConsumer` builds and runs the
same `hello_cna.c` both ways whenever the archive is present, and reports the static half as skipped
when the tree deliberately disabled it.

The shared library keeps its symbol set honest with a version script
(`modules/c-api/cmake/CnaCApiExports.map`) and `--exclude-libs,ALL` on ELF, and with ld64's
exported-symbols list (`modules/c-api/cmake/CnaCApiExports.darwin.txt`) on macOS: `cna_*` and
nothing else, 3,397 names pinned by `tools/c-api/abi_baseline.json`, including
`cna_texture_is_format_allowed_by_profile_ext`,
`cna_texture_is_cube_format_allowed_by_profile_ext` and
`cna_texture_is_render_target_format_allowed_by_profile_ext`. `CApi_Exports` checks the built
library with the matching platform tool. The archive has no such linker mechanism available to it,
which is why a static CNA was refused for a long time — `ar`-ing the C API together with every CNA
module and Sharp Runtime would publish tens of thousands of C++ symbols into your program.

So the archive is not simply `ar`'d together. The whole closure is partially linked into **one
relocatable object**, every symbol that is not part of the ABI is localized, and the build **fails**
if any non-`cna_*` symbol survives — except the handful GCC emits as `STB_GNU_UNIQUE` (function-local
statics in inline and template code), which cannot be localized because their uniqueness is what
makes them correct. Those are mangled C++ names no C program can collide with, and the build fails
if a symbol of any other binding appears, so the exception cannot widen quietly.

A static consumer links the archive plus the exact external closure of the producing build. That
normally includes the C++ runtime and platform libraries, the packaged SDL libraries and, for a
`WEBGPU` build, packaged wgpu-native. Configuration-selected system libraries can include FFmpeg,
libcurl, Opus, zstd and, on Linux SDLGPU builds with custom `ShaderEffect` support, shaderc.
`CNA::CApiStatic` carries the resolved set from the producing link line. Some system entries are
recorded as absolute paths, so the installed static package is qualified on a machine compatible
with the one that produced it; it is not a claim that those external libraries were bundled or that
the archive is freely portable between distributions. On macOS the same archive is made with
ld64's partial link (`ld -r -all_load`) and `nmedit -s`, and its interface is `c++` plus the
frameworks and dylibs the closure links -- see [macOS](#macos).

The static C archive embeds its own runtime and is supported as the single-file CNA archive for
pure-C consumers. Mixed C/C++ programs use `CNA::CApi` with the shared C++ SDK
(`libcna_c_api.so` + `libcna.so`), which share one runtime in either link order. Installed CMake
targets refuse `CNA::CApiStatic` combined with C++ SDK targets or the shared-runtime C adapter
at configure/generate time with a `CNA_RUNTIME_FORM` conflict. For pkg-config and plain linker
consumers, the same unsupported-combination rule applies; those tools have no equivalent guard.

Building the archive can be turned off per build tree with `-DCNA_C_API_BUILD_STATIC=OFF`: a debug
archive is large and is rewritten whenever the library relinks. Where it is off, the consumer test
says so by name rather than testing half a package in silence.

## Native dependencies

The dynamic dependency closure is configuration-dependent. In a shared-runtime build,
`libcna_c_api.so` depends on `libcna.so`; the latter carries renderer, platform, audio, content and
other selected system dependencies. A static-runtime adapter carries those edges itself. Inspect
the produced library (`readelf -d`, `otool -L` or the platform equivalent) rather than assuming a
fixed list from the renderer name alone.

**CNA-built SDL3 and SDL3_mixer ship with the package.** When CNA uses its vendored SDL build, the
`CNACApi` component installs those libraries beside `libcna_c_api.so` and `libcna.so`. A tree
configured with `CNA_USE_SYSTEM_SDL=ON` instead leaves SDL as a system dependency. The installed CNA
libraries use `$ORIGIN` on ELF; the imported `CNA::CApi` target also gives the executable a path to
the package library directory. Consequently the tested CMake consumer needs neither `-rpath-link`
nor `LD_LIBRARY_PATH` for the package-owned libraries. `CApi_InstalledConsumer` passes neither.
Both CNA shared libraries need their own `$ORIGIN` entry because ELF RUNPATH does not propagate
from one dependency to the next.

**wgpu-native ships too, when the `WEBGPU` renderer is compiled in.** CNA's build chooses that
binary itself — the pinned, checksum-verified wgpu-native release it fetches, or the one
`CNA_WEBGPU_ROOT` names — so it is in the same position as SDL3 rather than FFmpeg: there is no
system copy for you to match, and it links nothing beyond the C runtime. The `CNACApi` component
installs `libwgpu_native.so` beside `libcna_c_api.so` (`wgpu_native.dll` beside `cna_c_api.dll` on
Windows), and `CNA::CApiStatic` links that installed copy. The official release has no SONAME, so
the runtime's `DT_NEEDED` entry is the file name `libwgpu_native.so`: keep that name when you
deploy it.

**Other selected libraries remain system dependencies.** Depending on the producing configuration,
these can include the toolchain C++ runtime and platform libraries, libcurl, Opus, zstd, FFmpeg and
shaderc. The package does not claim to bundle them merely because its CMake target supplies the
link information. The dynamic package resolves their sonames through the system loader; the static
target records the producing build's resolved link closure. Install the same enabled features on
the deployment machine, or turn optional features off when configuring CNA.

**When enabled, FFmpeg does not ship in the package.** `libavcodec`, `libavformat`, `libavutil` and
`libswresample` come from the distribution, and copying a distribution's binaries into this package
would take on their redistribution terms, freeze their soname against future security updates, and
drag in the transitive libraries they were linked against. Install them the ordinary way:

```sh
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

Configure both `-DCNA_ENABLE_VIDEO=OFF` and `-DCNA_ENABLE_WAVEBANK_CODECS=OFF` to omit runtime FFmpeg
dependencies while keeping the C APIs available. With video OFF, file-backed video creation and
playback return `CNA_RESULT_NOT_SUPPORTED`, while declared metadata remains usable. WaveBank codecs
are selected independently: video OFF with audio AUTO/ON can still require FFmpeg. Both options
default to AUTO; see [../video-backend.md](../video-backend.md) and
[../wavebank-codecs.md](../wavebank-codecs.md).

The example creates a game and graphics device, so it is not renderer-independently headless.
`SDL_VIDEODRIVER=dummy` is suitable for `HEADLESS`, `SOFTWARE` and `STUB`; windowed or GPU
renderers need a compatible display. CNA's test suite runs such consumers through the private
Weston/Xwayland runner rather than the owner's desktop.

## macOS

Everything above holds on macOS (Apple silicon, macOS 13.3 or newer); each mechanism has its
Mach-O spelling. `CApi_InstalledConsumer` is registered there and installs, builds and runs the
shared package plus the static target when enabled. This is local native-platform evidence; the
repository currently has no macOS C API CI job.

- **The library** is `libcna_c_api.dylib`. Its install name is `@rpath/libcna_c_api.dylib`, and its
  Mach-O compatibility and current versions are the ABI's `MAJOR.MINOR.0` and
  `MAJOR.MINOR.PATCH`, so dyld refuses an older minor than the one a program was linked against.
  It exports `_cna_*` and nothing else, through ld64's exported-symbols list
  (`modules/c-api/cmake/CnaCApiExports.darwin.txt`); `CApi_Exports` reads that back with `nm -gU`.
- **Vendored SDL3 ships beside it**, and the library's `LC_RPATH` is `@loader_path` -- `$ORIGIN`'s meaning
  for dyld. `find_package(CNA)` gives your program an rpath to the package's `lib` directory, so
  nothing needs a `DYLD_*` variable; the consumer test reads the installed dylib's load commands to
  keep it that way.
- **The libraries CNA did not build stay outside the package**, as FFmpeg does on Linux, and for
  the same reasons. Which ones a build links depends on its configuration: libcurl (required on
  desktop for the gamer-services TLS transport), FFmpeg (`CNA_ENABLE_VIDEO` or
  `CNA_ENABLE_WAVEBANK_CODECS` set to AUTO/ON), Opus (network
  voice, when found) and zstd (CNB compression, when found). With Homebrew:

  ```sh
  brew install curl ffmpeg opus zstd
  ```

  They are linked by the path each library advertises (`/opt/homebrew/opt/...`), which survives a
  `brew upgrade`. Homebrew's bottles target the macOS they were built on, so a package linked
  against them runs only on that macOS or newer, whatever `CNA_MACOS_DEPLOYMENT_TARGET` says
  (ld64 says so at link time). The SDK's own libcurl works for TLS but has no WebSocket support,
  and a build that found it refuses the online relay transport by name.
- **Create and run the game on the main thread.** SDL's Cocoa video driver initializes only on the
  process's main thread. Off it, a renderer that needs that driver is refused with a message that
  says so, and SOFTWARE runs on SDL's offscreen driver without a window -- see
  [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).
- **Without a display**, `SDL_VIDEODRIVER=dummy` serves the SOFTWARE renderer.
  SDL's `offscreen` driver creates its contexts through EGL, which macOS does not have.

## Emscripten

An Emscripten tree with the C API and `CNA_C_API_BUILD_STATIC=ON` (the default) installs this
package with its static target only. `cmake --install <tree> --component CNACApi --prefix <sdk>`
places the public C headers, `libcna_c_api_static.a`, `CNAConfig.cmake` with
`CNACStaticTargets.cmake`, and the JavaScript files the archive's link contract names (today the
storage pre-js, in `share/cna/emscripten/`). There is no `CNA::CApi`: a shared wasm library would be
a side module (CABI-14). A pure-C project builds against it with the Emscripten toolchain and names
the SDK as a find root, because that toolchain searches packages only below its find roots:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_game LANGUAGES C)

find_package(CNA 0.1 CONFIG REQUIRED COMPONENTS CApiStatic)

add_executable(my_game main.c)
target_link_libraries(my_game PRIVATE CNA::CApiStatic CNA::CApiStaticAsyncify)
```

```sh
emcmake cmake -S . -B build -DCMAKE_FIND_ROOT_PATH=<sdk>
cmake --build build
```

The archive is the C API and its whole closure (CNA's modules, Sharp Runtime, SDL3, SDL3_mixer,
enet, MojoShader) merged into one archive. Unlike the native archive it is not partially linked or
localized -- wasm-ld settles a program's contents at its final link -- so it must be the program's
only copy of those libraries.

`CNA::CApiStatic` carries the link contract the archive needs, so the program adds no flag of its
own. Nothing writes that contract down by hand: the producing build links a C program,
`cna_c_api_static_link_contract`, against the C API, records that link command instead of running
it, and `tools/c-api/generate_static_archive.py` turns the command into the target. The options are
therefore the closure's interface link options, the same ones the C++ SDK's targets carry:

- the exception ABI the archive was compiled for (`-fexceptions`, `-sDISABLE_EXCEPTION_CATCHING=0`:
  Emscripten's JavaScript-based exceptions), plus `-sDEFAULT_TO_CXX=1`, which makes `emcc` link
  libc++ and libc++abi as `em++` does, in the variant those options select;
- the renderer's link contract: the `emdawnwebgpu` port for `WEBGPU`, the exact WebGL 2 request for
  `WEBGL2`;
- `-lembind`, and for storage `-lidbfs.js`, `-sFORCE_FILESYSTEM=1` and the `--pre-js`, named at its
  installed path;
- the runtime's stack size and, in a `CNA_ENABLE_EMSCRIPTEN_THREADS` build, `-pthread` and its
  companions.

The generator refuses an option that names a path of the producing machine, and fails when the
contract program links an archive that the merged archive lacks.

Asyncify is not in `CNA::CApiStatic`, just as it is not in the C++ SDK's `CNA::CNA`.
`CNA::CApiStaticAsyncify` carries the options of `CNA::EmscriptenAsyncify` (`-sASYNCIFY=1` and, in a
build with `WEBGPU`, its larger unwind stack). Link it for blocking `cna_game_run`, and with `WEBGPU`
for any graphics device at all: that renderer waits for the browser's adapter, device and
buffer-map promises by suspending the Wasm stack. A `WEBGL2` program whose JavaScript drives
`cna_game_run_frame_ext` from animation frames needs neither, and a library entered from
JavaScript must not take it ([`WASM_ARTIFACT.md`](WASM_ARTIFACT.md)). As natively,
`CNA::CApiStatic` is for pure-C programs and is refused beside C++ SDK targets.

Evidence (MTH-PLAT-004, 2026-11-03): from the moved Release `WEBGPU` SDK of
`cmake-build-wasm-webgpu`, installed with only `CNACApi`, a C99 project with only C enabled linked
`cmake/Tests/InstalledRuntimeProbe/api.c` -- ABI version, logger, and a CNB parse the library
refuses by catching a C++ exception -- and ran it under Node; and linked `capi_browser.c`, which in
headless Chrome 155 reported `WEBGPU`, read a texture back from the GPU, cleared three frames,
returned from `cna_game_run` after `cna_game_request_exit` and destroyed the game.
`tools/build/check_installed_cpp_package.py` runs both for every Emscripten producer. Not covered:
`WEBGL2`, a threaded build, other browsers.

## Where to go next

| Question | Document |
|---|---|
| What does a handle mean, and when is it dead? | [`HANDLES.md`](HANDLES.md) |
| What does a failure tell me? | [`ERRORS.md`](ERRORS.md) |
| How do strings and buffers work? | [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md) |
| Which thread may call what, and in what order do I shut down? | [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md) |
| What is this renderer actually able to do? | [`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md) |
| What may change between versions? | [`ABI_VERSIONING.md`](ABI_VERSIONING.md) |
| Which compilers and language modes is this proved against? | [`COMPATIBILITY.md`](COMPATIBILITY.md) |
