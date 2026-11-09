# `ShaderEffect` vs. compiled XNA `.fxb` bytecode

Status: reconciled with the public API, all eight renderer families and live capability queries on
**2026-10-31**.

CNA has two deliberately separate custom-shader contracts. Support for one never implies support
for the other.

| Contract | Public API | Input | Primary capability |
|---|---|---|---|
| Renderer-dialect shader | `Microsoft::Xna::Framework::Graphics::ShaderEffect` (a `CNAEXT` type) | An untyped vertex/fragment payload pair, or explicitly typed `ShaderCodeEXT`/`ShaderPackageEXT` variants when `CNA_CNAEXT=ON` | See the four `ShaderEffect` queries below |
| Compiled XNA Effect | `Effect(GraphicsDevice&, byte[])` | Direct3D 9 Effect Framework bytecode (`.fxb`, normally stored in XNB) | `CompiledEffects` |

The five public XNA stock effects -- `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect` and `SkinnedEffect` -- use renderer-owned implementations and require
neither custom-shader capability. `SpriteEffect` is an internal compiled runtime asset, not a
sixth public stock effect.

The complete porter-facing guide to the compiled path -- loading, reflection, parameters, passes,
pass state, samplers, cloning, lifetime, error handling and migration -- is
[`fx-compiled-effects.md`](fx-compiled-effects.md).

## Querying `ShaderEffect` support

Do not infer shader support from the renderer identity, especially in a multi-renderer build. Ask
the live `GraphicsDevice`:

- `SupportsCapability(GraphicsCapability::CustomEffects)` is the legacy broad answer for accepting
  the untyped string constructor. It does not prove that supplied text determines pixels.
- `GetShaderDialectEXT()` is the coarse payload dialect requested by the active renderer.
- `SupportsShaderLanguageEXT(language, stage)` is the exact query used when selecting an explicit
  `ShaderCodeEXT` or `ShaderPackageEXT` variant.
- `ExecutesShaderEffectSourceEXT()` distinguishes a real custom shader path from validation,
  tracing or fixed shading. A true answer still requires a valid, supported payload.
- `SupportsCapability(GraphicsCapability::CompiledEffects)` is independent and applies only to
  Effect Framework bytecode.

The exact language and dialect queries exist independently of the optional engine layer. The
`ShaderCodeEXT` and `ShaderPackageEXT` types, and the corresponding `ShaderEffect` constructors,
are compiled only with `CNA_CNAEXT=ON`.

The direct execution query is intentionally separate from `CustomEffects`. Most notably, HEADLESS
accepts legacy objects but produces no pixels, while an SDLGPU build without runtime GLSL
compilation can still execute an explicitly typed SPIR-V package.

## Current `ShaderEffect` matrix

The three EasyGL identities (`OPENGLES3`, `OPENGL33` and `WEBGL2`) share one implementation, so
the ten public renderer identities reduce to the eight implementation families below.

| Renderer family | Legacy `CustomEffects` | Exact typed intake (`CNA_CNAEXT=ON`) | Executes accepted payload? | Boundary |
|---|---:|---|---:|---|
| EasyGL | ✅ | Desktop GLSL or GLSL ES, matching the live profile | ✅ | Compiles and executes GLSL; `GetShaderDialectEXT()` distinguishes the two profiles. |
| WebGPU | ✅ | WGSL | ✅ | Compiles and executes WGSL on a live device. |
| SDLGPU | ⚙ | SPIR-V | ✅ | The explicit SPIR-V package path exists on every live SDLGPU build and uses ShaderCross when the selected SDL driver cannot consume SPIR-V directly. Legacy GLSL strings additionally require the build-derived `CNA_SDLGPU_SHADER_EFFECTS` definition and runtime `libshaderc`. |
| Software | ❌ | None (`Unknown`) | ❌ | Its resource object may retain source for compatibility, but the fixed CPU rasterizer never executes it. |
| DirectX9 | ✅ | HLSL | ✅ | Uses `D3DCompile`; HiDef targets `vs_3_0`/`ps_3_0` and Reach targets `vs_2_0`/`ps_2_0`. |
| FNA3D | ❌ | None (`Unknown`) | ❌ | FNA3D's shader entry point consumes compiled Effect Framework bytecode instead. |
| Headless | ✅ | None (`Unknown`) | ❌ | Accepts nonempty legacy payloads for validation and trace recording; there are no pixels. |
| Stub | ❌ | None (`Unknown`) | ❌ | No custom-shader renderer is created. |

