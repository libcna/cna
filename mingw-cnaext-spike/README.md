# `mingw-cnaext-spike` — the engine layer as a Windows compiler sees it

`plan_modern.md` **MOD-1719**. Everything here has been run on this machine (2026-08-18) with
`g++-mingw-w64-x86-64` 13.2.0; none of it is a sketch.

```bash
./mingw-cnaext-spike/check.sh          # from the repository root
sudo apt install g++-mingw-w64-x86-64  # the one prerequisite
```

The script needs a `sharp-runtime` checkout beside this one; override with `SHARP_RUNTIME=…`.
It uses `ccache` when present, same as the native builds.

## What it checks, and why each half exists

**1. Every `modules/graphics-ext/src/*.cpp` cross-compiles** (`-fsyntax-only -Wall -Wextra`) under
`x86_64-w64-mingw32-g++`, three times: once each with `CNA_RENDERER_DIRECTX11`, `DIRECTX12` and
`DIRECTX9` defined. That is the plain half — it catches a Linux-only libstdc++ assumption, a
`long`-is-64-bits assumption, a POSIX header slipping into the layer.

**2. `windows_header_collisions.cpp`** is the half that is actually about D3D. A D3D translation
unit includes `<windows.h>` before it includes anything of ours, and `<windows.h>` is a macro
minefield: `near` and `far` are object-like macros in `windef.h`, and `GetObject` and friends are
`#define`d to their `A`/`W` variants. `begin(…, float near, float far)` is the obvious way to write
the depth prepass and it would not compile for a single Windows user. This file includes
`<windows.h>` with none of the usual defensive defines, then every public engine-layer header, and
`#error`s if those macros turn out *not* to be live — otherwise the check would pass by measuring
nothing. Verified to fail as intended by temporarily adding a `float near` parameter to a header.

**3. `msvc_minmax_collisions.cpp`** is the half MinGW alone cannot test. MinGW-w64 guards
`windef.h`'s `min`/`max` macros with `#ifndef __cplusplus`, so a C++ translation unit never sees
them; **MSVC's `windef.h` does not**, and MSVC is the compiler a real D3D build uses. The file
reinstates them by hand after pulling the whole standard library in first (`<bits/stdc++.h>` — a GCC
extension, wrong in shipping code, exactly right here: it takes libstdc++ out of the blast radius so
only our own headers are in it).

This one **does not compile today**, and the reason is outside this repository:
`sharp-runtime`'s `SharpRuntimeHelper.hpp` writes `std::numeric_limits<T>::max()` unparenthesised at
lines 155/160/165/170/175, and the macro eats it. So `check.sh` scores it on blast radius rather
than on success: it requires that no diagnostic *originates* in `modules/graphics-ext`, and prints
what is still failing outside it. If sharp-runtime ever wraps those calls as
`(std::numeric_limits<T>::max)()`, the script says so on its own.

## What it does not do

It does not link, and it does not build the D3D renderers themselves — that needs SDL3, GL and
FFmpeg pre-built for Windows, none of which this container has. The row it closes is about the
*engine layer's* paths, and those are header- and source-level.
