# Compiled XNA effects in CNA

Current source/test contract checked on 2026-11-01. This is the porter-facing guide to
`Effect(GraphicsDevice&, byte[])` — the API an XNA/FNA game uses
to load a compiled effect it shipped as content. It covers what the format boundary is, how to
load, inspect, apply and clone an effect, which renderer supports it, and how to read every error
the path can produce.

The architecture is one renderer-neutral `ICompiledEffectRuntime` per effect: the graphics layer
owns the reflected XNA object graph and every instance's mutable values, and the renderer owns the
native shaders, pass application and device-bound resources. The tasks that built it
(`FX-001`..`FX-147`, 2026-08/10) are in Git history; source comments cite them as "FX-nnn".
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
| GLSL/SPIR-V/WGSL source pairs | not here | That is CNAEXT `ShaderEffect`, a separate API and capability |

The binary is untrusted input. It is bounded at 64 MiB, its reflected object graph is bounded and
arithmetic-checked. Header/limit/unsupported-format checks are distinct; native parser/shader
failures can still use renderer-specific runtime diagnostics.

**Trust boundary.** The retained FX-051 record reports more than three million OpenGL and
2.6 million SDL_GPU driver executions under ASan after forty-one historical crash classes were
fixed. Those runs used their recorded revision/seeds; current parser hardening and finite replay
do not prove safe behavior for all inputs or a clean current sanitizer campaign.
[Fuzzing scope](fx-bytecode-fuzzing.md) records the entry point's actual coverage and blind spots.

**Standing decisions, not gaps.** CNA embeds no HLSL compiler, reads no MonoGame MGFX, and never
translates a compiled effect into a `ShaderEffect`. A `.fx` source is compiled by the XNA/FNA
toolchain first; CNA's content pipeline can drive such an external compiler, but never fakes a
compile by embedding source text ([`xnb-interoperability.md`](xnb-interoperability.md)).

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
and mark the owning top-level parameter dirty. The graphics layer synchronizes only dirty parameter values to ICompiledEffectRuntime before
a pass. Native pass/draw paths can still pack/upload register blocks or capture snapshots, so an
unchanged Apply is not a universal zero-upload or zero-work guarantee.

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

EffectPass.Apply validates its owner/current technique before forwarding the stored pass index.
Effect's internal path checks disposal, calls OnApply, validates the current technique/index,
synchronizes dirty values, selects the native technique, applies/translates the pass and then
selects the effect for later draws. These are not transaction/rollback guarantees: user OnApply
and native application can have side effects before a later failure.

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
- Unknown translated state tokens produce a diagnostic. Runtime translation builds the change
  set before common publication; publication/native application and user callbacks are not a
  globally atomic transaction across every device/native state.

`Border` and `MirrorOnce` addressing have no XNA 4.0 `SamplerState` representation and are
rejected by name.

### Sampler state that reaches the GPU

Publishing a pass's sampler state on `GraphicsDevice.SamplerStates` is only half the job; the other
half is that the state actually filters the sampled texture. The relevant state groups are:

The common contracts distinguish Filter, AddressU/V/W, anisotropy, MaxMipLevel and
MipMapLevelOfDetailBias from the native route that implements them. See
[sampler state support](sampler-state-support.md) for current profile/driver boundaries; the old
four-column table omitted later WebGPU/CPU/D3D9 work and incorrectly called SDLGPU AddressW
unobservable after the volume path existed.

The supported sampling paths are checked by drawing, not only by reading CNA's state objects back:
`RunCompiledEffectSamplerPixelContract` binds a real texture, applies the pass, draws, reads the
pixel and compares it against the texel the requested sampler state selects. Addressing is probed
from coordinates outside `[0,1]` with two probes per axis, so `Wrap`, `Clamp` and `Mirror` have
three distinct signatures; the filter check samples a quarter of the way between two texel centres,
away from any boundary; `MaxMipLevel` and the LOD bias are checked against a mipmapped texture whose
levels are different colours (FX-093). The cube/volume contract proves the
dimensioned paths.

The current ordinary GL ES sampler path cannot use the desktop GL_TEXTURE_LOD_BIAS parameter;
FNA3D's OpenGL ES path omits that fixed state too. This is a route limitation, not a claim that
shader-based LOD adjustment is impossible in ES. CNA's ordinary sampling path does not approximate
that state there; it is accepted, published on the device state object, and documented as
unrepresentable rather than mapped onto a nearby state. The shared contract is told which subset a
backend can represent rather than guessing, so an ES build proves everything else and skips only
that one section. The one exception is a compiled vertex shader's `TEXLDL` on ES 3, which already
carries an explicit level: there the bias is added to that level in the generated GLSL (FX-146,
[`sampler-state-support.md`](sampler-state-support.md)).

