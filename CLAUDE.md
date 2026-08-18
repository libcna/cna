# CNA Project Guidelines — FNA to C++ Porting Instructions

## Project Overview

**CNA** is a C++ reimplementation of the XNA 4.0 programming model, built on a CNA-owned platform abstraction
(SDL3 is the default implementation) and a pluggable graphics renderer layer. It is a framework/runtime and
abstraction layer — not a game — designed to preserve XNA-style APIs
(`Microsoft::Xna::Framework`) while using modern C++23 internals.

### Source Reference

The authoritative behavioral and API reference is the local FNA source tree:

```text
/rv/data/library/github.com/FNA-XNA/FNA
```

Do **not** treat old CNA code or AI-generated stubs as authoritative if they conflict with the FNA reference API.

---

## Code Generation Rules

### XNA 4.0 API Compliance

When implementing code in the `Microsoft::Xna` namespace:

- **MUST** strictly adhere to the XNA 4.0 API specification as implemented in FNA.
- **MUST** preserve original XNA 4.0 class names, method signatures, and behavior.
- **MUST** use modern C++23 internally while maintaining XNA-style public APIs.
- If implementing functionality that is **NOT** part of the XNA 4.0 API within the `Microsoft::Xna` namespace,
  you **MUST** wrap it with the `CNAEXT` macro.

`CNAEXT` is defined in `modules/core/include/CNA/CNAHelper.hpp` as an empty marker macro used to visually tag non-XNA extensions.

---

## Namespaces

Original XNA types must stay in the matching XNA namespace:

```cpp
Microsoft::Xna::Framework::Color
Microsoft::Xna::Framework::Vector3
Microsoft::Xna::Framework::Graphics::Texture2D
Microsoft::Xna::Framework::Graphics::SpriteBatch
```

The `CNA` namespace is for project-specific extensions, helpers, internal renderers, and non-XNA additions only.
Do not move original XNA API types into the `CNA` namespace.

---

## API Names Must Match XNA/FNA

Class names, struct names, enum names, method names, operator names, and constant/static member names must
match the XNA/FNA API exactly.

Do not rename API members to make them more C++-like if that would diverge from XNA/FNA.

---

## C# Properties → C++ Convention

C# properties use the established CNA convention:

```cpp
getXProperty()   // getter
setXProperty(…)  // setter
```

Example:

```csharp
// C#
public byte R { get; set; }
```

```cpp
// C++
[[nodiscard]] bytecs getRProperty() const;
void setRProperty(bytecs value);
```

Do not replace C# properties with public fields unless the type already establishes that style.

---

## Type Aliases (SharpRuntime)

When C# source uses .NET primitive type names, preserve the corresponding alias from `SharpRuntime`:

| C# type   | SharpRuntime alias     | Underlying C++ type |
|-----------|------------------------|---------------------|
| `byte`    | `bytecs` / `Byte`      | `uint8_t`           |
| `sbyte`   | `sbytecs` / `SByte`    | `int8_t`            |
| `short`   | `shortcs` / `Int16`    | `int16_t`           |
| `ushort`  | `ushortcs` / `UInt16`  | `uint16_t`          |
| `int`     | `intcs` / `Int32`      | `int32_t`           |
| `uint`    | `uintcs` / `UInt32`    | `uint32_t`          |
| `long`    | `longcs` / `Int64`     | `int64_t`           |
| `ulong`   | `ulongcs` / `UInt64`   | `uint64_t`          |
| `float`   | `Single`               | `float`             |
| `string`  | `String`               | `std::string`       |
| `char`    | `charcs`               | `char16_t`          |

Include `"SharpRuntime/SharpRuntimeHelper.hpp"` to access these. If a needed alias does not yet exist, add
a minimal stub/alias in `sharp-runtime` rather than using a raw C++ type directly in the XNA API surface.

---

## Events

C# events and delegates are modeled through `System::EventHandler<TEventArgs>`:

