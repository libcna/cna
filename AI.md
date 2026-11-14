# CNA — Instructions for AI Agents

`CLAUDE.md` and `AGENTS.md` are symbolic links to this file, so Claude Code, Codex and every other
agent read the same rules. Edit only `AI.md`.

## Project

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`)
on a CNA-owned platform abstraction (SDL3 by default) and a pluggable graphics renderer layer. It is
a framework/runtime, not a game: XNA-style public APIs over modern C++23 internals.

Where things are recorded:

- current defects and limitations: `KNOWN_ISSUES.md`;
- subsystem behaviour: that subsystem's page under `docs/` (index: `docs/README.md`);
- deliberate divergences from FNA and XNA: `docs/xna-fna-behavior-decisions.md`;
- accepted C++ language-mapping deviations: `CHECKLIST.md`;
- finished work: Git history.

### Source Reference

The working API and behaviour reference is the FNA source tree (<https://github.com/FNA-XNA/FNA>),
read from a local checkout outside this repository; clone it once if none exists. Old CNA code and
AI-generated stubs are not authoritative where they conflict with it.

### Where FNA and XNA disagree, XNA wins

FNA is a reimplementation, not the specification. Where measured Microsoft XNA 4.0 behaviour (read
from the shipped assemblies' IL; XNA Game Studio 4.0 Refresh when two XNA builds disagree) differs
from FNA, **CNA follows XNA**. Matching FNA is no defence for a behaviour XNA does not have.

Record each such decision where it is made: a source comment naming the XNA IL that settles it, a
row in `docs/xna-fna-behavior-decisions.md`, and a regression test.

---

## Language bindings

CNA reaches other languages through its native C API/C ABI (`modules/c-api`; architecture in
`docs/binding.md`, contract in `docs/c-api/README.md`). That C API is the approved boundary; plain C
consumers need no further authorization.

A language-specific binding needs the project owner's explicit authorization. Authorized today:

- **C#/.NET**: `cna-dotnet` (CNA.NET) with `cna-dotnet-samples` and `cna-dotnet-template`;
- **`cna-multi-language-3d-demo`**: its existing C++, C and C# versions, maintenance only.

Do not plan or implement a binding for any other language (JavaScript/TypeScript, Rust, Python,
Java, Zig, Go, Swift, …) without that authorization. Documentation about an existing consumer is not
authorization to start another binding.

---

## XNA API rules

### XNA 4.0 compliance

In the `Microsoft::Xna` namespace:

- **MUST** follow the XNA 4.0 API as implemented in FNA (XNA wins where they differ, see above);
- **MUST** keep XNA 4.0 class names, method signatures and behaviour;
- **MUST** use modern C++23 internally behind the XNA-style public API;
- **MUST** mark anything that is **not** XNA 4.0 API with the `CNAEXT` macro, an empty marker
  defined in `modules/core/include/CNA/CNAHelper.hpp`.

### Namespaces

Original XNA types stay in their XNA namespace (`Microsoft::Xna::Framework::Color`,
`Microsoft::Xna::Framework::Graphics::Texture2D`, …). The `CNA` namespace is only for
project-specific extensions, helpers, internal renderers and other non-XNA additions; never move an
XNA type into it.

### Names

Class, struct, enum, method, operator and constant/static member names match XNA/FNA exactly. Do not
rename a member to look more C++-like.

### C# properties

```cpp
// C#: public byte R { get; set; }
[[nodiscard]] bytecs getRProperty() const;
void setRProperty(bytecs value);
```

Do not replace properties with public fields unless the type already uses that style.

### Type aliases (sharp-runtime)

Where the C# source uses a .NET primitive type, use the sharp-runtime alias from
`"SharpRuntime/SharpRuntimeHelper.hpp"`, never a raw C++ type in the XNA API surface:

| C#       | Alias                 | C++         |
|----------|-----------------------|-------------|
| `byte`   | `bytecs` / `Byte`     | `uint8_t`   |
| `sbyte`  | `sbytecs` / `SByte`   | `int8_t`    |
| `short`  | `shortcs` / `Int16`   | `int16_t`   |
| `ushort` | `ushortcs` / `UInt16` | `uint16_t`  |
| `int`    | `intcs` / `Int32`     | `int32_t`   |
| `uint`   | `uintcs` / `UInt32`   | `uint32_t`  |
| `long`   | `longcs` / `Int64`    | `int64_t`   |
| `ulong`  | `ulongcs` / `UInt64`  | `uint64_t`  |
| `float`  | `Single`              | `float`     |
| `string` | `String`              | `std::string` |
| `char`   | `charcs`              | `char16_t`  |

A missing alias is added to sharp-runtime first (see below).

### Events

C# events are `System::EventHandler<TEventArgs>`; do not invent another mechanism:

```cpp
System::EventHandler<ExitingEventArgs> Exiting;                       // public event EventHandler<T> Exiting;
Exiting += [](System::Object* sender, const ExitingEventArgs& e) { … };
Exiting.Raise(this, args);                                            // or Exiting.Invoke(this, args)
```

### Interfaces and inheritance

Preserve C# interface relationships as C++ abstract base classes:

```cpp
// C#: class Color : IEquatable<Color>, IPackedVector, IPackedVector<uint>
struct Color : public Graphics::PackedVector::IPackedVectorT<UInt32> { … };
```

Where an exact mapping is not practical, implement equivalent behaviour and explain that API-shape
deviation in the commit message, not in a source comment.

### IDisposable

```cpp
class Foo : public System::IDisposable {
public:
    void Dispose() override;
protected:
    void Dispose(bool disposing);   // only when the pattern requires it
};
```

Check `isDisposed_` before acting. Where XNA refuses use after disposal, throw
`System::ObjectDisposedException`.

### Visibility Mapping

| C#         | C++                                                                  |
|------------|----------------------------------------------------------------------|
| `public`   | `public`                                                             |
| `internal` | `private`, `protected`, a detail/internal namespace, or omitted      |
| `private`  | `private`                                                            |

Do not make every member public; C# `internal DebugDisplayString` is not a public C++ method.

### Static members and named constants

```cpp
// C#: public static readonly Color CornflowerBlue = new Color(…);
static const Color CornflowerBlue;          // .hpp
const Color Color::CornflowerBlue{…};       // .cpp
```

### Behaviour fidelity

Match XNA behaviour over C++ preference: packed layouts (e.g. `Color` is AABBGGRR), clamping,
integer casts, operators, overloads, default values and exceptions. XNA-facing argument and state
checks throw the matching sharp-runtime `System::*Exception` (`ArgumentOutOfRangeException`,
`ArgumentException`/`ArgumentNullException`, `ObjectDisposedException`, `NotSupportedException`,
`InvalidOperationException`); raw `std::` exceptions are only for genuinely internal failures.

Every intentional deviation from FNA logic carries a `//` comment explaining why. Do not redesign
behaviour for cleaner C++.