`AddressW` is carried through the renderer-neutral contract by
`IGraphicsRenderer::ApplySamplerAddressW` (FX-026). FNA3D, EasyGL, SDLGPU and
WebGPU apply it; SDLGPU and WebGPU prove it on a volume texture through the shared
`RunCompiledEffectAddressWContract`. It only matters for volume textures.

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

A clone has independent mutable effect/runtime state on the same device, including parameter
values, strings, bindings and selected technique index. Either effect may be disposed first.
Texture bindings still refer to the same external texture resources; cloning does not deep-copy
or independently own those textures. Native immutable shader artifacts may be shared.

## 9. Lifetime

- Disposing an effect that is currently selected removes it from the device; a later draw cannot
  use the dead runtime.
- Disposing the device disposes the effects it owns, in an order that never dereferences a dead
  backend.
- Ordinary presentation-reset tests retain the logical effect. Native context/device recovery
  is a separate path and may rebuild its runtime from retained state; a reset pass is not proof
  of arbitrary driver-loss recovery on every renderer.
- Apply/pass application, cloning and current-technique mutation check disposal and throw
  ObjectDisposedException; disposal is idempotent. Do not generalize that to every metadata
  getter: the reflected collections/values can remain accessible after disposal.

## 10. Renderer support matrix

The true entries below are conditional on the option being enabled and the selected route's
requirements. They are not a promise to execute every Shader Model 1–3 program without refusal.
SOFTWARE has a bounded instruction executor with named unsupported-opcode/shape paths.

| Renderer | `CompiledEffects` when enabled | Build option | Implementation |
|---|---|---|---|
| `FNA3D` | **true** | always on | MojoShader runtime with native-driver-specific execution; focused shared contracts |
| `SDLGPU` | **true** | `CNA_SDLGPU_COMPILED_EFFECTS` (off by default) | MojoShader SDL_GPU adapter and queued compiled draw route |
| EasyGL family (`OPENGLES3`, `OPENGL33`, `WEBGL2`) | **true** | `CNA_EASYGL_COMPILED_EFFECTS` (default ON for OPENGL33 without ES/WebGL identities; otherwise OFF) | MojoShader OpenGL adapter with instance-owned GL profile selection |
| `WEBGPU` | **true** | `CNA_WEBGPU_COMPILED_EFFECTS` (off by default) | CNA's MojoShader SPIR-V binding, translated to WGSL for the browser (WEBGPU-167, WEBGPU-203/204) |
| `SOFTWARE` | **true** | `CNA_SOFTWARE_COMPILED_EFFECTS` (off by default) | Bounded CPU executor with named instruction/shape limits (SOFTWARE-162/165) |
| `DIRECTX9` | **true** | `CNA_DIRECTX9_COMPILED_EFFECTS` (off by default) | Native D3D9 shader tokens from the effect; MojoShader only for the container and reflection (FX-070) |
| HEADLESS / STUB | false | — | Validation/windowless roles without a compiled-effect executor |

EasyGL selects the MojoShader source dialect from the renderer instance that owns the GL context:
desktop `OPENGL33` uses `glsl120`, and the ES 3/WebGL 2 identities use `glsles3`. It deliberately
does not accept `MOJOSHADER_glBestProfile` choosing `glspirv` on a newer desktop driver: that
adapter assumes a complete vertex/pixel pair, while an XNA Effect pass may validly assign only one
stage and inherit the other at draw time.

The opt-in options control the additional parser/translation/executor dependency and code. With the option off the renderer reports `CompiledEffects == false` and refuses a
compiled `Effect` exactly like any unsupported backend -- the capability never claims more than the
build actually contains.

An unsupported renderer refuses construction with a `NotSupportedException` naming the capability.
It never accepts the bytecode and quietly draws with a stock shader — a silent fallback would make
a porting bug look like an art bug. That rule holds per draw route as well, not only per renderer:
a route a backend has not implemented (a compiled effect's vertex shader sampling a texture, a
volume sampler on a backend/profile that cannot sample it, a stream set the renderer cannot bind)
throws by name at draw time.

