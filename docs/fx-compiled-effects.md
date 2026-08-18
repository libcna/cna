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

**Trust boundary.** A coverage-guided fuzz campaign is clean on both FNA3D drivers past the bar
`plan_fx.md` FX-051 set for it -- over three million executions on OpenGL/GLSL and over two and a
half million on SDL_GPU/SPIR-V, under AddressSanitizer with asserts fatal, no new crash. Getting
there fixed forty-one distinct ways untrusted bytecode crashed the process.

That is a measured bound, not a proof. Fuzzing cannot establish absence, and the SPIR-V emitter
still validates shader bytecode with asserts in places no campaign has reached. So: ship your own
effects with confidence, and treat a user-supplied one as untrusted input that has been made much
harder to weaponise rather than as safe.
[`fx-bytecode-fuzzing.md`](fx-bytecode-fuzzing.md) records what the bound covers and what it does
not.

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

**String parameters** follow XNA 4.0 rather than FNA. `EffectParameter::SetValue(const
std::string&)` and `GetValueString()` both reject a parameter whose reflected
`EffectParameterType` is not `String` with `System::InvalidCastException`, which is exactly what
`Microsoft.Xna.Framework.Graphics.EffectParameter` does; on a genuine string parameter the value is
stored and reads back. FNA leaves the setter unimplemented (`throw new
NotImplementedException("effect->objects[?]")`), so this is one of the few places CNA follows the
specification FNA is itself approximating. Nothing is written into MojoShader's shared object
table: an Effect Framework string is CPU-side reflection data that no shader stage reads, so CNA
owns the current value per `Effect` instance and a clone gets its own copy.

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

### Sampler state that reaches the GPU

Publishing a pass's sampler state on `GraphicsDevice.SamplerStates` is only half the job; the other
half is that the state actually filters the sampled texture. What each backend can express:

| State | FNA3D | SDL_GPU | EasyGL / OpenGL ES 3 | EasyGL / OpenGL 3.3 |
|---|---|---|---|---|
| `Filter` | yes | yes | yes | yes |
| `AddressU` / `AddressV` | yes | yes | yes | yes |
| `AddressW` | yes | recorded, unobservable | yes (`GL_TEXTURE_WRAP_R`) | yes (`GL_TEXTURE_WRAP_R`) |
| `MaxAnisotropy` | yes | yes | yes | yes |
| `MaxMipLevel` | yes (`GL_TEXTURE_BASE_LEVEL`) | yes (`min_lod`) | yes (`GL_TEXTURE_MIN_LOD`) | yes (`GL_TEXTURE_MIN_LOD`) |
| `MipMapLevelOfDetailBias` | desktop GL only | yes (`mip_lod_bias`) | **no** | yes (`GL_TEXTURE_LOD_BIAS`) |

Every "yes" in that table is now checked by drawing, not by reading CNA's state objects back:
`RunCompiledEffectSamplerPixelContract` binds a real texture, applies the pass, draws, reads the
pixel and compares it against the texel the requested sampler state selects. Addressing is probed
from coordinates outside `[0,1]` with two probes per axis, so `Wrap`, `Clamp` and `Mirror` have
three distinct signatures; the filter check samples a quarter of the way between two texel centres,
away from any boundary; `MaxMipLevel` and the LOD bias are checked against a mipmapped texture whose
levels are different colours (`plan_fx.md` FX-093).

`MipMapLevelOfDetailBias` has no OpenGL ES equivalent at all -- `GL_TEXTURE_LOD_BIAS` is a desktop
GL parameter -- which is why FNA3D's own OpenGL driver skips it under ES too. CNA does not
approximate it there; it is accepted, published on the device state object, and documented as
unrepresentable rather than mapped onto a nearby state. The shared contract is told which subset a
backend can represent rather than guessing, so an ES build proves everything else and skips only
that one section.

`AddressW` is carried through the renderer-neutral contract by
`IGraphicsRenderer::ApplySamplerAddressW` (`plan_fx.md` FX-026). FNA3D and EasyGL apply it; SDL_GPU
records it and carries it in its sampler-cache identity, but its compiled route resolves 2D textures
only, so the axis is not observable there yet (FX-092, FX-110). It only matters for volume textures.