### Member order

Keep C++ member order close to the C# source order where practical; it keeps reviews diffable.

### No backward-compatibility hacks

Do not add old CNA aliases or shortcuts to keep outdated demos compiling: `Color::CornflowerBlue`,
never a free `inline const Color CornflowerBlue(…)`. If old game code breaks after the API is
corrected, fix the game code separately.

---

## File Structure

A module-oriented monorepo (`docs/physical-modules.md`): every subsystem and every renderer family
owns `modules/<name>/{CMakeLists.txt,include/,src/,tests/,examples/}` (`examples/` only where it has
examples).

- **Headers** (declarations and public documentation) go under the owning module's `include/`, whose
  root reproduces the public namespace path, so the include spelling is stable API:
  `modules/graphics/include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp` is included as
  `#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"`.
- **Implementation** goes under the module's `src/`: XNA API in `src/Xna/`, `CNA::Internal` parts in
  `src/Internal/`, CNAEXT surfaces in `src/CnaExt/`; single-area modules stay flat. Renderer
  implementations live in `modules/renderers/<family>/src/`.
- No non-template implementation in headers.

A production `.cpp` outside every module directory fails the configure (the source-partition
validator in `modules/CMakeLists.txt`).

| Layer                    | Location                                                   | Purpose                      |
|--------------------------|------------------------------------------------------------|------------------------------|
| XNA public API           | `modules/<module>/include/Microsoft/Xna/Framework/…`       | Game-facing, must match XNA  |
| Renderer contracts       | `modules/graphics/include/CNA/Internal/Renderers/Common/…` | `IGraphicsRenderer` etc.     |
| Renderer implementations | `modules/renderers/<family>/{src,include}/…`               | Hidden from the XNA API      |
| CNA utilities/extensions | `modules/core/include/CNA/…`, `modules/*-ext/…`            | CNAEXT helpers, logging etc. |