A refusal also has to say *which kind* of limitation it is, because the two kinds mean different
things to a port. A **renderer-wide** limitation is one the renderer has through every route.
HiDef vertex textures are not a universal limitation: FNA3D, EasyGL and SDLGPU route the public
`VertexTextures`/`VertexSamplerStates` collections to compiled draws. SDLGPU captures independent
vertex-stage textures and sampler descriptions when a draw is issued, including changes after
`EffectPass.Apply()`, and retains the sampled resources through deferred submission. Single and
Vector4 textures have pixel regression coverage. WebGPU also captures vertex bindings and uses
non-filtering layouts for point-sampled float32 textures; its native and WGSL routes share the
same pixel oracle. The museum's `fx-compiled` room confirms both on RADV (2026-10-25); the exact
repair evidence is in Git history (`git show 9d49828f4:CNA_MUSEUM_REPAIR_HANDOFF.md`).
A **compiled-Effect-specific** limitation is one
the renderer supports elsewhere but this draw route cannot preserve; it is a debt of the
compiled-effect work, and its refusal must say so rather than imply the renderer cannot do it.
Neither kind is ever a silent fallback, a substituted texture, an ignored state or an ignored pass.
§11 lists the refusals a supported backend still makes. Cube and volume sampling are no longer
among them: FNA3D, SDLGPU and WebGPU pass the shared cube/volume contract, and EasyGL's GLSL ES
profiles accept a volume sampler since FX-143.

Running EasyGL's Shader Model 1-3 sections on the MojoShader SPIR-V route (Apple M4
AM4-144) found silent wrong-pixel defects shared by every SPIR-V backend, all fixed for `SDLGPU`
and `WEBGPU`: a predicated `TEXKILL` that killed unconditionally
(libcna/mojoshader `spirv-predicated-texkill`), `vPos` at the pixel centre rather than
Direct3D 9's integer pixel (`spirv-vpos-pixel-corner`), and `TEXBEM`/
`TEXBEML` with zero bump matrices (`MojoShaderEffect::PackSpirvUniformBlock` fills the two float4s
per sampler MojoShader appends to the uniform block). Three more are the pipeline's, and
`MojoShaderEffect`'s SPIR-V adaptations (`SpirvPipelineAdaptation.hpp`) fit the module to it: `vFace`
is mirrored because both renderers keep counter-clockwise as the native front face; a colour output
with no attachment becomes a private variable, which Vulkan's validation layer otherwise reports;
and on WebGPU, whose viewport cannot leave its attachment, the vertex stage's clip position moves
by XNA's pixel-centre correction (SDLGPU moves the viewport, as FNA3D's SDLGPU driver does).
The repaired MojoShader pin `c6397f8dd949d21e72bf9ad2550648a96e6d7d3f`, recorded by FNA3D
`6a35962dbe48ed202259fa60f927688a7c5e4ea8`, was published on 2026-11-08. It implements
ps_1_4 `BEM` using the destination-numbered texture-stage bump matrix and `TEXDEPTH` using
guarded r5.r/r5.g fragment depth. It also admits `TEXREG2AR`, `TEXREG2GB`, `TEXREG2RGB`,
`TEXDP3`, `TEXDP3TEX` and `TEXM3X2DEPTH`. No opcode in this eight-instruction SPIR-V
envelope retains a missing-emitter refusal. OFF builds retain the named
`GraphicsCapability::CompiledEffects` refusal for all eight opcode fixtures before translation.
Each accepted renderer route still requires its shared pixel and lifetime qualification.

The unchanged 44-case shared corpus now exercises all five newly admitted instructions through
public `Effect` construction from compiled bytes and real `Texture2D` sampling. Its new 8x8
full-image checks use the fork tasks' literal source RGBA `(32,96,160,224)` and 4x4 lookup grid
`(40(x+1),40(y+1),7(4x+y+1),255)`, with point/clamp sampling. D3D9 instruction arithmetic,
shown beside the assertions, independently yields AR `(3,0)`, GB `(1,2)`, 2D RGB `(0,1)` and
signed RGB `(0,0)`. The dot row `(2.25,1,-.5,9)` gives `88/255` without the fourth component:
`TEXDP3` broadcasts it to all four lanes, and `TEXDP3TEX` samples `(dot,0)` at texel `(1,0)`.
The signed source gives a negative dot, hence RGBA-zero or lookup `(0,0)`. A nonzero clear in
all channels detects discarded output. Distinct source channels and lookup axes discriminate
wrong channels, coordinates, samplers and dot4. Every pixel is checked exactly; the original
coordinate, implicit-LOD, volume and depth assertions are retained with their existing tolerances.

The matrix-depth contract retains its original checks and adds full 8x8 depth-tested draws
using the literal fork fixture. Source `(32,96,160,224)/255`, numerator row `(1,0,0,9)`
and denominator row `(0,4,0,9)` give `1/12` plain and `191/252` with both sources `_bx2`.
Mixed modifiers use numerator `(-1,0,0,9)`, giving `191/384` (PAD signed) or `8/63`
(DEPTH signed). Each fraction fails LESS against a known lower depth clear and passes LESS
against an upper clear. A zero denominator writes one, failing LESS and passing GREATER
against `15/16`. Raster depth `.9` cannot produce the passing observations. Exact magenta
or distinct nonzero clear is required at every pixel. A standalone
`CompiledEffectLegacyMatrixDepthTest` also reaches the FNA3D drivers; the corpus stays 44 cases.
The fork includes the ES3 float depth-API repair described in
[`fna3d-dependency-defects.md`](fna3d-dependency-defects.md).