A slot no pass has assigned keeps whatever the game selected. The compiled-effect draw routes read
`GraphicsDevice.SamplerStates[slot]` for those, so `SpriteBatch.Begin`'s own sampler state, for
example, is not overwritten with defaults by an effect that never mentions slot 0.

The reverse also holds, and used to not: a slot a compiled Effect *did* assign must not keep that
assignment once a stock draw takes over. A renderer that mutates one long-lived sampler object per
slot has to re-establish every property on each application, or an effect's `MaxMipLevel` outlives
it into the next `SpriteBatch` flush and clamps a texture the game never asked to clamp. Both EasyGL
and FNA3D had that leak; `RunCompiledEffectStockDrawIsolationContract` pins the fix (FX-092).

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

| Renderer | `CompiledEffects` | Build option | Why |
|---|---|---|---|
| `FNA3D` | **true** | always on | Owns the MojoShader Effect Framework runtime; passes the full shared contract including the draw matrix |
| `SDL_GPU` | **true** | `CNA_SDL_GPU_COMPILED_EFFECTS` (off by default) | MojoShader's SDL_GPU adapter; passes the shared contract, multi-stream and instanced draws excepted -- it advertises neither capability, so `GraphicsDevice` refuses them before submission |
| EasyGL family (`OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`) | **true** | `CNA_EASYGL_COMPILED_EFFECTS` (off by default) | MojoShader's OpenGL adapter; passes the full shared contract, including multi-stream and instanced draws |
| every other renderer identity | false | — | No compiled-effect runtime yet, or no programmable shader target at all |

The two opt-in options exist because MojoShader is a fetched dependency those renderers do not
otherwise need. With the option off the renderer reports `CompiledEffects == false` and refuses a
compiled `Effect` exactly like any unsupported backend -- the capability never claims more than the
build actually contains.

An unsupported renderer refuses construction with a `NotSupportedException` naming the capability.
It never accepts the bytecode and quietly draws with a stock shader — a silent fallback would make
a porting bug look like an art bug. That rule holds per draw route as well, not only per renderer:
a route a backend has not implemented (a compiled effect's vertex shader sampling a texture, a
3D/cube sampler binding, a stream set the renderer cannot bind) throws by name at draw time.

A refusal also has to say *which kind* of limitation it is, because the two kinds mean different
things to a port. A **renderer-wide** limitation is one the renderer has through every route --
vertex-stage texture sampling, for one, which no CNA renderer implements at all; a compiled Effect
is not expected to add it. A **compiled-Effect-specific** limitation is one the renderer does not
have elsewhere -- a `Texture3D` bound to a compiled sampler, say, which both SDL_GPU and EasyGL
sample perfectly well in their ordinary draw families. The second kind is a debt of this feature and
carries a task ID. `plan_fx.md` section 10.5 is the full table.

Vulkan joined the supported set on 2026-08-18 (`CNA_VULKAN_COMPILED_EFFECTS=ON`). It is the one
backend with no MojoShader-provided adapter -- there is no `mojoshader_vulkan.c` -- so the
nine-function effect context is CNA's own, written against the portable SPIR-V profile, and the
renderer builds a pipeline per linked shader pair and vertex layout, binds the four descriptor sets
that profile fixes, and replays the draw from a per-frame uniform ring at `Present()`. It refuses by
name: vertex-stage sampling, multi-stream vertex input, instancing, and a `Texture3D` on a pixel
sampler.

DirectX 11, Metal and DirectX 9 are the planned next waves; each becomes true only after it passes
the same shared suite. Fixed-function, 2D-only and CPU renderers stay intentionally unsupported.
`plan_fx.md` Phase G tracks the rollout, and section 10.3 there classifies every renderer identity.

### What "passes the shared contract" means

`tests/support/CNA/TestSupport/CompiledEffectConformance.hpp` is the whole gate. A backend adds one
test file that builds its device and calls the contract sections; nothing in them is
renderer-specific. They are:

| Section | What it proves |
|---|---|
| format | empty, MGFX, noise and every truncation boundary are rejected distinctly |
| reflection | order, names, semantics, classes, dimensions, arrays, structures, annotations |
| parameter API | every getter/setter round-trips: scalars, vectors, matrices and transposes, arrays, element and member views, string semantics |
| techniques/passes | first technique selected, exact pass identity, technique switching |
| render state | every supported Direct3D 9 render-state token lands on the device's own state objects |
| state policy | an unassigned group survives; an unknown token is refused by name |
| samplers | addressing, LOD, anisotropy, the full filter-collapse table, exact register targeting |
| texture binding | a sampler's texture comes from its reflected texture parameter |
| clone | values, textures and technique copied; independent mutation and disposal |
| lifecycle | repeated create/apply/dispose, disposed-effect rejection, idempotent disposal |
| **draw matrix** | buffered and user draws, indexed and not, non-zero `baseVertex`/`startIndex`/`vertexStart`, and canonical built-in vertex types with no explicit declaration -- each reading back the effect's own `Tint` from a render target |
| **multi-stream** | a shader consuming attributes from two bound buffers renders differently when the second stream's contents change, including with a different non-zero `VertexOffset` per stream |
| **instancing** | a per-instance stream advances per instance, survives a non-zero `baseVertex`/`startIndex`/instance offset, and does not leave its divisor behind for the next ordinary draw |
| **SpriteBatch** | `SpriteBatch.Begin(..., effect)` either runs the compiled shader or refuses by name |
| **SpriteBatch multi-pass** | the passes run at XNA's own batch granularity -- once per pass over the whole texture run, not once per sprite |
| **SpriteBatch texture slot** | the sprite being drawn wins slot 0 over the effect's own texture parameter, and source rectangles and `SpriteEffects` reach the shader |
| **sampler pixels** | the pass's texture binding, addressing, filter, `MaxMipLevel` and LOD bias are checked in the rendered pixel, not on a state object |
| **pass selection** | the pass a caller applies is the pass that draws; the fixture's passes write different colours, so a fallback to pass 0 cannot pass |
| **render-target source** | a compiled Effect sampling a `RenderTarget2D` sees it the same way up as a stock draw does, through one hop and two, while a plain `Texture2D` is not flipped |
| **stock-draw isolation** | a compiled Effect's sampler state does not survive into a later stock `SpriteBatch` draw |
| **orientation** | compiled geometry lands in the same half of a render target as a stock effect's |
| **effect switching** | two effects and a clone drawing alternately each render their own parameter values, and a stock 3D or `SpriteBatch` draw in between disturbs neither direction |

The draw sections all read a pixel back and compare it against the effect's own parameter. That is
deliberate: a draw that silently fell back to a stock shader, bound an attribute from the wrong
stream, applied the wrong pass, or dropped the pass's sampler state produces a different pixel
instead of passing quietly. `RunCompiledEffectContract`'s own doc comment carries the authoritative
list of the drawing sections a backend must run; this table is a description of what they mean, not
a second copy of the list.

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
| `...binds a Texture3D/TextureCube to pixel sampler slot N. This renderer samples that kind elsewhere...` | `NotSupportedException` | A compiled-Effect-specific limitation on SDL_GPU and EasyGL (`plan_fx.md` FX-110) |
| `...samples slot N, but no texture is bound there.` | `NotSupportedException` | The effect's texture parameter was never assigned, and nothing was selected on `GraphicsDevice.Textures` |
| `Vertex-stage texture sampling is not implemented in this renderer at all, by any draw route.` | `NotSupportedException` | Renderer-wide, not FX-specific (`plan_fx.md` FX-109) |
| `...cannot run after the GL context was recreated...` | `NotSupportedException` | EasyGL only: MojoShader's context and its linked programs died with the old GL context; recreate the `Effect` from its bytecode (`plan_fx.md` FX-107) |
| `...cannot sample the RenderTarget2D it is drawing into.` | `NotSupportedException` | EasyGL only: reading and writing one target in a single draw, which is undefined in XNA too |
| `...cannot sample a RenderTarget2D on the OpenGL ES 2.0 / WebGL 1 profiles...` | `NotSupportedException` | Correcting a render target's row order needs `glBlitFramebuffer`, which those profiles do not have |
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
  (`cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch`), which turns forty ways
  untrusted bytecode could crash the process into ordinary parser errors. Thirty-nine of the forty were
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