```cpp
// Declaration (in class, matches C# "public event EventHandler<T> Name;")
System::EventHandler<ExitingEventArgs> Exiting;

// Subscription
Exiting += [](System::Object* sender, const ExitingEventArgs& e) { … };

// Raising
Exiting.Raise(this, args);
// or
Exiting.Invoke(this, args);
```

`EventHandler<T>` stores subscribed callbacks and exposes `Raise()` / `Invoke()`. This is the project-wide
pattern; do not invent a different event mechanism.

---

## Interfaces and Inheritance

Preserve C# interface relationships as C++ abstract base classes.

```csharp
// C#
class Color : IEquatable<Color>, IPackedVector, IPackedVector<uint> { … }
```

```cpp
// C++
struct Color : public Graphics::PackedVector::IPackedVectorT<UInt32> { … };
```

If an exact mapping is not practical, implement equivalent behavior and document the intentional deviation
in the PR description or task report — not in source comments.

---

## IDisposable

C# `IDisposable` is mapped to `System::IDisposable` (from sharp-runtime):

```cpp
class Foo : public System::IDisposable {
public:
    void Dispose() override;
protected:
    void Dispose(bool disposing);   // only when the pattern requires it
};
```

Always check `isDisposed_` before acting; throw `std::runtime_error` if used after disposal.

---

## Visibility Mapping

Map C# visibility intentionally — do not make every member public:

| C#        | C++                                                              |
|-----------|------------------------------------------------------------------|
| `public`  | `public`                                                         |
| `internal`| `private`, `protected`, detail/internal namespace, or omit entirely |
| `private` | `private`                                                        |

C# `internal DebugDisplayString` should **not** become a public C++ API method.

---

## File Structure

The repository is a **module-oriented monorepo** (Phase-3 physical layout, see
`docs/physical-modules.md`): every subsystem and every renderer implementation family owns
`modules/<name>/{CMakeLists.txt,include/,src/,tests/,examples/}` (examples/ present where a
module has examples).

- Declarations and public documentation: `.hpp` under the owning module's `include/`
- Implementation: `.cpp` under the owning module's `src/`

Each module's `include/` root **reproduces the public namespace path**, so consumer include
spelling is stable API and identical across modules:

```text
modules/graphics/include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp
modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp
modules/audio/include/Microsoft/Xna/Framework/Audio/SoundEffect.hpp
```

all still included as `#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"` etc.

The **src/ tree of a module** keeps the Phase-2 area convention: XNA API implementation under
`src/Xna/`, `CNA::Internal` engine parts under `src/Internal/`, CNAEXT extension surfaces under
`src/CnaExt/`; single-area modules stay flat. Renderer implementations live in
`modules/renderers/<family>/src/`.

```text
modules/graphics/src/Xna/Texture2D.cpp        # Microsoft::Xna::Framework::Graphics::Texture2D
modules/input/src/Internal/SdlInputBridge.cpp # CNA::Internal::Input
modules/math/src/Vector3.cpp                  # Framework-root math type
modules/renderers/vulkan/src/…                # one directory per renderer family
```

A new production `.cpp` placed outside every module directory fails the configure (the
physical source-partition validator in `modules/CMakeLists.txt` keeps ownership total).

Avoid putting non-template implementations in headers.

---

## Behavior Fidelity

Match XNA/FNA behavior over personal C++ preference. Preserve:

- Packed value layouts (e.g. Color: AABBGGRR)
- Clamping behavior
- Integer cast behavior
- Operator behavior
- Method overloads
- Default values
- Exception behavior where practical (`std::out_of_range`, `std::runtime_error`)

Do not redesign behavior for cleaner C++ if that diverges from XNA/FNA.

---

## Member Order

Keep C++ member order close to the C# source order where practical. This makes diff-based review easier.

---

## No Backward Compatibility Hacks

Do not add old CNA aliases or shortcuts just to keep outdated demos compiling.

Wrong:

```cpp
inline const Color CornflowerBlue(…);
```

Correct:

```cpp
Color::CornflowerBlue
```

If old game code breaks after the API is corrected, fix the game code separately.

---

## Comments and Documentation

- **Every public method, constructor, property getter/setter, operator, and constant in every `.hpp` file MUST have a Doxygen comment.**
- Use the **full Doxygen block style** with `@brief`, `@param`, and `@return` where applicable:
  ```cpp
  /**
   * @brief Short description of what the method does.
   *
   * @param paramName Description of the parameter.
   * @return Description of the return value.
   */
  ```
- For simple members with no parameters and no return value (e.g. a void method, a constant, an enum value),
  a single-line `/** @brief Short description. */` is acceptable.
- **Never** use bare `///` comments on public API members — always use the `/** */` block form with at least `@brief`.
- Copy the intent of public C# XML doc comments (`<summary>`, `<param>`, `<returns>`) into Doxygen.
  The text can often be taken verbatim from FNA and placed in `@brief` / `@param` / `@return`.
- Do not copy comments word-for-word if rephrasing is clearer.
- **Never** add comments like "taken from FNA", "copied from FNA", or "based on FNA source".
- Keep internal implementation free of redundant comments. Only add a comment when the WHY is non-obvious.
- `///` style comments are acceptable only inside method bodies for brief inline notes — not on public API declarations.

---

## SharpRuntime Extensions

`sharp-runtime` is the project's C++ reimplementation of the .NET runtime (`System.*` namespace and primitive
type aliases). If CNA needs anything from .NET that is not yet in sharp-runtime — a type alias, a class, an
interface, an exception type, a utility — **add it to sharp-runtime first**, then use it from CNA.

Do **not** inline .NET concepts directly into CNA headers as raw C++ types or ad-hoc workarounds.

Examples of things that belong in sharp-runtime, not in CNA:

- Type aliases (`bytecs`, `String`, `Single`, …)
- `System::IDisposable`, `System::Object`, `System::EventHandler<T>`
- `System::Exception` and its subclasses
- `System::TimeSpan`, `System::IO::Stream`, `System::Collections::*`
- Any other `System.*` type referenced by the XNA 4.0 API

When adding to sharp-runtime, follow the same minimal-stub rule: implement only what CNA currently needs,
with the correct final name and namespace.

---

## Missing Dependencies

When porting a file and a required class, enum, or type alias does not yet exist:

- If the missing type belongs to the .NET runtime (`System.*` or a primitive alias), add it to **sharp-runtime** (see above).
- Otherwise add a **minimal correctly-named stub** in the correct namespace, sufficient for compilation.
- Do not implement large unrelated systems to satisfy a missing dependency.
- Stubs must use the correct final namespace and name from XNA/FNA.
- Report missing stubs in the task/PR description.

---

## Static Members and Named Constants

C# `static readonly` fields become C++ `static const` members defined in `.cpp`:

```csharp
// C#
public static readonly Color CornflowerBlue = new Color(…);
```

```cpp
// .hpp
static const Color CornflowerBlue;

// .cpp
const Color Color::CornflowerBlue{…};
```

---

## Porting Workflow — Per-file Checklist

Every `.cs` file ported from FNA to CNA **must be complete** — not partial. "Make and forget" means the file is
done in one pass. Do not skip any checklist item and come back later.

The full per-file checklist is in:

```text
CHECKLIST.md
```

Use it for every file. The minimum requirements are:

- `// SPDX-License-Identifier: MS-PL` at the top of both `.hpp` **and** `.cpp`.
- `#include "CNA/CNAHelper.hpp"` in `.hpp` if `CNAEXT` is used anywhere in that file.
- Every method body verified **line-by-line** against the FNA equivalent.
- Every intentional deviation from FNA logic documented with a `//` comment in the source.
- Concrete classes that inherit `System::Object` **must** override `GetTypeName()` with `CNAEXT`:
  ```cpp
  CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
  ```
  The return value is the fully-qualified .NET name, e.g. `"Microsoft.Xna.Framework.Game"`.