MojoShader now infers legacy sampler dimensions from the effect's typed Texture parameters,
following sampler-state Texture object/name links before compiling each shader. CTAB associates
sampler names with registers; explicit pass `Sampler[n]` bindings also work without CTAB.
Caller-provided sampler maps retain priority. CNA's SDLGPU/WEBGPU callbacks forward the maps;
their older CTAB-only fallback remains for dependency source overrides.
`CompiledEffectSamplerInferenceTest` uses ordinary Effect parameters and GraphicsDevice.Textures,
with generic sampler CTAB/state types and typed Texture2D/TextureCube/Texture3D parameters.
All six unsigned/_bx2 forms compare all 64 pixels to hand-derived lookup-grid literals.
Direct MojoShader tests with explicit maps remain independent controls. Public GPU qualification
of the new CNA depth and inference witnesses is an integration gate; their presence alone
does not establish a renderer pass.

Generic texture declarations without a typed parameter/register association still default to 2D.
Incompatible typed bindings across passes sharing one shader and inferred sampler arrays refuse
by name. Inference does not widen TEXREG2AR/GB or TEXDP3TEX beyond their supported 2D forms.

Predicated SPIR-V destination writes preserve inactive lanes with a typed select after the
write mask, shift and saturation operations. Predicated TEXKILL uses select-based component
masking compatible with the pinned Naga translator. Both original 44-case GPU corpora passed
at the task-14 pin. Task 15 adds ps_1.x scale-before-saturation, with 456 numeric/module cases
and fork qualification. Later fork evidence includes 204 TEXREG2 IR variants, 96 dot variants,
72 mapped native MojoShader draws and 144 FNA3D native full-image draws across OpenGL, ES3 and
SDLGPU. Those are fork results, not a current-CNA GPU pass: after publication and integration
of TQ-SWEEP12-10..15 into `next`, the first private multi run passed BEM but exposed the old
five-opcode refusal expectations and the remap fixture's missing TEXCOORD1 output. This task
aligns the envelope and fixture; the updated CNA policy/corpus runs remain an integrator gate.

### What "passes the shared contract" means

`tests/support/CNA/TestSupport/CompiledEffectConformance.hpp` is the whole gate. A backend adds one
test file that builds its device and calls the contract sections; shared assertions are parameterized by route/profile options, and the backend tests select the
sections they exercise. The following describes major sections, not the full current call list:

| Section | What it proves |
|---|---|
| format | empty, MGFX, noise and every truncation boundary are rejected distinctly |
| reflection | order, names, semantics, classes, dimensions, arrays, structures, annotations |
| parameter API | selected scalar/vector/matrix/transpose/array cases, element/member views and string semantics |
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
| **SpriteBatch inherited projection** | a pixel-only pass inherits the authentic stock vertex shader and receives its projection in Effect Framework matrix storage; four coloured source quadrants must reach all four target corners |
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

**Evidence rule.** Promotion requires real public-API draw evidence, not only parsing or shader
text. The capability describes the implemented route; it does not prove today's entire matrix
was run. Backend test call sites, profile options, named refusals and individual skips determine
what a particular pass establishes. Stock/core tests and browser/native tests remain distinct.

### 10.1 Depth convention on EasyGL

MojoShader ends every vertex shader it generates with Direct3D 9's clip-space depth
conversion:

```glsl
gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
```

because OpenGL's clip volume is `z ∈ [-w, w]` where Direct3D's is `[0, w]`. **EasyGL's own
stock shaders now apply the same conversion** (`SOFTWARE-336`). Both paths therefore consume
XNA's Direct3D-style clip depth and use the viewport's complete `[min,max]` window-depth range:

```text
stock     d = min + z·(max-min)
compiled  d = min + z·(max-min)
```

The older SAMPLE-028 repair narrowed only compiled-effect draws to the stock path's incorrect
upper-half scale. That restored relative ordering on `ColorReplacementSample_4_0`, but left stock
`z=0` at window depth 0.5, admitted stock geometry behind XNA's near plane, and discarded half the
depth precision. `BackBufferDepthStencilContractTest.StockEffectUsesXnaClipDepthConvention` pins
both absolute failures; `EasyGLCompiledEffectDrawTest.IsDepthTestedOnTheSameScaleAsStockGeometry`
retains the mixed-path ordering proof after the workaround's removal.

