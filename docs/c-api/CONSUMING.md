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
cannot drift. `find_package(CNA 0.1 CONFIG)` accepts a same-minor match, which is what
[`ABI_VERSIONING.md`](ABI_VERSIONING.md) promises within experimental `0.x`. Ask for the version;
a package request without one accepts anything.

## Without CMake

The library is a plain shared object with C linkage and no C++ in its interface, so any build
system can consume it:

```sh
cc -std=c99 -I/opt/cna/include main.c -L/opt/cna/lib -lcna_c_api -o my_game
```

## Shared and static

The C API is built and installed as a **shared** library, and that is not incidental. The version
script (`cmake/CnaCApiExports.map`) and `--exclude-libs,ALL` are what keep the exported symbol set
to `cna_*` and nothing else — 2,720 names, pinned by `tools/c-api/abi_baseline.json`. A static
build would export every C++ symbol of every module it archived, and the ABI promise would be
meaningless. `CNA_C_API_STATIC` is reserved for a future static configuration; it is not one today.

## What the package does not do yet

`libcna_c_api.so` carries `DT_NEEDED` entries for **SDL3, SDL3_image, SDL3_mixer and FFmpeg**,
which the `CNACApi` component does not install — they are not this project's to redistribute. The
library's `INSTALL_RPATH` is `$ORIGIN`, so a deployment that places those libraries beside it works
with no environment variable at all; otherwise they must be where the dynamic loader already looks.
Until a decision is made about shipping them, this is the one step a consumer has to take by hand,
and `CApi_InstalledConsumer` takes it too rather than pretending otherwise: it passes
`-Wl,-rpath-link` at link time and `LD_LIBRARY_PATH` at run time.

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