- Tests: every public method, operator, and constant covered. Out-ref overloads tested separately.
  See CHECKLIST.md for the complete test requirements.

The table of known acceptable C++ deviations from FNA/XNA (e.g. `GetHashCode()` returning `std::size_t`,
`ref`/`out` → value-ref pairs, null guards omitted for C++ references) is maintained in `CHECKLIST.md`.

---

## Tests

Every public XNA 4.0 API method, constructor, operator, and constant **must** have at least one unit test.
Tests live under the owning module's `modules/<name>/tests/` mirroring the namespace path
(shared fixture assets and the cross-module minimal-link probes stay under top-level
`tests/`), using Google Test.

Rules:
- Every method overload must be covered by at least one test case.
- Out-ref overloads (e.g. `void Contains(const BoundingBox&, ContainmentType&)`) must be tested separately from the value-returning variants.
- Static factory methods (`CreateFromPoints`, `CreateMerged`, `CreateFromSphere`, …) each need their own test.
- Equality operators (`==`, `!=`) and `Equals()` must be tested for both equal and unequal cases.
- `ToString()` must be tested for expected format.
- `GetHashCode()` must be tested for consistency (equal objects produce equal hashes).
- Tests that exist before a new implementation lands are insufficient — when adding a new method, add or extend tests in the same task.
- Do not mark an API as "complete" in AUDIT.md unless its tests are also complete.

---

## Build and Report

After making changes:

1. Build the affected target (`cmake --build cmake-build-debug --target CNA`).
2. Report: changed files, added stubs, missing dependencies, intentional deviations, build result, remaining errors.

Default debug build dir: `cmake-build-debug/`. Vulkan build dir: `cmake-build-vulkan/`.
Multi-renderer build dir: `cmake-build-multi/` (see below).

### Runtime renderer selection (second build mode)

Compile-time renderer selection (`-DCNA_GRAPHICS_RENDERER=<X>`) remains the default and recommended
mode, and is unchanged in every respect.

A second, opt-in mode compiles several renderers into one binary and chooses between them at
runtime through `CNA::GraphicsRendererSelection`:

```bash
cmake -S . -B cmake-build-multi -G Ninja \
      -DCNA_GRAPHICS_RENDERER=HEADLESS \
      -DCNA_GRAPHICS_RENDERERS="HEADLESS;SOFTWARE;STUB"
```

`CNA_GRAPHICS_RENDERER` keeps its meaning as the **default** renderer and must be a member of the
list. Only the default's `CNA_RENDERER_<X>` macro is defined project-wide; each family's own macro
is private to its target, which is what keeps the existing renderer-gated tests and examples
meaningful — they describe the default.

Unbuildable combinations are rejected at configure time with a reason
(`cmake/RendererCombinations.cmake`). See `docs/runtime-renderer-selection.md` and
`plan_runtimerenderer.md`.

`cmake-build-multi/` is the one addition to the build-directory list above; it is not a per-ticket
directory and is shared by all multi-renderer work.

### Build locations & caching (mandatory)

- **Always build into the stable in-repo `cmake-build-<variant>/` directories** (one per renderer
  variant). They survive across sessions and allow **incremental** rebuilds — never do a full rebuild
  when a handful of files changed.
- **Never build in the session scratchpad / a system temp dir.** Scratchpad is only for throwaway
  scripts and backups; build artifacts there are discarded, forcing wasteful full rebuilds.
- **Use `ccache`** when available (`CNA_USE_CCACHE=ON`; pass
  `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` on a fresh configure if the cache shows it NOTFOUND).
- **bgfx:** do not re-clone. Prefer a reusable `~/deps/bgfx`; the bgfx `shaderc` tool (needed to
  regenerate `bgfx_shaders.hpp`) can also be built from `cmake-build-bgfx/_deps/bgfx_cmake-src/`.