This is EasyGL-specific: `FNA3D` runs MojoShader's own OpenGL device end to end, and
`SDLGPU` has its own convention.

### 10.2 Row order when a compiled effect samples a render target, on EasyGL

An OpenGL framebuffer object stores its first row at the **bottom** of the image, so a
`RenderTarget2D`'s colour texture is upside down relative to an uploaded `Texture2D`. EasyGL
corrects that when the target is sampled rather than when it is drawn, and it has two ways of
doing so:

* **Stock and `ShaderEffect` sprites** mirror V in the sprite's own vertex data, because
  `SpriteBatch.Begin` may substitute a user shader whose GLSL this renderer does not own.
* **Compiled effects** get a row-reversed **copy** of the source, made per sampler slot, because
  the current MojoShader draw path does not consume EasyGL's stock sampling-flip flag.

Only one of the two may apply to any one draw. A `SpriteBatch` batch flushing through the
compiled route therefore leaves its sprite V alone and relies on the copy — which is also the
only one of the two that can correct a slot the sprite quad does not own, such as
`GraphicsDevice.Textures[1]`. This matters to any game whose postprocess is a chain of
fullscreen `SpriteBatch` quads reading each other's render targets; getting it wrong turns the
frame upside down while leaving the intermediate buffers individually correct.

The compiled route prepares a flipped copy for a bottom-up sampled target; this can require a
blit and is not a constant free operation. It refuses sampling the target currently being drawn
into. Consult the actual preparation/draw path rather than assuming one copy per application frame.

Found on `BloomSample_4_0`. `RunCompiledEffectSpriteBatchRenderTargetSourceContract` pins it —
one hop, two hops, and a plain `Texture2D` that must stay untouched.

This is EasyGL-specific: `SDLGPU` and `FNA3D` store render-target rows the same way
up as an uploaded texture and need no correction on either route.

### 10.3 Fragment-shader precision on the GL ES profiles

A Direct3D 9 shader computes in full 32-bit float unless an instruction opts into partial
precision. GLSL ES fragment float precision must be declared. The old translator's mediump choice could
have insufficient range for such expressions; mediump specifies minimum requirements and may
use higher precision, rather than universally meaning one fixed fp16 representation.
See GLSL ES 3.00 §4.5 in the [Khronos specification](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf).

That is not a small-error problem, which is what makes it worth naming here. The usual shape is
a vector carried in world units and normalized in the pixel shader:

```hlsl
// vertex shader
output.lightDirection = LightPosition - worldSpacePos;   // e.g. ~1000 units long

// pixel shader: a limited-range mediump implementation can overflow this dot product.
input.lightDirection = normalize(input.lightDirection);
```

On the affected limited-range path, that overflow made normalization collapse and terms derived
from the direction lose their expected lighting contribution. This was a device/path failure,
not a guaranteed result for every mediump implementation. Nothing errors. The frame still draws the geometry, still tracks the camera, and
still honours the terms that do not depend on the direction, so a scene lit by an ambient
constant and nothing else can look like a working render until the light is moved and not one
pixel changes.

CNA's pinned translator emits highp for its ES fragment path (fragment-precision). ES 3.00
supports it; desktop GLSL uses its own profile-appropriate precision code. The qualifier is not
an assertion that every driver will reproduce all D3D9 floating-point behavior bit-for-bit.

Found on `NormalMappingSample_4_0` (FX-121).

### 10.4 Vertex colour output clamping in the GLSL profiles

Direct3D 9 clamps a vertex shader's colour output registers (`oD0`, `oD1`) to [0,1] **before**
the rasterizer interpolates them. A shader that computes a lighting sum can and often does
exceed 1 at a vertex, and D3D9 therefore interpolates from the clamped value, not the raw one.

MojoShader's GLSL ES profiles have no `gl_FrontColor` and fall through to a plain varying, which
nothing clamps — so the raw value is interpolated and saturated per fragment instead. The desktop
profiles write `gl_FrontColor`, which a compatibility context is specified to clamp, yet FNA3D's
OpenGL driver drew BasicEffect's lit colour unclamped on Mesa 25 (2026-10-14), and a core context
has no such clamp. The two are not the same picture:

```text
vertex colours 1.24 and 0.40, interpolated across a triangle

  D3D9   clamp first:  1.00 ......... 0.40    a slope the whole way
  GL ES  clamp last:   1.00 == 1.00 . 0.40    flat, then a steeper slope
```