---

## Comments and documentation

- Every public method, constructor, property getter/setter, operator and constant in every `.hpp`
  has a Doxygen block, `/** … */` with `@brief` plus `@param`/`@return` where applicable. A simple
  member with neither may use a one-line `/** @brief … */`.
- Never a bare `///` on a public API declaration; `///` is acceptable only for brief notes inside
  method bodies.
- Carry the intent of FNA's public XML doc comments (`<summary>`, `<param>`, `<returns>`) into
  Doxygen; rephrase where that is clearer.
- Never write "taken from FNA", "copied from FNA" or "based on FNA source".
- Inside implementations, comment only a non-obvious why.

---

## sharp-runtime and missing dependencies

`sharp-runtime` is CNA's C++ reimplementation of the .NET runtime (`System.*` and the primitive
aliases). Anything .NET that CNA needs and sharp-runtime lacks (an alias, `System::Object`,
`System::IDisposable`, an exception type, `System::TimeSpan`, `System::IO::Stream`,
`System::Collections::*`, …) is added to sharp-runtime first, as a minimal stub with its final name
and namespace, and then used from CNA. Never inline a .NET concept into a CNA header as a raw C++
type or an ad-hoc workaround.

A missing non-.NET type gets a minimal, correctly named stub in its final XNA/FNA namespace, enough
to compile, never a large unrelated system. Report every stub you add.

---

## Porting an FNA file

A ported `.cs` file is finished in one pass, never partially. The full checklist and the table of
accepted C++ deviations are in `CHECKLIST.md`; the minimum:

- `// SPDX-License-Identifier: MS-PL` at the top of both the `.hpp` and the `.cpp`;
- `#include "CNA/CNAHelper.hpp"` in the `.hpp` when it uses `CNAEXT`;
- every method body verified line by line against FNA, and against XNA where they differ;
- every intentional deviation from FNA logic explained by a `//` comment;
- every concrete `System::Object` subclass overrides
  `CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;`, returning the
  fully-qualified .NET name (e.g. `"Microsoft.Xna.Framework.Game"`);
- tests as below.

---

## Tests

Every public XNA 4.0 method, constructor, operator and constant has at least one Google Test. Tests
live in the owning module's `modules/<name>/tests/`, mirroring the namespace path; shared fixtures
and cross-module link probes stay under the top-level `tests/`.

- Every overload has a test case; out-ref overloads (e.g.
  `void Contains(const BoundingBox&, ContainmentType&)`) are tested separately from the
  value-returning ones.
- Each static factory (`CreateFromPoints`, `CreateMerged`, `CreateFromSphere`, …) has its own test.
- `==`, `!=` and `Equals()` are tested for equal and unequal cases, `ToString()` for its format,
  `GetHashCode()` for consistency (equal objects, equal hashes).
- A new method gets its tests in the same task; tests that existed before do not count.
- An API is not complete until its tests are. API presence is the generated
  `docs/xna-4-runtime-member-coverage.md`
  (`python3 tools/audit_xna_runtime_surface.py --write-reports`).

---

## Building

### Shared-machine rules

On the owner's machine many agents share one SSD, 16 cores and 30 GB of RAM without swap. The parent
directory's `CLAUDE.md`/`AGENTS.md` (outside this repository) hold the full rules and their
reasons; where they differ from this file, this file wins inside CNA. In short:

- one shared ccache: `CCACHE_DIR=/rv/cnaccache`, `CCACHE_BASEDIR=/rv`; never point `CCACHE_DIR`
  anywhere else, never export `CCACHE_MAXSIZE` or `CCACHE_HASHDIR`;
- never configure, compile or `git clone` under `/tmp` or in the session scratchpad;
- never create a build directory named after a ticket, date, branch or topic (see the list below);
- take third-party dependencies from shared checkouts in `~/deps/<name>`, with
  `FETCHCONTENT_UPDATES_DISCONNECTED=ON`;