- Cap build parallelism at `-j4` in this sandbox.

---

## Existence-Gate Spikes — Persistent Directories Too

Standalone existence-gate spike programs (throwaway-looking probes proving a new renderer's
underlying API works before any renderer code is written — see `DX1-0`, `DX2-0`, `D9-0`) follow
the same build-location rule as CMake builds (see *Build locations & caching* above): write and
compile them in a `<name>-spike/` directory at the repo root (see `dx9-spike/README.md` for the
precedent), never in the session scratchpad. Once a spike's finding is settled, keep its `.cpp`
source and a short `README.md` of what it proved committed there; gitignore only the built
binaries (`*.exe`, `*.o`) inside that directory. `ccache` works for MinGW cross-compiles too
(`ccache x86_64-w64-mingw32-g++ …`), same as the native launcher.

---

## Git Commits — Always Commit After Finishing a Task

**Always create a commit as soon as a task is finished** (build verified, tests passing,
plan/`AUDIT.md`/`NEXT.md` updated) — do not wait for an explicit "commit" request for each
individual task. Do not push unless the user explicitly asks to push.

- Stage only the files that belong to the completed task, by explicit name
  (`git add <file> <file> ...`). Never use `git add -A`/`git add .` — this repo routinely has
  unrelated pre-existing local changes (e.g. an untracked vendor directory, an unrelated
  deleted config file) that must not be swept into an unrelated commit.
- Write the commit message referencing the task ID from the relevant plan file (e.g.
  `fix(Task P3-4): ...`), summarizing what changed and why, matching the detail level of
  this project's existing commit history (`git log --oneline`).
- One task = one commit. Do not bundle multiple unrelated tasks into a single commit.

---

## Internal (CNA) vs XNA Layer

| Layer                     | Location                                                        | Purpose                        |
|---------------------------|-----------------------------------------------------------------|--------------------------------|
| XNA public API            | `modules/<module>/include/Microsoft/Xna/Framework/…`            | Game-facing, must match XNA    |
| Renderer contracts         | `modules/graphics/include/CNA/Internal/Renderers/Common/…`       | `IGraphicsRenderer` etc.        |
| Renderer implementations   | `modules/renderers/<family>/{src,include}/…`                    | Hidden from XNA API            |
| CNA utilities/extensions  | `modules/core/include/CNA/…`, `modules/*-ext/…`                 | CNAEXT helpers, logging, etc.   |

## Platform Boundary

Platform, renderer and audio selection are three independent CMake axes:

- `CNA_PLATFORM` selects windowing, events, input and host services (`SDL3`, `HEADLESS`, or
  `TERMINAL`; SDL2/SDL12 are reserved but not implemented).
- `CNA_GRAPHICS_RENDERER` selects the renderer.
- `CNA_AUDIO_PLATFORM` selects playback/capture (`SDL3` or `NULL`).

New production code must use `CNA::Platform::IPlatform` and its narrow services. Do **not** include
SDL or call an `SDL_*`/`MIX_*` function outside these intentional native edges:

- `modules/platform/src/Sdl3/`;
- `modules/audio/src/Platform/Sdl3/` and the mixer implementation isolated inside audio;
- renderer families `sdl-renderer`, `sdl-gpu`, `fna3d`, and `freedirect`.

A genuinely SDL3-specific test belongs with the SDL3 platform implementation. Consumer tests use
canned platform services or the parameterized conformance suite, not native event injection.
Capabilities are promises: unsupported behavior refuses explicitly, and a service is non-null
exactly when its presence capability is true. Poll events and update input once per frame; never
put platform calls in a draw/audio/input inner loop.

Run the boundary gates after relevant changes:

```bash
python3 tools/platform/sdl_inventory.py --check
python3 tools/platform/sdl_classify.py --check
python3 tools/platform/renderer_sdl_audit.py --check
python3 tools/platform/sdl_ratchet.py --check
python3 tools/platform/hot_path_lint.py
```