On a low-poly mesh that flat region is large and obvious. CNA restores the clamp
(libcna/mojoshader `vertex-color-clamp`) where the generated vertex shader's outputs become final,
for every GLSL profile, desktop and ES alike: at the end of `main()`, or, when an explicit `ret`
ends `main()` ahead of its subroutines, in an epilogue function defined once every colour register
those subroutines write is known.

Found on `PerPixelLightingSample_4_0` (FX-122). The tell was that red and
green matched the original exactly and only blue differed, and only where blue would exceed 1.

## 11. Error guide

These are diagnostic examples, not an exhaustive list or a stable message protocol. Profile,
format, native-driver and selected instruction requirements can refuse a particular draw.

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
| A backend/profile-specific `Texture3D`/`sampler3D` refusal | `NotSupportedException` | Volume sampling remains renderer/profile-dependent; cube sampling is supported by every compiled-Effect backend (FX-110) |
| `...samples slot N, but no texture is bound there.` | `NotSupportedException` | The effect's texture parameter was never assigned, and nothing was selected on `GraphicsDevice.Textures` |
| A vertex-stage texture sampling refusal | `NotSupportedException` | Check the selected renderer's compiled-effect path; this is no longer a universal CNA limitation (FX-109 and the museum B8 repair) |
| `...cannot run after the GL context was recreated...` | `NotSupportedException` | EasyGL only: a compiled draw reached the renderer before it had restored its MojoShader state for a recreated GL context. Recovery rebuilds every live compiled effect from its retained bytecode, values, textures and technique (FX-107), so this is a draw issued between the loss and that recovery |
| `...cannot sample the RenderTarget2D it is drawing into.` | `NotSupportedException` | EasyGL only: reading and writing one target in a single draw, which is undefined in XNA too |
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

Compiled-effect adapters share cna_mojoshader and the common translation helper. The dependency
integration selects MojoShader from the FNA3D pin, can publish it without linking the FNA3D
renderer, and removes its SDL standard-library/SDL_GPU edges for supported SDL-free compositions.
See cmake/ThirdPartyFNA3D.cmake; do not assume compiled effects require selecting FNA3D.

