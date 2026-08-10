# `ShaderEffect` vs. compiled XNA `.fx` bytecode (Task 354)

This is a practical, developer-facing guide for anyone porting an XNA/FNA game (or writing a new
one) against CNA and wondering how custom shaders/effects work today. For the internal audit trail
behind these decisions, see `plan_graphics.md` Tasks 351–354 and `docs/fx-bytecode-support-plan.md`
(the Phase 74 implementation plan referenced below). For per-renderer pixel-test coverage of every
stock effect, see `docs/xna-4-api-coverage.md` §7.

## What works today

**1. The 6 stock XNA effects** — for standard XNA-style rendering, use these directly, exactly as
in XNA/FNA:

| Effect | What it's for |
|--------|----------------|
| `BasicEffect` | Vertex color / texture / lighting / fog — the default effect for most geometry. |
| `AlphaTestEffect` | Discards pixels based on a `CompareFunction` against a reference alpha. |
| `DualTextureEffect` | Blends two textures in a single draw (e.g. lightmaps). |
| `EnvironmentMapEffect` | Cubemap-based reflective/emissive surfaces. |
| `SkinnedEffect` | Bone-weighted vertex skinning (stride-52 vertices, up to per-vertex bone blending). |
| `SpriteEffect` | The default effect `SpriteBatch` uses internally. |

All 6 are implemented natively in C++ (not translated from bytecode) and are pixel-tested on
EasyGL; see `docs/xna-4-api-coverage.md` §7 for exact per-renderer/per-effect status (Vulkan and
Bgfx support varies by effect).

**2. `ShaderEffect`** (`Microsoft::Xna::Framework::Graphics::ShaderEffect`, `CNAEXT` — a CNA
extension, not part of the XNA 4.0 API) — for custom shaders, construct it directly from
hand-written GPU shader source:

```cpp
// EasyGL: GLSL ES 3.0 source strings
ShaderEffect fx(device, vertexGlslSource, fragmentGlslSource);

// Vulkan: pre-compiled SPIR-V, passed as raw bytes packed into std::string
std::string vertSpv(reinterpret_cast<const char*>(vertSpirvBytes), vertSpirvSize);
std::string fragSpv(reinterpret_cast<const char*>(fragSpirvBytes), fragSpirvSize);
ShaderEffect fx(device, vertSpv, fragSpv);
```

(See `examples/easygl_shader_effect_test.cpp` and `examples/vulkan_shader_effect_test.cpp` for
full working examples.) **Bgfx's `ShaderEffect` renderer is currently a no-op stub** — both source
strings are accepted but ignored, per `docs/xna-4-api-coverage.md` §7 — so this path is EasyGL/
Vulkan only today.

`ShaderEffect` also exposes (Task 946, EasyGL only so far):

```cpp
fx.Apply();  // bind the program before setting any uniform, so the write reaches the right one
fx.SetUniformFloatArray("uWeights", weights, count);   // uniform float uWeights[count];
fx.SetUniformVec2Array("uOffsets", offsets, count);    // uniform vec2 uOffsets[count]; (offsets holds count*2 floats)
fx.SetTexture(1, someTexture2D);                        // binds sampler unit 1 (unit 0 is normally
                                                          // driven by the caller, e.g. SpriteBatch's
                                                          // own texture parameter)
```

Use `SetTexture()` for any shader that samples more than one texture in a single draw (matching
real XNA's `GraphicsDevice.Textures[unit] = someTexture`) — see
`examples/easygl_bloom_combine_test.cpp` for a worked example (two independent flat-color
textures combined via a ported `BloomCombine.fx`).

**Loading via `ContentManager`**: `Content.Load<std::shared_ptr<Effect>>("name")` reads a
`name.shader.json` descriptor (`{"vertex": "...", "fragment": "..."}`, paths relative to the
content root) and constructs a `ShaderEffect` from the referenced GLSL files — see
`examples/easygl_bloom_extract_test.cpp` for a full round-trip example.

## What doesn't work yet: loading a real compiled `.fx` file

A real XNA/FNA game's content build produces a compiled **effect bytecode** blob (the XNA Content
Pipeline's `EffectProcessor` output) — a binary container holding parameter/technique/pass
reflection metadata plus embedded Direct3D9 Shader Model 2/3 bytecode. FNA loads this via
`Effect(GraphicsDevice, byte[] effectCode)`, which hands the whole blob to MojoShader (through
FNA3D) for parsing and GPU-shader translation.

CNA has the matching constructor signature —
`Effect(GraphicsDevice&, const std::vector<SharpRuntime::bytecs>& effectCode)` — but it does not
yet parse or translate that bytecode. Calling it throws immediately:

```
System::NotImplementedException:
Effect(GraphicsDevice&, const std::vector<bytecs>&): compiled XNA .fx bytecode is not yet
supported (tracked as Phase 74, see docs/fx-bytecode-support-plan.md). Use a hand-authored
ShaderEffect or one of the built-in stock effects instead.
```

This is a deliberate, honest interim guard (Task 353) — CNA never silently produces a broken or
fake effect from a bytecode blob it can't actually translate.

## Practical guidance if you're porting a game today

If your game ships a compiled `.fx` effect, the current recommended path is: **hand-port the
original HLSL shader source to GLSL (EasyGL) or SPIR-V (Vulkan) and use `ShaderEffect` directly.**
The compiled binary itself isn't loadable yet, but the original `.fx`/HLSL source is usually
available (it's what the content build compiled from), and hand-porting a handful of custom
shaders is almost always a smaller, more tractable task than waiting on full bytecode support —
especially since most XNA games' custom effects are fairly small (a handful of techniques/passes
around one of the standard vertex layouts).

## Roadmap: Phase 74 — full compiled-bytecode support

The long-term policy decision (Task 352) is **full support** for real compiled `.fx` bytecode,
not a permanent throw. This is real, currently-unimplemented, multi-phase work (Tasks 10200–10208,
`plan_graphics.md` Phase 74), summarized here — see `docs/fx-bytecode-support-plan.md` for the
full reasoning:

1. Vendor MojoShader (its zlib-licensed C source is already available locally and reads XNA's
   exact compiled-effect container format — no from-scratch parser needed).
2. Wrap MojoShader's effect-parsing API to produce CNA-native technique/pass/parameter reflection
   data from a raw bytecode blob.
3. EasyGL path: MojoShader can transpile the embedded Shader Model 2/3 bytecode directly to GLSL.
4. Vulkan path: MojoShader has no SPIR-V output profile, so this needs a second hop — GLSL through
   a GLSL→SPIR-V compiler (glslang is the leading candidate; vendoring it is its own tracked task).
5. Bgfx path: needs its own feasibility investigation first — bgfx's native shader pipeline isn't
   raw GLSL or SPIR-V source, so there's no default path.
6. Wire the parsed metadata + compiled per-renderer programs into `Effect`, replacing Task 353's
   throwing constructor with a real one, and implement `Clone()` (Task 883).
7. Real test fixtures (this project has no XNA Content Pipeline tooling to produce fresh compiled
   bytecode, so sourcing/hand-producing real test blobs is its own tracked step).
8. Full developer documentation once the feature actually works.

Until Phase 74 lands, this document — not `docs/fx-bytecode-support-plan.md` — is the one to point
game developers at: it describes what's usable *right now*.
