# CNA Project Guidelines — FNA to C++ Porting Instructions

## Project Overview

**CNA** is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics backend
layer. It is a framework/runtime and abstraction layer — not a game — designed to preserve XNA-style APIs
(`Microsoft::Xna::Framework`) while using modern C++23 internals.

### Source Reference

The authoritative behavioral and API reference is the local FNA source tree:

```text
/rv/data/library/github.com/FNA-XNA/FNA
```

Do **not** treat old CNA code or AI-generated stubs as authoritative if they conflict with the FNA reference API.

### Binding Analysis Sources

The following project-design documents are **read-only source material**. Do not edit, reformat,
move, or delete them. Derive C-binding decisions and planning artifacts from them instead:

```text
analysis_binding.md
analysis_binding_sharp_runtime.md
```

The current approved planning boundary is limited to CNA's native C API/C ABI. Do not create
plans or implementations for C#, .NET, JavaScript/TypeScript, Rust, Python, Java, Zig, Go,
Swift, or other language-specific bindings unless the project owner explicitly authorizes them.

---

## Code Generation Rules

### XNA 4.0 API Compliance

When implementing code in the `Microsoft::Xna` namespace:

- **MUST** strictly adhere to the XNA 4.0 API specification as implemented in FNA.
- **MUST** preserve original XNA 4.0 class names, method signatures, and behavior.
- **MUST** use modern C++23 internally while maintaining XNA-style public APIs.
- If implementing functionality that is **NOT** part of the XNA 4.0 API within the `Microsoft::Xna` namespace,
  you **MUST** wrap it with the `NOXNA` macro.

`NOXNA` is defined in `include/CNA/CNAHelper.hpp` as an empty marker macro used to visually tag non-XNA extensions.

---

## Namespaces

Original XNA types must stay in the matching XNA namespace:

```cpp
Microsoft::Xna::Framework::Color
Microsoft::Xna::Framework::Vector3
Microsoft::Xna::Framework::Graphics::Texture2D
Microsoft::Xna::Framework::Graphics::SpriteBatch
```

The `CNA` namespace is for project-specific extensions, helpers, internal backends, and non-XNA additions only.
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

- Declarations and public documentation: `.hpp` file under `include/`
- Implementation: `.cpp` file under `src/`

Directory structure mirrors the namespace:

```text
include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp
src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp
```

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
- `#include "CNA/CNAHelper.hpp"` in `.hpp` if `NOXNA` is used anywhere in that file.
- Every method body verified **line-by-line** against the FNA equivalent.
- Every intentional deviation from FNA logic documented with a `//` comment in the source.
- Concrete classes that inherit `System::Object` **must** override `GetTypeName()` with `NOXNA`:
  ```cpp
  NOXNA [[nodiscard]] const std::string& GetTypeName() const override;
  ```
  The return value is the fully-qualified .NET name, e.g. `"Microsoft.Xna.Framework.Game"`.
- Tests: every public method, operator, and constant covered. Out-ref overloads tested separately.
  See CHECKLIST.md for the complete test requirements.

The table of known acceptable C++ deviations from FNA/XNA (e.g. `GetHashCode()` returning `std::size_t`,
`ref`/`out` → value-ref pairs, null guards omitted for C++ references) is maintained in `CHECKLIST.md`.

---

## Tests

Every public XNA 4.0 API method, constructor, operator, and constant **must** have at least one unit test.
Tests live under `tests/` mirroring the namespace path, using Google Test.

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

| Layer                     | Location                                      | Purpose                        |
|---------------------------|-----------------------------------------------|--------------------------------|
| XNA public API            | `include/Microsoft/Xna/Framework/…`           | Game-facing, must match XNA    |
| Backend contracts         | `include/CNA/Internal/Backends/Common/…`      | `IGraphicsBackend` etc.        |
| Backend implementations   | `src/CNA/Internal/Backends/{SDL,EasyGL,Vulkan}` | Hidden from XNA API           |
| CNA utilities             | `include/CNA/`, `src/CNA/`                    | NOXNA helpers, logging, etc.   |

Backend selection is compile-time via `CNA_GRAPHICS_BACKEND` CMake option
(`SDL_RENDERER` | `EASYGL` | `VULKAN` | `BGFX` | `WEBGPU`). `WEBGPU` is experimental and has a
functional native 2D baseline, not yet the 3D/effect parity of the established GPU backends.

---

## WebGPU Is Active (Experimental)

The project owner explicitly lifted the former WebGPU prohibition on **2026-07-12** and authorized
implementation as CNA's fifth graphics backend.

- WebGPU tasks live in **`plan_webgpu.md`** (`WEBGPU-1`–`WEBGPU-123`). Keep task statuses and
  limitations current as implementation proceeds.
- The native backend uses pinned **wgpu-native v29.0.1.1**, selected with
  `-DCNA_GRAPHICS_BACKEND=WEBGPU`. Prefer `CNA_WEBGPU_ROOT` for reproducible/offline builds; the
  CMake integration may otherwise download the matching official binary package.
- The current baseline implements native surface/device setup, clear/present, Texture2D, buffer
  uploads and WGSL SpriteBatch. Do not describe it as Vulkan-level or full XNA 3D parity until the
  remaining shader, state, effect, render-target, readback and test tasks are actually complete.
- Preserve the established backends: WebGPU changes should remain backend-local or common only
  where a common-interface change is genuinely required and verified across existing backends.

See `docs/webgpu-backend.md` for the current capability boundary.

---

## System Dependencies (Linux)

The following system packages are required to build CNA on Debian/Ubuntu:

```bash
# FFmpeg — required for VideoPlayer (video decoding)
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev

# Note: libswscale-dev may not be available in some repos (runtime libswscale8 is enough).
# CNA implements YUV→RGBA conversion internally and does NOT depend on libswscale headers.

# Draco — optional, enables KHR_draco_mesh_compression decoding in GltfImportCore
# (plan_cnj.md CNB-91, Phase 14F). Detected via CMake's find_package(draco CONFIG); when absent,
# a Draco-compressed glTF primitive throws a clear "not supported" error at import time instead
# of failing to build. Not vendored (unlike cgltf.h/stb_image.h) — a real multi-file C++ library,
# not a single header.
sudo apt-get install -y libdraco-dev
```
