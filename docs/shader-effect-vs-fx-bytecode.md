# `ShaderEffect` vs. compiled XNA `.fx` bytecode

CNA has two deliberately separate custom-shader contracts. Query the matching capability; support
for one never implies support for the other.

| API | Input | Capability | Current renderer support |
|---|---|---|---|
| `CNAEXT::ShaderEffect` | Caller-authored GLSL, SPIR-V, or another backend-native source pair | `CustomEffects` | Renderer-specific |
| `Effect(GraphicsDevice&, byte[])` | XNA/FNA Direct3D 9 Effect Framework binary (`.fxb`, normally stored in XNB) | `CompiledEffects` | FNA3D; SDL_GPU and the EasyGL/OpenGL family behind their own build options |

The six stock effects remain portable CNA APIs and do not require either custom-effect capability.

The full porter-facing guide to the compiled path -- loading, reflection, parameters, passes, pass
state, samplers, clone, lifetime, the renderer matrix, every error message, and XNA-to-CNA
migration -- is [`fx-compiled-effects.md`](fx-compiled-effects.md).

## Compiled effects on FNA3D

FNA3D already owns the MojoShader integration used by FNA. CNA uses that same device-local path to
parse reflection, translate the embedded Shader Model 2/3 programs for the selected FNA3D driver,
apply pass state, and bind samplers. It does not reinterpret a compiled effect as `ShaderEffect`.

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
techniques, and passes. Numeric values preserve Effect Framework register padding; texture
parameters are connected to their sampler declarations rather than guessed by uniform name.
`Clone()` creates independent native and public value storage and preserves the selected
technique.

`SpriteBatch.Begin(..., &effect)` also executes every pass of a compiled effect. As in FNA,
SpriteBatch binds its sprite texture to slot zero after each pass is applied.

## Loading from XNB

The canonical `Microsoft.Xna.Framework.Content.EffectReader` is registered by the built-in XNB
reader set and returns `std::shared_ptr<Effect>`:

```cpp
auto effect = content.Load<std::shared_ptr<Effect>>("Effects/BloomExtract");
```

The reader bounds the length prefix at 64 MiB, requires an exact payload read, assigns the asset
name, and wraps construction failures in `ContentLoadException` with asset context. On a renderer
whose `CompiledEffects` capability is false, the same asset fails explicitly; it is never replaced
with a stock shader.

## Accepted and rejected formats

The byte-array constructor accepts the XNA/FNA Direct3D 9 Effect Framework format, including the
XNA 4 wrapper understood by MojoShader. It rejects:

- empty or structurally truncated input;
- input larger than 64 MiB;
- impossible wrapper offsets or unreasonable top-level reflection counts;
- MonoGame `MGFX`/`mgfxo`, which is a distinct container and needs a separate future runtime;
- a valid FX binary on any backend that reports `CompiledEffects == false`.

Malformed content and an unsupported backend are intentionally distinguishable:
`ArgumentException` identifies invalid FX input, while `NotSupportedException` identifies a
format/backend mismatch. The XNB reader translates either into an asset-scoped
`ContentLoadException`.

## The other renderers

**Updated 2026-08-17.** Two more backends have since passed the gate: `SDL_GPU`
(`CNA_SDL_GPU_COMPILED_EFFECTS`) and the EasyGL/OpenGL family (`CNA_EASYGL_COMPILED_EFFECTS`), both
opt-in at configure time because MojoShader is a fetched dependency neither renderer otherwise
needs. With the option off they report `CompiledEffects == false` and refuse, exactly like any
unsupported backend. `fx-compiled-effects.md` §10 carries the current matrix and each backend's
remaining refusals.

Every other renderer inherits the common `CreateCompiledEffect()` refusal and reports
`CompiledEffects == false`. This is the correct quality gate: parsing metadata alone, translating
only one shader stage, ignoring pass state, or falling back to pass zero is not advertised as
support. The gate was widened in 2026-08-17's repair pass after three backends were found passing
it while several of their draw routes still rendered with a stock shader -- the shared suite now
reads pixels back for every draw shape it covers.

Future programmable backends should implement the renderer-neutral `ICompiledEffectRuntime` and
pass the same reflection, mutation, clone, draw, SpriteBatch, state, sampler, malformed-input, and
lifecycle suite before enabling the capability. Fixed-function and CPU renderers can remain
explicitly unsupported. The audited rollout and backend-specific feasibility work are tracked in
[`plan_fx.md`](../plan_fx.md).

## `ShaderEffect` remains useful

Use `ShaderEffect` when the port owns backend-specific shader source or bytecode and does not need
XNA Effect Framework techniques, passes, annotations, or pass state. Its support matrix is
independent, so code should query `CustomEffects` and supply the source dialect required by the
selected renderer.