See `docs/platform-abstraction.md` for the contract and implementation checklist. The migration
task/evidence log is `plan_platform.md`.

Renderer selection is compile-time via `CNA_GRAPHICS_RENDERER` CMake option
(`SDL_RENDERER` | `OPENGLES2` | `OPENGLES3` | `OPENGL33` | `WEBGL1` | `WEBGL2` | `BGFX` | `VULKAN` | `WEBGPU` |
`MAGNUM` | `HEADLESS` | `SOFTWARE` | `STUB` | `DIRECTX11` | `DIRECTX12` | `DIRECT2D` | `CANVAS` |
`HTML_DOM` | `SKIA` | `FREEDIRECT` | `DIRECTX9` | `DIRECTX1` | `DIRECTX2` | `DIRECTX3` | `DIRECTX5` | `DIRECTX6` |
`DIRECTX7` | `DIRECTX8` | `DIRECTX10` | `SDL_GPU` | `OPENGLES1` | `OPENGL4` | `OPENGL1` | `OPENGL2` |
`WICKED` | `SOKOL` | `DILIGENT` | `GLIDE` | `GDI` | `LLGL` | `METAL` | `BLEND2D` | `FNA3D` |
`SVG_DOM` | `OPENVG` | `PORTABLEGL` | `TINYGL` | `IGL` | `PIXIJS`). These are exactly 49
public identities; EasyGL remains an internal implementation shared by five GL profiles. The former
`ASCII` renderer identity was removed in favor of a renderer-neutral post-process effect,
`CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`) -- see `docs/ascii-post-process-effect.md`.
`WEBGPU`
is experimental and has a functional native
2D baseline, not yet the 3D/effect parity of the established GPU renderers.
`MAGNUM` is a desktop-OpenGL renderer built on mosra/magnum -- see `docs/magnum-renderer.md` and
`plan_magnum.md` for its own capability boundary.
`DILIGENT` is experimental too, and is the one renderer whose native API is chosen at **runtime**
(DiligentCore is itself an abstraction over D3D11/D3D12/Vulkan/OpenGL/Metal) — see
`plan_diligent.md` and `docs/diligent-renderer.md`.
`TINYGL` is the fixed-function CPU OpenGL renderer (C-Chads/tinygl) -- the fixed-function
counterpart to `PORTABLEGL`'s shader-era CPU OpenGL. Its transparency is a 1-bit colour-key cutout,
not alpha blending, and it has no stencil, scissor, render targets or shaders of any kind; see
`docs/tinygl-renderer.md` and `plan_tinygl.md` for the full boundary.
`IGL` (facebook/igl) is the second portable-abstraction identity after `LLGL`: it drives IGL's own
OpenGL (GLX) or Vulkan backend, fixed for the process by `CNA_IGL_BACKEND` because the platform
window's render intent must be decided before the renderer exists -- see `plan_igl.md` and
`docs/igl-renderer.md`.
`SKIA` is a separate experimental CPU-raster 2D renderer backed by a pinned external Skia artifact;
it does not delegate rendering to EasyGL and does not advertise 3D/depth/MSAA/MRT capabilities.
Use `plan_skia.md`, `NEXT_skia.md`, `docs/skia-renderer.md`, and
`docs/skia-developer-build.md` for that subsystem; do not reconstruct its state from the general
`NEXT.md`.
`PIXIJS` is the newest and most experimental renderer, Emscripten-only, rendering `SpriteBatch`
output through a pooled `PIXI.Sprite` scene graph (pixijs.com) rather than raw WebGL calls or
Canvas2D/DOM primitives. As of its initial authoring it has not been built or run on any real
Emscripten toolchain in any session -- see `plan_pixijs.md` and `docs/pixijs-renderer.md` for its
own honest status legend and capability boundary; do not describe it as verified or usable until
`plan_pixijs.md`'s own PIXIJS-84 (a real Emscripten toolchain build) actually happens.

