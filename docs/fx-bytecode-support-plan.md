# Plan: XNA compiled effect (`.fx`) bytecode support

> Historical design record. This document's original dependency and backend assumptions were
> superseded by [`plans/plan_fx.md`](../plans/plan_fx.md). The implemented FNA3D contract and current format/
> renderer matrix are documented in
> [`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md); do not use the milestones
> below as current implementation status.

Task 352 (`plans/plan_graphics.md`, Phase 41) decided CNA's policy for XNA/FNA compiled effect
bytecode — the binary blob XNA's Content Pipeline `EffectProcessor` produces from a `.fx` HLSL
source file, and the same blob a real XNA/FNA game embeds as a compiled `Effect` resource:
**full support**. This document records the research behind that decision and the phased
implementation plan (Phase 74) that carries it out. It is a planning document, not a status
report — every item below is currently unimplemented (⬜) except where noted.

## What FNA actually does (verified against local FNA source)

`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/Effect.cs` has almost no logic of
its own:

```csharp
public Effect(GraphicsDevice graphicsDevice, byte[] effectCode)
{
    GraphicsDevice = graphicsDevice;
    FNA3D.FNA3D_CreateEffect(graphicsDevice.GLDevice, effectCode, effectCode.Length,
        out glEffect, out effectData);
    this.effectData = effectData;
    INTERNAL_parseEffectStruct(effectData);
    CurrentTechnique = Techniques[0];
}

protected Effect(Effect cloneSource)
{
    GraphicsDevice = cloneSource.GraphicsDevice;
    FNA3D.FNA3D_CloneEffect(GraphicsDevice.GLDevice, cloneSource.glEffect,
        out glEffect, out effectData);
    ...
}
```

All real work — parsing the compiled-effect container, extracting parameter/technique/pass
reflection metadata, and translating the embedded Direct3D9 Shader Model 2/3 bytecode into a
GPU-runnable shader for the active renderer — happens inside **FNA3D**, which delegates to
**MojoShader** (`FNA3D_CreateEffect`/`FNA3D_CloneEffect` are thin wrappers around
`MOJOSHADER_compileEffect`/`MOJOSHADER_cloneEffect` in FNA3D's own driver code). FNA3D's local
checkout in this environment (`/rv/data/library/github.com/FNA-XNA/FNA3D`) is an uninitialized
git submodule (empty directory) — FNA3D's own Vulkan-driver source, including exactly how it
turns MojoShader's output into `VkShaderModule`s, was **not available to verify locally**; the
SPIR-V-hop description below is based on general knowledge of FNA3D's public architecture, not
a line-by-line source read, and should be re-verified once/if a real FNA3D checkout is available.

## What's available locally to build on

- **MojoShader's full C source is vendored and readable** at
  `/rv/data/library/github.com/u3d-community/U3D/Source/ThirdParty/MojoShader` (zlib-licensed,
  Ryan C. Gordon, compatible with CNA's MS-PL). Confirmed contents:
  - `mojoshader_effects.c` — parses exactly the XNA/D3D9 compiled-**effect** container format
    (techniques, passes, parameters, annotations, state blocks). This is the piece that removes
    the need to write a from-scratch container parser.
  - `mojoshader.c`/`mojoshader.h` — the Shader Model 2/3 bytecode-to-source transpiler. Supported
    *output* profiles (`MOJOSHADER_PROFILE_*` in `mojoshader.h`): `GLSL`, `GLSL120`, `ARB1`, `NV2`,
    `NV3`, `NV4`, plus pass-through `D3D`/`BYTECODE`. **No SPIR-V profile exists.** Getting a
    Vulkan-usable shader therefore needs a second hop: MojoShader → GLSL, then GLSL → SPIR-V via
    a separate compiler (see Vulkan task below).
  - No CMake target currently wires this source into either `cna_graphics` or `sharp-runtime` —
    it needs to be vendored properly (see Task 10200).
- **`glslang`** (Khronos's GLSL-to-SPIR-V compiler, needed for the Vulkan hop) is present on this
  machine only as build tooling inside the Android NDK (`~/Android/Sdk/ndk/*/sources/third_party/
  shaderc/third_party/glslang`) and a Flatpak SDK runtime — neither is a repo-vendorable source
  checkout usable from `cna_graphics`'s own CMake build today. This needs its own vendoring
  decision (submodule vs. system package vs. `FetchContent`), tracked separately (Task 10203).
- CNA's existing vendoring convention (`cmake/ThirdPartyENet.cmake`, `cmake/ThirdPartySDL.cmake`,
  `third_party/` + git submodules + a `THIRD_PARTY_NOTICES.md` entry) is the pattern to follow.

## Why this is a new phase, not a single task

Task 351's audit already established that CNA's `Effect` base class has **zero** bytecode
machinery — no constructor accepting bytecode at all (not even a throwing stub), because every
concrete stock effect builds its `Parameters`/`Techniques` by hand in C++. Adding real bytecode
support touches: a new vendored native dependency, a container-format parser, two independent
GPU-shader-translation paths (EasyGL/GLSL is close to free via MojoShader itself; Vulkan/SPIR-V
needs the extra glslang hop; Bgfx uses neither GLSL nor raw SPIR-V as its native shader format
and needs its own feasibility investigation before any implementation work is scheduled), the
`Effect`/`EffectPass`/`EffectParameter`/`EffectTechnique` wiring, `Clone()` (Task 883, opened by
Task 351), real test fixtures, and developer docs. That is the same shape and scale as the
WebGPU renderer (Phases 56–69), which is why it gets its own dedicated phase and task-number block
(`10200`+) rather than being squeezed into Phase 41 alongside Tasks 355–360's narrower
`EffectPass`/`EffectTechnique` verification work.

## Relationship to Tasks 353/354 (re-scoped by this task)

- **Task 353** no longer means "bytecode is permanently unsupported, so throw forever." It's
  re-scoped to an interim safety net: until Phase 74 lands, any bytecode-accepting constructor
  added to `Effect` must throw a clear, documented not-yet-implemented exception rather than
  silently producing a broken/fake effect. This is small and independently valuable — it can
  (and should) be done immediately, before Phase 74's real implementation exists.
- **Task 354**'s developer-facing doc now needs to explain three things instead of two: what's
  supported today (`ShaderEffect`, hand-written GLSL/SPIR-V source), what throws in the meantime
  (Task 353's guard), and what's planned (this document / Phase 74).

## Phase 74 task breakdown

See `plans/plan_graphics.md` Phase 74 for the authoritative, tracked task list (Tasks 10200–10208).
Summary:

1. Vendor MojoShader as a `third_party/mojoshader` submodule + `cmake/ThirdPartyMojoShader.cmake`
   + `THIRD_PARTY_NOTICES.md` entry.
2. Build a minimal C++ wrapper around `mojoshader_effects.c`'s parsing API: given a compiled
   effect byte blob, produce CNA-native technique/pass/parameter reflection data.
3. EasyGL path: MojoShader `GLSL`/`GLSL120` profile output compiled directly via EasyGL's
   existing shader-program creation path.
4. Vulkan path investigation + vendoring: decide and vendor a GLSL→SPIR-V compiler (glslang is
   the obvious default; investigate whether a lighter-weight alternative already fits CNA's build
   better) before the translation task itself is scheduled.
5. Vulkan path implementation: MojoShader GLSL output → SPIR-V via the Task 4 compiler → Vulkan
   pipeline creation.
6. Bgfx feasibility investigation (no implementation yet): bgfx's native shader format comes from
   its own `shaderc`/binary-shader pipeline, not raw GLSL or SPIR-V source — determine whether
   bgfx's runtime APIs can consume MojoShader/glslang output at all before committing to an
   approach.
7. Wire parsed reflection data + compiled per-renderer programs into `Effect`, add the real
   `Effect(GraphicsDevice*, bytecs bytecode[])` constructor and `Clone()` (folding in Task 883's
   already-identified `EffectPass::owner_` aliasing hazard).
8. Test fixtures: since this project has no XNA Content Pipeline tooling to compile a real `.fx`
   file, source or hand-produce real compiled-effect bytecode blobs for tests (this is a real,
   distinct blocker, not incidental — tracked as its own task rather than assumed solvable inline).
9. Developer documentation covering the full feature once implemented.

Each task's exact wording and scope lives in `plans/plan_graphics.md` — this document exists to carry
the reasoning that produced that list, not to duplicate it verbatim.