- build only the sanitizer variant the task needs, delete probe binaries once their results are
  written down, and never delete a build tree another session may still be using.

### Build directories

Reuse the stable in-repo trees; they survive sessions and rebuild incrementally:

| Directory                 | Configuration                                                              |
|---------------------------|----------------------------------------------------------------------------|
| `cmake-build-debug/`      | default Debug build                                                        |
| `build/`                  | default Release build                                                      |
| `cmake-build-<renderer>/` | one per default renderer: `-sdlgpu`, `-webgpu`, `-software`, `-headless`, `-opengl33`, `-fna3d` |
| `cmake-build-multi/`      | runtime renderer selection (below)                                         |
| `cmake-build-mingw/`      | MinGW cross build, `DIRECTX9` default                                      |
| `build-probe/`            | throwaway probes of every ticket, separated by file-name prefix            |

Create a new `cmake-build-<renderer>/` only for a renderer that has none. Reconfigure only when a
tree is broken or its configuration genuinely changed, never as a habit.

### ccache and parallel jobs

`CNA_USE_CCACHE=ON` is the default; on a fresh configure also pass
`-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache`. If ccache is missing, say
so instead of silently building without it. It works for MinGW cross-compiles too
(`ccache x86_64-w64-mingw32-g++ …`).

Compile with **at most 6 cores** per session in total, helper lanes included (owner, 2026-11-12):
`cmake --build <dir> --parallel 6`, `ninja -j6`, `make -j6`, if needed under `taskset -c 0-5`.
Never `-j$(nproc)` or a bare `--parallel`. Sanitizer and template-heavy translation units can each
need several GB, so lower the job count for a target that runs short of memory, and do not run
several heavy jobs at once.

### Runtime renderer selection (second build mode)

Compile-time selection (`-DCNA_GRAPHICS_RENDERER=<X>`) is the default and recommended mode. The
opt-in second mode builds several renderers into one binary and chooses at run time through
`CNA::GraphicsRendererSelection`:

```bash
cmake -S . -B cmake-build-multi -G Ninja \
      -DCNA_GRAPHICS_RENDERER=HEADLESS \
      -DCNA_GRAPHICS_RENDERERS="HEADLESS;SOFTWARE;STUB"
```

`CNA_GRAPHICS_RENDERER` stays the **default** and must be in the list. Only the default's
`CNA_RENDERER_<X>` macro is defined project-wide; each family's own macro is private to its target,
so renderer-gated tests and examples describe the default. Unbuildable combinations are refused at
configure time (`cmake/RendererCombinations.cmake`); the design decisions are in
`docs/runtime-renderer-selection.md`.

### Existence-Gate Spikes

A standalone probe proving a new renderer's underlying API works, before any renderer code exists,
lives in `spikes/<name>-spike/` (precedent: `spikes/dx9-spike/README.md`), never in the scratchpad.
While the renderer is a candidate or supported, commit the probe source and a short `README.md` of
what it proved, and gitignore only the built binaries (`*.exe`, `*.o`). When the renderer is retired
or rejected, delete the spike directory and its ignore rules; Git history keeps the source.

---

## Running GPU/window tests

**Never run tests, examples, demos or oracles on the owner's live desktop** (`DISPLAY=:0`,
`WAYLAND_DISPLAY=wayland-0`). Use the private runner:

```bash
tools/platform/run_gpu_tests_private.sh <build-dir> [ctest arguments...]
# e.g. tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu -R '^SdlGpu_' -j6
```

It starts a headless Weston with a private rootful Xwayland (DRI3 on the real GPU; Xvfb cannot
present Vulkan, which SDLGPU uses on Linux) and runs `ctest` there, so nothing appears on the
desktop. `CNA_TEST_DISPLAY` is empty by default, so registered tests inherit the launcher's display;
the live desktop would need an explicit `-DCNA_TEST_DISPLAY=:0 -DCNA_TEST_ALLOW_LIVE_DISPLAY=ON`
(`cmake/TestDisplayPolicy.cmake`), and the runner refuses a tree that forces a display.

---

## Platform boundary

Platform, renderer and audio are three independent CMake axes:

- `CNA_PLATFORM` selects windowing, events, input and host services: `SDL3`, `HEADLESS` or
  POSIX-only `TERMINAL`. **SDL3 is CNA's only graphical platform**: Windows, X11, Wayland, macOS,
  iOS, Android and the browser go through SDL's video drivers, and a renderer still receives the
  native `HWND`, `Display*` + XID or `wl_display*` + `wl_surface*` from
  `IPlatformWindow::GetNativeHandle()`. Never add a CNA-owned backend for a window system SDL3
  already drives; the old `WIN32`, `X11` and `WAYLAND` selectors are refused, naming SDL3.
- `CNA_ENABLE_SDL` (`AUTO` by default) decides whether the vendored SDL3 sub-build is configured.
  `OFF` skips it and refuses at configure time, by name, every platform/audio/renderer selection
  that needs SDL; nothing is silently substituted. An SDL-free build is windowless
  (`HEADLESS`/`TERMINAL`).
- `CNA_GRAPHICS_RENDERER` selects the renderer (below).
- `CNA_AUDIO_PLATFORM` selects playback/capture: `SDL3`, `NULL` or `ALSA`. `SOUND_ENABLED` means a
  mixer exists behind `MixerEngine.hpp`: SDL3_mixer for `SDL3`, CNA's own mixer
  (`modules/audio/src/Backend/CnaMixer/`) for `ALSA`, the SDL-free Linux backend
  (`docs/audio-alsa.md`). `NULL` has no mixer.

New production code uses `CNA::Platform::IPlatform` and its narrow services. SDL headers and
`SDL_*`/`MIX_*` calls are allowed only at these native edges:

- `modules/platform/src/Sdl3/`;
- `modules/audio/src/Platform/Sdl3/` and the mixer implementation isolated inside audio;
- the renderer families `sdl-gpu` and `fna3d`.

A genuinely backend-specific test belongs with the platform implementation it exercises; consumer
tests use canned platform services or the parameterized conformance suite, not native event
injection. Capabilities are promises: unsupported behaviour refuses explicitly, and a service is
non-null exactly when its presence capability is true. Poll events and update input once per frame;
never put platform calls in a draw, audio or input inner loop.

After relevant changes, run the boundary gates. The last one covers SDL use in tests and examples
and also runs at configure time, so a failure there breaks every configure:

```bash
python3 tools/platform/sdl_inventory.py --check
python3 tools/platform/sdl_classify.py --check
python3 tools/platform/renderer_sdl_audit.py --check
python3 tools/platform/sdl_ratchet.py --check
python3 tools/platform/hot_path_lint.py
python3 tools/platform/nonproduction_sdl_audit.py --check
```

The contract, its design decisions and the implementation checklist are in
`docs/platform-abstraction.md`.

---

## Renderers

`CNA_GRAPHICS_RENDERER` selects one of exactly **10 public identities** over 8 implementation
families: `OPENGLES3`, `OPENGL33`, `WEBGL2`, `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX9`,
`SDLGPU`, `FNA3D`. EasyGL is an internal implementation shared by the three GL profiles. `SDLGPU` is
the modern native path: CNA maps XNA 4.0 onto SDL's `SDL_GPU`, and SDL owns its Vulkan, Direct3D 12
and Metal backends. The canonical list is `cmake/RendererIdentities.cmake`, pinned by
`scripts/check_renderer_identities.py`.

- Say **renderer**, never backend, for a CNA graphics renderer (`GraphicsRenderer*`,
  `CNA_GRAPHICS_RENDERER*`, `cna_graphics_renderer_*`; see `docs/renderer-registry.md`). The
  identity check refuses the old graphics-backend identifiers.
- The renderer set is curated: a renderer is added only for meaningful platform coverage,
  compatibility value, architectural value or a capability the set lacks, never to grow the count.
- Retired identities (41, among them `VULKAN`, `DIRECTX11`, `METAL` and `SDL_RENDERER`; the list is
  `docs/removed-renderers.md`) are refused at configure time by name and with their permanently
  reserved C ABI value, and at run time as retired rather than unknown. Their numbers are never
  reassigned and no renderer aliases them. Never resurrect one without a fresh owner instruction.
