# Compiled XNA effects in CNA

This is the porter-facing guide to `Effect(GraphicsDevice&, byte[])` — the API an XNA/FNA game uses
to load a compiled effect it shipped as content. It covers what the format boundary is, how to
load, inspect, apply and clone an effect, which renderer supports it, and how to read every error
the path can produce.

For the architectural background and the remaining backend rollout, see [`plan_fx.md`](../plan_fx.md).
For the difference from the CNAEXT source-based shader API, see
[`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md). For fuzzing, see
[`fx-bytecode-fuzzing.md`](fx-bytecode-fuzzing.md).

## 1. What is accepted

| Input | Accepted | Notes |
|---|---|---|
| XNA/FNA Direct3D 9 Effect Framework binary (`.fxb`) | yes | Including the extra wrapper the XNA 4 Effect compiler prepends |
| The `Effect` payload inside an XNB asset | yes | Through the canonical `Microsoft.Xna.Framework.Content.EffectReader` |
| MonoGame `MGFX`/`.mgfxo` | no | A distinct container; rejected by name, never guessed at |
| HLSL `.fx` **source** | no | CNA embeds no HLSL compiler; compile with the XNA/FNA toolchain first |
| GLSL/SPIR-V/Metal source pairs | not here | That is CNAEXT `ShaderEffect`, a separate API and capability |

The binary is untrusted input. It is bounded at 64 MiB, its reflected object graph is bounded and
arithmetic-checked, and every rejection is a specific exception rather than a generic failure.

**Trust boundary.** That holds for CNA's own code and for the parser paths CNA has hardened, but
not yet for arbitrary hostile content on every driver. A fuzz campaign is now clean on FNA3D's
OpenGL driver, but stops early on the SDL_GPU driver, whose SPIR-V emitter validates shader
bytecode with asserts throughout. Treat compiled effects the way you would treat any other native-parsed asset --
ship your own, do not load one a user supplied. `plan_fx.md` FX-051 and
[`fx-bytecode-fuzzing.md`](fx-bytecode-fuzzing.md) track the remaining exposure.

## 2. Loading

### From bytes

```cpp
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

std::vector<SharpRuntime::bytecs> bytes = ReadAllBytes("Bloom.fxb");
Effect effect(device, bytes);
```

### From XNB through the ContentManager

```cpp
auto effect = content.Load<std::shared_ptr<Effect>>("Effects/Bloom");
```

The reader takes a signed 32-bit length, enforces the ContentReader allocation limit before
allocating, requires an exact payload read so a truncated file can never produce a
partially-initialised effect, assigns the asset name, and wraps any construction failure in a
`ContentLoadException` that keeps the underlying diagnostic. A direct-byte effect and an
XNB-loaded effect have identical reflection and behaviour.

## 3. Checking support first

```cpp
if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
{
    // Fall back to a stock effect, or tell the user which renderer to select.
}
```

`CompiledEffects` is a separate capability from `CustomEffects`. Support for one never implies the
other.

## 4. Inspecting the reflected graph

```cpp
for (int i = 0; i < effect.getParametersProperty().getCountProperty(); ++i)
{
    const EffectParameter& parameter = effect.getParametersProperty()[i];
    parameter.getNameProperty();
    parameter.getSemanticProperty();
    parameter.getParameterClassProperty();   // Scalar / Vector / Matrix / Object / Struct
    parameter.getParameterTypeProperty();    // Bool / Int32 / Single / Texture2D / ...
    parameter.getRowCountProperty();
    parameter.getColumnCountProperty();
    parameter.getAnnotationsProperty();      // immutable compile-time metadata
    parameter.getElementsProperty();         // array elements, when the parameter is an array
    parameter.getStructureMembersProperty(); // struct members, recursively
}
```

Reflected order matches the binary, and name lookup (`parameters["Threshold"]`) returns `nullptr`
for a name the effect does not declare, so a missing parameter is detectable rather than fatal.

Sampler and shader objects are **not** exposed as public parameters, exactly as in XNA and FNA. A
texture used by a sampler appears as the texture parameter the sampler references; assigning it is
how the texture reaches the sampler slot. Uniform names are never guessed.

Numeric storage preserves the Effect Framework's float4 register padding internally while getters
and setters speak the logical XNA shape, so a `float3` array behaves like a `float3` array rather
than exposing padding.

## 5. Setting values

```cpp
effect.getParametersProperty()["WorldViewProj"]->SetValue(worldViewProjection);
effect.getParametersProperty()["Threshold"]->SetValue(0.25f);
effect.getParametersProperty()["Weights"]->SetValue(std::vector<float>{0.2f, 0.8f});
effect.getParametersProperty()["Texture"]->SetValue(&texture);
```

Setters validate the type and count against the reflected declaration, write the correct subrange,
and mark the owning top-level parameter dirty. Only dirty values are uploaded before a pass, so a
per-frame `Apply()` that changes nothing costs no upload.

`SetValueTranspose` exists for the XNA row/column-major distinction, and array-element and
structure-member views write into their parent's storage rather than into a copy.

## 6. Techniques and passes

```cpp
effect.setCurrentTechniqueProperty(effect.getTechniquesProperty()["Bloom"]);
for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
{
    pass.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
}
```

The first reflected technique is selected on construction. `EffectPass::Apply()` applies **that
exact pass** — it never collapses to pass 0. `Effect::Apply()` is defined as pass 0 of the current
technique. A pass belonging to a different technique, or to a different effect, is rejected.

Applying a pass, in order: validate ownership, call the overridable `OnApply()` hook, upload dirty
values and textures, select the exact native technique and pass, apply the pass, translate its
state assignments, then select the effect for subsequent draws.

Compiled passes survive `DrawUserPrimitives`, `DrawPrimitives`, `DrawIndexedPrimitives`,
instanced draws, and `SpriteBatch`. As in FNA, SpriteBatch binds its sprite texture to slot zero
after each pass is applied.

## 7. What a pass does to the device

A legacy Effect Framework pass assigns individual Direct3D 9 render states. CNA publishes them
through the device's own state objects and collections, exactly as XNA and FNA do, so the game
observes them afterwards:

```cpp
pass.Apply();
device.getBlendStateProperty();          // reflects the pass's blend assignments
device.getDepthStencilStateProperty();   // reflects its depth/stencil assignments
device.getRasterizerStateProperty();     // reflects cull/fill/bias/scissor/MSAA
device.getSamplerStatesProperty()[0];    // reflects filter/addressing/LOD/anisotropy
device.getTexturesProperty()[0];         // reflects a SAMP_TEXTURE assignment
```

Rules worth knowing when porting:

- A state group the pass never assigned is left exactly as the game selected it.
- Without `SEPARATEALPHABLENDENABLE`, the alpha blend factors follow the colour ones — but
  `BLENDOP` alone never changes the alpha blend function.
- `BLENDFACTOR` keeps the historical byte order FNA uses.
- A sampler register that carries no sampler state at all is skipped whole, including its texture.
- A sampler's texture is rebound only when the pass assigns `SAMP_TEXTURE` **and** the reflected
  texture parameter actually holds a texture.
- The three Direct3D filter axes collapse into XNA's eight aggregate `TextureFilter` values, with
  an anisotropic component read as its linear equivalent — the FNA behaviour, not a manufactured
  `Anisotropic` state.
- An unknown render-state or sampler-state token raises a diagnostic naming the token rather than
  being silently ignored, and the state groups are published only after every token translated, so
  a rejected token never leaves half a group applied.

`Border` and `MirrorOnce` addressing have no XNA 4.0 `SamplerState` representation and are
rejected by name.

**Known limitation — `AddressW`.** A pass that assigns `ADDRESSW` is translated and published, so
`device.getSamplerStatesProperty()[slot].getAddressWProperty()` reports it correctly. CNA's shared
renderer interface, however, carries no W addressing at all: `IGraphicsRenderer::ApplySamplerState`
takes only U and V, and the FNA3D renderer mirrors U into W. The next draw that re-applies sampler
state from the device therefore overwrites the effect's W mode with its U mode. This is a
pre-existing gap in the renderer-neutral sampler contract rather than a compiled-effect one -- it
affects `SamplerState.AddressW` set directly by a game in exactly the same way -- and it only
matters for volume textures. `plan_fx.md` FX-026 tracks closing it.

## 8. Cloning

```cpp
std::unique_ptr<Effect> copy(effect.Clone());
```

A clone is an independent effect on the same device: its native runtime, parameter values, strings
and texture references are its own, `CurrentTechnique` is preserved by stable index, and either
instance can be disposed first without disturbing the other. Only immutable compiled shader data is
shared.

## 9. Lifetime

- Disposing an effect that is currently selected removes it from the device; a later draw cannot
  use the dead runtime.
- Disposing the device disposes the effects it owns, in an order that never dereferences a dead
  backend.
- `GraphicsDevice::Reset()` reconfigures the backend rather than recreating it, so an existing
  effect keeps working across a reset.
- Using a disposed effect throws `System::ObjectDisposedException`; disposal is idempotent.

## 10. Renderer support matrix

| Renderer | `CompiledEffects` | Why |
|---|---|---|
| `FNA3D` | **true** | Owns the MojoShader Effect Framework runtime and passes the full conformance suite |
| every other renderer identity | false | No compiled-effect runtime yet, or no programmable shader target at all |

An unsupported renderer refuses construction with a `NotSupportedException` naming the capability.
It never accepts the bytecode and quietly draws with a stock shader — a silent fallback would make
a porting bug look like an art bug.

`SDL_GPU`, the EasyGL/OpenGL family, DirectX 11, Vulkan and Metal are the planned next waves; each
becomes true only after it passes the same shared suite. Fixed-function, 2D-only and CPU renderers
stay intentionally unsupported. `plan_fx.md` Phase G tracks the rollout.

## 11. Error guide

| Symptom | Exception | Cause |
|---|---|---|
| `Compiled effect bytecode must not be empty.` | `ArgumentException` | Zero-length input |
| `... exceeds CNA's 64 MiB safety limit.` | `ArgumentException` | Oversized payload |
| `... does not contain a structurally valid XNA Direct3D 9 Effect Framework header.` | `ArgumentException` | Not an Effect Framework binary, or truncated before its header |
| `The supplied bytes are a MonoGame MGFX effect...` | `NotSupportedException` | MonoGame content; recompile with the XNA/FNA pipeline |
| `The active graphics renderer does not support compiled XNA/FNA Effect Framework bytecode...` | `NotSupportedException` | `CompiledEffects` is false for the selected renderer |
| A MojoShader parser diagnostic | `std::runtime_error` | Structurally plausible but corrupt content; the native message is preserved |
| `Shader parameter not found in effect.` | `std::runtime_error` | A shader's constant table names a parameter the effect does not declare |
| `unsupported render state <n>` / `unsupported sampler state <n>` | `std::runtime_error` | A token CNA does not translate; report it with the effect that produced it |
| `Border and MirrorOnce sampler addressing are not representable...` | `std::runtime_error` | Addressing mode outside XNA 4.0 |
| Any of the above while loading an asset | `ContentLoadException` | Same cause, wrapped with the asset name |

Malformed content and an unsupported backend are deliberately distinguishable: `ArgumentException`
means the bytes are wrong, `NotSupportedException` means the format or platform is wrong.

## 12. Migrating from XNA/FNA

The code barely changes; only the C# property syntax does.

| XNA / FNA (C#) | CNA (C++) |
|---|---|
| `new Effect(GraphicsDevice, bytes)` | `Effect effect(device, bytes);` |
| `Content.Load<Effect>("Bloom")` | `content.Load<std::shared_ptr<Effect>>("Bloom")` |
| `effect.Parameters["Threshold"].SetValue(0.25f)` | `effect.getParametersProperty()["Threshold"]->SetValue(0.25f)` |
| `effect.CurrentTechnique = effect.Techniques["Bloom"]` | `effect.setCurrentTechniqueProperty(effect.getTechniquesProperty()["Bloom"])` |
| `foreach (var pass in effect.CurrentTechnique.Passes) pass.Apply();` | `for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty()) pass.Apply();` |
| `effect.Clone()` | `std::unique_ptr<Effect> copy(effect.Clone());` |
| `spriteBatch.Begin(..., effect)` | `spriteBatch.Begin(..., &effect)` |

Subclassing `Effect` still works: `OnApply()` defaults to a no-op and is called before the pass is
applied, which is where a port keeps its per-frame parameter updates.

## 13. Dependency and licence notices

Compiled-effect execution on FNA3D uses MojoShader, which FNA3D carries as a submodule. CNA links
exactly one MojoShader — the revision FNA3D pins — and never builds a second copy.

- FNA3D: pinned at `3240147` by `cmake/ThirdPartyFNA3D.cmake`.
- MojoShader: `6333f74dbd5644789a63e903816441b16c1e8b60`, zlib licence.
- CNA applies one narrow, versioned robustness patch to that exact MojoShader revision
  (`cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch`), which turns thirty-four ways
  untrusted bytecode could crash the process into ordinary parser errors. Thirty-three of the thirty-four were
  found by the FX-051 fuzz campaign; see [`fx-bytecode-fuzzing.md`](fx-bytecode-fuzzing.md) for
  the list and for the exposure that remains. CMake applies the patch automatically and
  idempotently, for fetched checkouts and for an explicit `FETCHCONTENT_SOURCE_DIR_FNA3D` override
  alike.
- The stock `.fxb` fixtures under `modules/renderers/fna3d/effects/` are Microsoft's XNA stock
  effects as redistributed by FNA under `LICENSE.StockEffects`; their provenance and hashes are
  recorded in that directory's `README.md` and verified by a test.

See [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for the full notice set.

## 14. Performance baseline (FX-053)

Measured with `cna_compiled_effect_benchmark` on an FNA3D/SDL_GPU (Vulkan, Intel Iris Xe) Debug
build, `SDL_VIDEODRIVER=offscreen`, while a sanitizer build was competing for the same machine —
so treat the absolutes as pessimistic and compare runs, not numbers.

| Operation | Iterations | Median | Mean |
|---|---:|---:|---:|
| construct `SpriteEffect.fxb` (1 KiB) | 200 | 105.8 us | 98.6 us |
| construct `BasicEffect.fxb` (28 KiB) | 100 | 1964.9 us | 1972.8 us |
| construct `SkinnedEffect.fxb` (54 KiB) | 50 | 1636.8 us | 1641.4 us |
| clone `BasicEffect` | 200 | 261.8 us | 264.6 us |
| apply pass, nothing dirty | 2000 | 2.9 us | 3.0 us |
| set one `float4` + apply pass | 2000 | 3.3 us | 3.3 us |
| set matrix + `float4` + int + apply pass | 2000 | 5.1 us | 5.2 us |
| compiled effect: apply + draw 2 triangles | 500 | 10.9 us | 13.5 us |
| stock `BasicEffect`: apply + draw 2 triangles | 500 | 11.5 us | 12.3 us |

What the numbers say:

- **Construction cost tracks shader work, not file size.** The 54 KiB `SkinnedEffect` constructs
  faster than the 28 KiB `BasicEffect`; the byte count is not the predictor, the embedded programs
  are. Construction belongs at load time, not in a frame.
- **`Clone()` is roughly 7.5x cheaper than constructing the same effect**, because the native clone
  reuses the already-translated shader artifacts and copies only mutable state. A game that needs
  many instances of one effect should clone.
- **Dirty tracking works.** A pass applied with nothing changed costs about 2.9 us; one changed
  `float4` adds ~0.4 us and a matrix plus two scalars ~2.2 us. Unchanged parameters are not
  re-uploaded.
- **A compiled pass is not more expensive to draw with than a stock effect** (10.9 us vs 11.5 us
  for the same geometry), so porting an effect to the compiled path costs nothing per draw.

**Decision on the immutable artifact cache:** not justified. The expensive step is native shader
translation during construction, and `Clone()` already reuses it without sharing any mutable value,
texture, selected technique or pass state. A bytecode-keyed cache would add cross-instance sharing
risk for a case the existing API already covers. `plan_fx.md` FX-053 records this as decided, to be
revisited only if a real port shows repeated construction of identical bytecode in a hot path.