For the legacy string constructor, the two strings are untyped. The application must first query
the renderer dialect and supply the expected representation. The explicit code/package
constructors instead retain a language and stage, require vertex and fragment variants in the same
language with the `main` entry point, and select only a pair that the live device reports through
`SupportsShaderLanguageEXT()`.

That distinction matters most on SDLGPU. With `CNA_CNAEXT=ON`, its public typed dialect is SPIR-V
and the code/package route does not need a runtime source compiler. The optional legacy route takes
GLSL text and compiles it to SPIR-V; it is unavailable where the build cannot provide target-native
`libshaderc`. The descriptor and coordinate contracts for both routes are documented in
[`sdlgpu-shader-effect-layout.md`](sdlgpu-shader-effect-layout.md).

## Compiled XNA effects

Compiled effects are a separate runtime with their own capability gate:

| Renderer family | `CompiledEffects` |
|---|---|
| FNA3D | ✅ Always enabled |
| EasyGL | ⚙ `CNA_EASYGL_COMPILED_EFFECTS` (off by default) |
| WebGPU | ⚙ `CNA_WEBGPU_COMPILED_EFFECTS` (off by default) |
| SDLGPU | ⚙ `CNA_SDLGPU_COMPILED_EFFECTS` (off by default) |
| Software | ⚙ `CNA_SOFTWARE_COMPILED_EFFECTS` (off by default) |
| DirectX9 | ⚙ `CNA_DIRECTX9_COMPILED_EFFECTS` (off by default) |
| Headless | ❌ |
| Stub | ❌ |

FNA3D owns the MojoShader integration used by FNA. CNA uses that same device-local path to parse
reflection, translate the embedded Shader Model 2/3 programs for the selected FNA3D driver, apply
pass state and bind samplers. It never reinterprets a compiled effect as `ShaderEffect`.

The five opt-in rasterizing families use the same public contract and quality gate. MojoShader is
an additional dependency for the families that do not otherwise need it. With the matching option
off, the renderer reports `CompiledEffects == false` and refuses construction rather than parsing
only metadata or substituting a stock shader. [`fx-compiled-effects.md`](fx-compiled-effects.md)
§10 is the detailed source of truth for each implementation and its remaining input restrictions.

### Loading and use

The byte-array constructor accepts XNA/FNA Direct3D 9 Effect Framework binaries, including the XNA
4 wrapper understood by MojoShader:

```cpp
std::vector<SharpRuntime::bytecs> bytes = ReadAllBytes("BloomExtract.fxb");
Effect effect(device, bytes);

effect.getParametersProperty()["Threshold"]->SetValue(0.25f);
effect.setCurrentTechniqueProperty(effect.getTechniquesProperty()["Bloom"]);
for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
{
    pass.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
}
```

The reflected graph includes parameters, array elements, structure members, annotations,
techniques and passes. Numeric values preserve Effect Framework register padding; texture
parameters are connected to their sampler declarations rather than guessed by uniform name.
`Clone()` creates independent native and public value storage and preserves the selected
technique. `SpriteBatch.Begin(..., &effect)` executes every pass and, as in FNA, binds its sprite
texture to slot zero after each pass is applied.

The built-in `Microsoft.Xna.Framework.Content.EffectReader` loads the same payload from XNB and
returns `std::shared_ptr<Effect>`:

```cpp
auto effect = content.Load<std::shared_ptr<Effect>>("Effects/BloomExtract");
```

The reader bounds the length prefix at 64 MiB, requires an exact payload read, assigns the asset
name and wraps construction failures in `ContentLoadException` with asset context.

### Refused input

The byte-array constructor rejects:

- empty or structurally truncated input;
- input larger than 64 MiB;
- impossible wrapper offsets or unreasonable top-level reflection counts;
- MonoGame `MGFX`/`mgfxo`, which is a different container;
- a valid FX binary when the active renderer reports `CompiledEffects == false`.

Malformed content and an unsupported renderer are distinguishable: `ArgumentException` identifies
invalid FX input, while `NotSupportedException` identifies a format/renderer mismatch. The XNB
reader translates either into an asset-scoped `ContentLoadException`. There is no silent fallback
from either custom-shader contract to a stock effect.

## Sources of truth

- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) is the high-level
  renderer comparison.
- [`renderer-capability-profiles.md`](renderer-capability-profiles.md) documents the live query
  model.
- [`fx-compiled-effects.md`](fx-compiled-effects.md) owns compiled-effect runtime details.
- Each renderer page owns its platform, driver and build restrictions; current defects live only
  in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md).