- `GraphicsCapability::CustomEffects` means a renderer *accepts* an effect, not that it runs your
  shader source: SOFTWARE and HEADLESS keep rendering with their fixed path, and SDLGPU's
  `ShaderEffect` takes SPIR-V rather than GLSL. New shader-based code asks the detailed profile in
  `docs/renderer-capability-profiles.md`, not the coarse capability alone.

### WebGPU

`WEBGPU` is **supported, not production** (`GraphicsRendererMaturity::Supported` since 2026-10-17):
verified on Linux x86_64 and in headless Chrome only (`WEBGPU-1`). The native renderer uses pinned
**wgpu-native v29.0.1.1**; prefer `CNA_WEBGPU_ROOT` for reproducible offline builds, otherwise CMake
may download the matching official binary package.

`docs/webgpu-renderer.md` is the single source of truth. Its *Status* (with *Parity, stated
precisely*) and *Important limitations* sections say what is verified and what is open; keep them
current, and state parity only in their words, never by restating a feature list or a count here.
Keep WebGPU changes renderer-local unless a common-interface change is genuinely required and
verified across the other renderers.

### CNA graphics extensions (`CNA_CNAEXT`, `modules/graphics-ext/`)

`CNA_CNAEXT=ON` (**OFF by default**) builds a small standalone extension module:
`AsciiPostProcessEffect`, `CRTEffect`, `DepthEffect`, `DebugDraw` and the shader-package helpers they
use. With the option off the module does not exist. The exact public list and its capability
boundary are in `docs/cnaext-engine-layer.md`. The larger engine layer that once lived here (HDR,
shadows, sky, image-based lighting, materials, instancing/LOD, compute) was removed; do not restore
it.

There is no XNA name to preserve in this module, so: verbs are `lowerCamelCase`; properties are
`getX()`/`setX()`, or `isX()` for booleans; types and enum values stay `UpperCamelCase`.

---

## System dependencies (Debian/Ubuntu)

```bash
# FFmpeg: optional VideoPlayer and XACT WaveBank decoders, selected independently by
# CNA_ENABLE_VIDEO and CNA_ENABLE_WAVEBANK_CODECS (OFF/AUTO/ON, both AUTO by default).
# Either ON requires it; both OFF omit FFmpeg discovery and every FFmpeg link dependency.
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
# libswscale-dev is not needed: CNA converts YUV to RGBA itself.

# Desktop OpenGL and X11 headers: the desktop GL renderers (OPENGL33, OPENGLES3) and the vendored
# SDL3's x11 video driver. Its wayland driver also needs libwayland-dev, wayland-protocols,
# libxkbcommon-dev and libdecor-0-dev (cmake/ThirdPartySDL.cmake names any that are missing).
sudo apt-get install -y libgl1-mesa-dev libglx-dev libx11-dev

# Draco: KHR_draco_mesh_compression in GltfImportCore is built from the pinned third_party/draco
# submodule (CNA_ENABLE_DRACO, ON except under Emscripten), so no package is needed;
# -DCNA_ENABLE_DRACO=OFF builds without a decoder and a Draco-compressed primitive then throws at
# import time. The package serves only the explicit system mode:
sudo apt-get install -y libdraco-dev   # only with -DCNA_USE_SYSTEM_DRACO=ON
```

---

## Finishing a task

1. **Build** the affected targets. There is no `CNA` target: build the whole configuration
   (`cmake --build <dir> --parallel 6`) or a test/example target by its own name; for a small edit,
   only the named targets.
2. **Test** what changed (GPU and window tests only through the private runner) and run the gates
   the change touches.
3. **Record** a change of current state in `KNOWN_ISSUES.md` and changed subsystem behaviour in that
   subsystem's `docs/` page.
4. **Commit** as soon as the task is finished, without waiting to be asked. Never push unless the
   user asks.
   - Stage only the task's files, by name (`git add <file> …`). Never `git add -A` or `git add .`:
     the tree routinely holds unrelated local changes.
   - Write the message in this repository's style (`git log --oneline`):
     `type(TASK-ID): what changed and why`, e.g. `fix(RV-SFX-8): …`, `docs(KNOWN_ISSUES): …`.
   - One task, one commit.
5. **Report** the changed files, added stubs, missing dependencies, intentional deviations, the
   build result and any remaining errors.