---

## The CNAEXT Engine Layer (`CNA::Graphics`)

Everything above the XNA API — HDR pipeline, post-process passes, shadows, sky, image-based
lighting, materials, instancing/LOD/culling, compute — lives in `modules/graphics-ext/` under the
`CNA_CNAEXT` CMake option, which is **OFF by default**. With it off the layer does not exist: every
file in that module is wrapped in `#ifdef CNA_CNAEXT`, and a ctest (`CNAEXT_GuardDiscipline`)
enforces that. A game that does not opt in renders exactly what it rendered before.

Do not reconstruct this subsystem's state by reading its code:

- **`CNAEXT.md`** — the design (what the layer is, what it is not, why).
- **`plan_modern.md`** — the task backlog implementing it, `MOD-1`–`MOD-1924`, with every deviation
  and refusal recorded in the row itself rather than in a commit message.
- **`NEXT_modern.md`** — the running ledger: what is done, the decisions that did not survive
  contact, the full-suite baseline after each phase, and how to run the tests here
  (repo-root CWD, a real display, `Xvfb :99`).
- **`docs/cnaext-engine-layer.md`** — the capability boundary, per subsystem and per renderer.
- **`docs/cnaext-perf.md`** — every recorded measurement, with the recipe that produced it.

The build directory for this work is `cmake-build-cnaext/` (`-DCNA_CNAEXT=ON`). EasyGL is the
reference renderer; other renderers pick each subsystem up in `plan_modern.md` Phase 16, and until
they do they report `false` from the matching capability and take a documented fallback rather than
failing.

---

## WebGPU Is Active (Experimental)

The project owner explicitly lifted the former WebGPU prohibition on **2026-07-12** and authorized
its renderer implementation.

- WebGPU tasks live in **`plan_webgpu.md`** (`WEBGPU-1`–`WEBGPU-123`). Keep task statuses and
  limitations current as implementation proceeds.
- The native renderer uses pinned **wgpu-native v29.0.1.1**, selected with
  `-DCNA_GRAPHICS_RENDERER=WEBGPU`. Prefer `CNA_WEBGPU_ROOT` for reproducible/offline builds; the
  CMake integration may otherwise download the matching official binary package.
- The current baseline implements native surface/device setup, clear/present, Texture2D, buffer
  uploads and WGSL SpriteBatch. Do not describe it as Vulkan-level or full XNA 3D parity until the
  remaining shader, state, effect, render-target, readback and test tasks are actually complete.
- Preserve the established renderers: WebGPU changes should remain renderer-local or common only
  where a common-interface change is genuinely required and verified across existing renderers.

See `docs/webgpu-renderer.md` for the current capability boundary.

---

## System Dependencies (Linux)

The following system packages are required to build CNA on Debian/Ubuntu:

```bash
# FFmpeg — required for VideoPlayer (video decoding)
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev

# Note: libswscale-dev may not be available in some repos (runtime libswscale8 is enough).
# CNA implements YUV→RGBA conversion internally and does NOT depend on libswscale headers.

# Magnum — required only for CNA_GRAPHICS_RENDERER=MAGNUM (desktop OpenGL). Magnum itself is
# fetched/built by cmake/ThirdPartyMagnum.cmake; these are the system GL/X11 headers it links
# against. Add libegl1-mesa-dev as well when configuring with -DCNA_MAGNUM_USE_EGL=ON.
sudo apt-get install -y libgl1-mesa-dev libglx-dev libx11-dev

# Draco — optional, enables KHR_draco_mesh_compression decoding in GltfImportCore
# (plan_cnj.md CNB-91, Phase 14F). Detected via CMake's find_package(draco CONFIG); when absent,
# a Draco-compressed glTF primitive throws a clear "not supported" error at import time instead
# of failing to build. Not vendored (unlike cgltf.h/stb_image.h) — a real multi-file C++ library,
# not a single header.
sudo apt-get install -y libdraco-dev
```