- FNA3D: CNA's fork libcna/FNA3D, pinned by `cmake/ThirdPartyFNA3D.cmake`
  ([`fna3d-renderer.md`](fna3d-renderer.md#the-two-forks) lists the pins and why the forks exist).
- MojoShader: CNA's fork libcna/mojoshader, FNA3D's submodule: the submodule revision recorded by the current FNA3D pin, zlib licence. The historical
  6333f74 base has since been merged with newer upstream work; it is not today's whole pin. A MojoShader fix is
  named here by its former patch name (`effect-parser-robustness` was
  `cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch`); each is one commit on the
  fork's `cna` branch with that name in its `CNA-Patch:` trailer
  (`git log --grep=effect-parser-robustness`).
- The effect-parser-robustness commit contains historical parser fixes. Retained counts describe
  that campaign and do not count every later hardening change; see
  [`fx-bytecode-fuzzing.md`](fx-bytecode-fuzzing.md) for the list and for the exposure that
  remains. It is part of the fork's source, so a fetched checkout and an explicit
  `FETCHCONTENT_SOURCE_DIR_FNA3D` checkout of the pin both have it.
- That directory contains six redistributed Microsoft XNA stock effects under LICENSE.StockEffects
  and six CNA-specific compiled effects on 2026-11-01. Its README/hash checks describe their
  separate provenance; do not apply the stock-effect attribution to every .fxb in the directory.

See [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for the full notice set.

## 14. Performance baseline (FX-053)

Measured with `cna_compiled_effect_benchmark` on an FNA3D/SDLGPU (Vulkan, Intel Iris Xe) Debug
build, `SDL_VIDEODRIVER=offscreen`, while a sanitizer build was competing for the same machine. These are historical observations,
not a current baseline, controlled before/after comparison or upper bound on present latency.

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

What that historical fixture/run illustrated:

- **Construction cost tracks shader work, not file size.** The 54 KiB `SkinnedEffect` constructs
  faster than the 28 KiB `BasicEffect`; the byte count is not the predictor, the embedded programs
  are. Construction belongs at load time, not in a frame.
- **`Clone()` is roughly 7.5x cheaper than constructing the same effect**, because the native clone
  reuses the already-translated shader artifacts and copies only mutable state. A game that needs
  many instances of one effect should clone.
- **Dirty tracking works.** A pass applied with nothing changed costs about 2.9 us; one changed
  `float4` adds ~0.4 us and a matrix plus two scalars ~2.2 us. Unchanged parameters are not
  re-uploaded.
- The recorded compiled/stock point estimates were close (10.9 versus 11.5 us). Without retained
  distributions and a controlled workload they establish neither equivalence nor zero per-draw
  cost for a port; deferred submission and CPU/GPU timing boundaries also matter.

**Historical decision on the immutable artifact cache:** no cache was added on that evidence. The expensive step is native shader
translation during construction, and `Clone()` already reuses it without sharing any mutable value,
texture, selected technique or pass state. A bytecode-keyed cache would add cross-instance sharing
risk for a case the existing API already covers. FX-053 records this as decided, to be
revisited only if a real port shows repeated construction of identical bytecode in a hot path.

## 15. 2026-11-01 verification

The existing multi-tree Renderer/Graphics targets and replay/benchmark tools build. Privately,
106 common Effect/parameter/pass/technique tests pass. FNA3D's OpenGL and SDL_GPU drivers each
pass 66/67 selected compiled/oracle tests, with the same volume case skipped under Reach.
CNA SDLGPU passes 61/62 (ShaderCross integration not linked); native WEBGPU passes all 64.
Both FNA3D drivers replay all twelve current effect binaries plus fourteen crash artifacts.

EasyGL and SOFTWARE compiled-effect options are OFF in that tree, DIRECTX9 is absent, and no
browser build is executed here. This is current focused functional/replay evidence, not a new
ASan/UBSan/LSan campaign, all-backend promotion, historical performance reproduction or proof of
every untrusted input. Documentation changes no implementation or ABI.


## 16. GAP-13 native qualification handoff (2026-11-06)

OPENGL33-only EasyGL selections default to compiled effects ON after the owner's AMD and
software-GL corpus passes. The shared option stays default OFF if OPENGLES3 or WEBGL2 is also
compiled; the other four optional-family defaults remain OFF. The
owner configured the existing `cmake-build-opengl33` and `cmake-build-mingw` trees with their
EasyGL/DirectX9 compiled-effect options ON. An ON cache entry and a successful build do not
establish a corpus pass. `Gap13_CompiledPolicy_OPENGL33` / `_DIRECTX9` independently check the
configured policy, detailed capability, and two compiler-produced shader parameter colors;
`Gap13_CompiledCorpus_OPENGL33` / `_DIRECTX9` run the shared drawing corpus, now 44 cases
(the previous 43 plus independent register-only `TEXDEPTH` coverage). Existing backend non-drawing and lifecycle
assertions remain required, including the three TQ-073 compiled teardown/recovery cases.
The native gates check a fresh Google Test XML inventory: missing, empty, partial, skipped or
failed cases cannot pass merely because a process exits successfully.

The default declaration gate is `CnaCompiledEffectsDefaults`; it evaluates the real option
declarations and verifies explicit OFF opt-outs. Promotion changes its independent released-policy
expectation alongside the source defaults only after the owner retains the public corpus results.
The SOFTWARE/SDLGPU/WEBGPU and browser allocations remain a separate qualification stage; their
defaults have not changed. FNA3D stays always enabled and HEADLESS/STUB stay unsupported.

The native installed-SDK checker accepts `--compiled-effects --compiled-renderer <identity>`.
It copies the compiler-produced fixture into a source-independent consumer and checks the
producer's expected capability plus shader pixels, or a capability-named refusal for an OFF
producer. `--prepare-only` builds and relocates the native consumers without running them; its
audit explicitly records `passed=false` and pending runtime tests. It is preparation evidence,
not installed-package qualification. Browser package qualification remains in the later stage.


### Phase 3 corpus corrections and qualification boundary (2026-11-06)

The OPENGL33 gate passes all 43 drawing cases plus both policy cases under window-free
llvmpipe, Mesa 25.0.7. Its previously failing vertex-texture fixture used fixed coverage counts
on a 64x64 caller backbuffer; it now renders to a fixed 256x256 target with the same thresholds.
The six desktop compiled centroid cases now put their triangle edge at D3D9 pixel coordinate
x=1.1 so pixel 1 is partially covered; the old edge at x=1.6 left that pixel clear. Both complete
GL module binaries pass in that software configuration. These are desktop GL measurements,
not an ES/WebGL or installed-package qualification claim. The released default selector confines
promotion to builds with OPENGL33 and no OPENGLES3/WEBGL2 identity; explicit ON/OFF still wins.

Shared legacy fixtures now initialize source texture registers explicitly, use bounded
ps_1_x TEXCOORD data, and enable a real mip chain when testing implicit LOD. Their original
pixel assertions remain. The DirectX9 owner run exposed additional native vertex-sampler,
legacy sampler-dimension and bump-stage plumbing gaps. Their repair and a precise native
predicate-profile refusal envelope are review patches awaiting a reserved MinGW build and
runtime verification; DirectX9 has no new corpus pass or default promotion. At the old dependency pin this phase retained eight
SPIR-V opcode refusals. The 2026-11-06 fork pin now accepts BEM and TEXDEPTH and retains the
other six named refusals; that changes the current corpus and requires a fresh integration run.


### Phase 4 native build status (2026-11-06)

The owner has now repeated the corrected OPENGL33 gates on AMD radeonsi: policy2/corpus43,
centroid6 and both complete module binaries pass. OPENGLES3/WEBGL2 remain separately
unqualified. One shared EASYGL option means an unconditional default ON would enable all
three identities; the desktop-scoped default therefore keeps fresh ES/browser and
mixed EasyGL selections OFF while retaining explicit ON/OFF choices. The default is scoped to fresh OPENGL33 selections; desktop installed ON/OFF acceptance remains open.

The DirectX9 repair patch is now applied and both MinGW test binaries compile after fixing
the native refusal test's namespace qualifications. Four repaired-path mutants have been built
and preserved for owner execution. Earlier DX9 runs after the failed compile used old binaries;
they cannot establish that these repairs pass. Current native policy3/corpus43, full binaries,
installed/OFF routes and meaningful mutation failures still need owner runtime results.


### Fork-pin corpus update (2026-11-08)

The enabled SPIR-V policy case executes the pixel/depth contracts for all eight legacy
instructions, including TEXM3X2DEPTH's literal plain/signed/mixed/zero depth witnesses.
OFF builds still refuse all eight fixture types by the CompiledEffects capability before
translation. The combined matrix/register depth contract retains its original assertions and
adds those full-image matrix probes; `LegacyRegisterDepthOutput` independently checks TEXDEPTH.
No SPIR-V missing-emitter catch remains in the corpus. Current strict native/browser corpus denominator:
**44**, with policy counts unchanged (two, or three for SPIR-V/native-refusal routes).

The historical 43-case results above remain measurements of that earlier inventory. They do not
establish a 44-case pass. The pinned fork's IR/native qualification does not establish CNA
uniform packing, resource lifetime, shadercross or browser WGSL pixels; rerun enabled native and
browser corpus/whole-module gates and installed-package acceptance before promotion.


### Phase 5 DirectX9 qualification blocker (2026-11-06)

The owner ran the newly built native controls on Wine/DXVK 3.1.1: policy3 passes, the shared
corpus is42/43, the focused DirectX9 gate and full Renderer binary pass, and Graphics has one
failing case, LegacyBumpEnvironment. The four preserved native mutants fail their witnesses.
That case has four failing assertions: both TEXBEM off-diagonal lookups and the red/green
channels of ps_1_4 BEM arithmetic. The diagonal, zero-matrix/LOD, luminance and retained-state
checks in the same case pass. This is not a native family qualification pass.

The independent TEXBEM equation is u'=u+m00*du+m10*dv and v'=v+m01*du+m11*dv. CNA retains
native00/01/10/11 state IDs and passes FLOAT bit patterns with std::bit_cast<DWORD>; a numeric
cast or compensating matrix transpose would violate that contract. The current host's results
match swapped off-diagonals, but the exact translator/driver fault has not been isolated by
native API readback or real Windows hardware. Keep the existing pixel assertions strict and
DIRECTX9 default OFF. TEXBEM is a real D3D9 instruction (ps_1_1 through1_3); do not invent a
native opcode refusal or count an environment skip as a corpus pass. BEM is the separate ps_1_4
arithmetic instruction. A corrected host or independent conforming native run is needed before
this remaining case can qualify the family.


### Desktop-scoped EasyGL default policy
+
+`CNA_EASYGL_COMPILED_EFFECTS` is a single shared cache option and compile definition. A fresh
+selection containing OPENGL33 and no OPENGLES3/WEBGL2 defaults ON. A fresh OPENGLES3, WEBGL2 or
+mixed EasyGL selection defaults OFF, including OPENGL33+OPENGLES3, because a single flag cannot
+default just one profile ON in that binary. Other renderer identities in the set do not alter the
+EasyGL default. Explicit OFF remains an opt-out, and explicit ON can enable the unqualified
+profiles for qualification. Existing cache values are preserved, including across renderer changes.
+
+The owner retained real AMD policy2/corpus43, centroid6 and complete GL module passes. This
+qualifies the desktop corpus, not GLES3's GLSLES3 driver behavior or WebGL2's browser translation,
+exception ABI, texture/MSAA restrictions and installed browser package. Those profiles need their
+own ON/OFF policy, full shared corpus, lifetime/mutation and installed-consumer results before a
+family-wide unconditional default may be adopted. Installed desktop ON/OFF qualification remains
+a separate release acceptance step; the default patch itself does not make an installed pass claim.
