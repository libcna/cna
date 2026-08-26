# Consuming the CNA C API

Everything else under `docs/c-api/` describes what the ABI *is*. This describes how a program that
is not part of this repository gets hold of it, and it is written against a mechanism that is
tested rather than one that sounds right: `CApi_InstalledConsumer` installs the package, configures
`modules/c-api/examples/c` as a standalone project that knows nothing but a prefix path, builds it
and runs it, on every build of this repository.

## The example

`modules/c-api/examples/c/hello_cna.c` is the program to copy. In one file, checked call by checked
call, it does the eight things a first CNA program has to do:

1. check the ABI version before anything else;
2. create a game with C callbacks;
3. borrow the graphics device, which is legal only inside a callback;
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
cmake --install <cna-build-dir> --component CNACApi --prefix /opt/cna
```

The `CNACApi` component is the C ABI and nothing else: one shared library, the public headers, and
the CMake package. Installing without `--component` installs the whole project — 113 MB of SDL and
GoogleTest headers a C consumer has no use for.

Then, in a project that has never heard of this repository:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_game LANGUAGES C)

find_package(CNA 0.1 CONFIG REQUIRED)

add_executable(my_game main.c)
target_link_libraries(my_game PRIVATE CNA::CApi)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/cna
cmake --build build
```

There is exactly one target, `CNA::CApi`, because that is the whole of what this project promises
to anybody outside it. Every canonical C++ module is linked into the shared library **privately**,
behind hidden visibility and a version script, so a consumer neither sees nor needs `cna_core`,
`cna_graphics` or Sharp Runtime. Nothing but `#include <CNA/C/cna.h>` and that one target.

The package version **is** the C ABI version, read out of `abi.h` at configure time so the two
cannot drift. `find_package(CNA 0.1 CONFIG)` accepts **any same-major version at or above the one
requested**, which is what [`ABI_VERSIONING.md`](ABI_VERSIONING.md) promises: reject a different
major, require a minimum minor. So request the version you wrote against and leave it there --
every minor within a major is additive, and raising the request with each one buys nothing. Ask for
a version at all, though: a package request without one accepts anything.

## Without CMake

The library is a plain shared object with C linkage and no C++ in its interface, so any build
system can consume it:

```sh
cc -std=c99 -I/opt/cna/include main.c -L/opt/cna/lib -lcna_c_api -o my_game
```

## Shared and static

The package ships **both**, and they export the same names.

```cmake
target_link_libraries(my_game PRIVATE CNA::CApi)        # the shared library
target_link_libraries(my_game PRIVATE CNA::CApiStatic)  # one archive, nothing to deploy
```

`CNA::CApiStatic` defines `CNA_C_API_STATIC` for you, which is what makes the export macro expand
to nothing; nothing else about your source changes. The same `hello_cna.c` is built both ways from
the installed package and run, on every build of this repository, so the two halves cannot drift.

The shared library keeps its symbol set honest with a version script
(`cmake/CnaCApiExports.map`) and `--exclude-libs,ALL`: `cna_*` and nothing else, 2,897 names pinned
by `tools/c-api/abi_baseline.json`. The archive has no such mechanism available to it, which is why
a static CNA was refused for a long time — `ar`-ing the C API together with every CNA module and
Sharp Runtime would publish tens of thousands of C++ symbols into your program.

So the archive is not simply `ar`'d together. The whole closure is partially linked into **one
relocatable object**, every symbol that is not part of the ABI is localized, and the build **fails**
if any non-`cna_*` symbol survives — except the handful GCC emits as `STB_GNU_UNIQUE` (function-local
statics in inline and template code), which cannot be localized because their uniqueness is what
makes them correct. Those are mangled C++ names no C program can collide with, and the build fails
if a symbol of any other binding appears, so the exception cannot widen quietly.

A static consumer links the archive plus SDL3 (shipped in the package) and the usual
`stdc++ m pthread dl`; when `CNA_ENABLE_VIDEO=AUTO/ON` selected the optional backend, it also links
FFmpeg from the system. The `CNA::CApiStatic` target carries the resolved set for you.

Building the archive can be turned off per build tree with `-DCNA_C_API_BUILD_STATIC=OFF`: it is a
few hundred megabytes in a debug build and is rewritten whenever the library relinks. Where it is
off, the consumer test says so by name rather than testing half a package in silence.

## Native dependencies

`libcna_c_api.so` carries `DT_NEEDED` entries for SDL3, SDL3_image and SDL3_mixer. It additionally
carries FFmpeg entries only when `CNA_ENABLE_VIDEO=AUTO/ON` selected `cna_video_ffmpeg`; an `OFF`
build has no FFmpeg dependency.

**The SDL3 libraries ship with the package.** This project builds them, so the `CNACApi` component
installs them into the same directory as `libcna_c_api.so`, whose `INSTALL_RPATH` is `$ORIGIN`. The
result is that an installed CNA needs **no environment variable of any kind**: it links without
`-rpath-link` and runs without `LD_LIBRARY_PATH`. `CApi_InstalledConsumer` passes neither, which is
how that claim stays true — a regression fails a test instead of surprising a consumer.

**When enabled, FFmpeg does not ship in the package.** `libavcodec`, `libavformat`, `libavutil` and
`libswresample` come from the distribution, and copying a distribution's binaries into this package
would take on their redistribution terms, freeze their soname against future security updates, and
drag in the transitive libraries they were linked against. Install them the ordinary way:

```sh
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

Configure `-DCNA_ENABLE_VIDEO=OFF` to omit that dependency while keeping the entire C video ABI
available. File-backed creation and playback then return `CNA_RESULT_NOT_SUPPORTED`, while declared
metadata remains usable. `AUTO` is the default; see [../video-backend.md](../video-backend.md).

The example runs headless. Set `SDL_VIDEODRIVER=dummy` where there is no display, exactly as this
repository's own tests do.

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
