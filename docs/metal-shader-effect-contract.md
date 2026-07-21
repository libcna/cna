# Metal custom `ShaderEffect` contract

`METAL-146`/`METAL-152` (`plan_metal.md` Phase 14). This documents the fixed contract a custom MSL
vertex+fragment pair must follow to work as a `SpriteBatch` custom effect on the Metal backend
(`ShaderEffect` constructed with raw MSL source, then passed to `SpriteBatch::Begin(sortMode,
blendState, samplerState, depthStencilState, rasterizerState, &effect)`).

## Scope: a `SpriteBatch`-only facility, not arbitrary 3D

This mirrors the same scope `VulkanEffectBackend`/`D3D11EffectBackend`/`D3D12EffectBackend` already
commit to (confirmed by reading each directly, not assumed): a custom `ShaderEffect` here draws
2D sprites through `SpriteBatch` with a **fixed** vertex layout, matching Metal's own existing
`Sprite2D` pipeline exactly. It is *not* the arbitrary-`VertexElement` 3D custom-shader facility
`plan_metal.md`'s still-open `METAL-26`/`METAL-27` generic descriptor builder would eventually
unblock — that broader capability is `EasyGLGraphicsBackend`'s own unique extra scope (OpenGL's
attribute binding is inherently layout-flexible; Vulkan/D3D11/D3D12/Metal's structured pipeline
objects are not).

## Compiling: two separate MSL sources, no fixed entry-point name

`ShaderEffect(device, vertSrc, fragSrc)` compiles `vertSrc` and `fragSrc` as two **independent**
`MTLLibrary` objects (matching `IEffectBackend::CompileProgram(vertSrc, fragSrc)`'s own
two-separate-strings signature — unlike a single combined `.metal` file). Each source **must
declare exactly one function** (a `vertex` function in `vertSrc`, a `fragment` function in
`fragSrc`). There is no fixed required name (e.g. no mandatory `main0`) — the backend reads
`MTLLibrary.functionNames` back and uses whatever single function it finds, the same freedom
GLSL/HLSL's own single-implicit-entry-point convention already gives every other backend's custom
effects.

## Vertex data contract (buffer index 0)

Every sprite quad is 6 vertices, each shaped exactly like `Sprite2DVertex` on every other backend:

```metal
struct V2In {
    float2 position;  // screen-space pixel coordinates (pre-transform), NOT normalized device coords
    float2 uv;
    float4 color;      // straight (non-premultiplied) RGBA, 0..1 range
};
```

Read manually via `const device V2In* v [[buffer(0)]]` and `v[vid]` (no `MTLVertexDescriptor`/
`[[stage_in]]` on the input side — matches the stock `cna_v2d` shader's own established
convention exactly, avoiding any fixed-vertex-descriptor requirement).

## Automatic transform (buffer index 1)

```metal
struct U2D { float2 scale; float2 offset; };
```

Set automatically by `MetalSpriteBatch::Draw()` on every draw call — the effect author never sets
this. `ndc = position * scale + offset` reproduces the exact same letterbox/virtual-resolution-aware
NDC transform the stock `cna_v2d` shader uses (`plan_metal.md METAL-157/158`). This is a deliberate
divergence from `VulkanEffectBackend`/`D3D11EffectBackend`'s own `vpSize`-only convention (a raw
viewport-size vec2, predating this project's own letterbox fix) — copying that convention verbatim
would silently reintroduce the exact letterboxing bug `METAL-157`/`158` already fixed, for any
Metal custom-effect sprite draw.

## User uniforms (buffer indices 2, 3, 4)

MSL has no GLSL-style named-uniform reflection simple enough to build a genuine
`SetUniformFloat(name, ...)` on top of (`MTLRenderPipelineReflection` is argument-table
introspection, not a name→offset uniform map). Matching `VulkanEffectBackend`'s SPIR-V
push-constant precedent and `D3D11EffectBackend`'s HLSL constant-buffer precedent (both hit the
identical problem and both chose the same answer), every `ShaderEffect::SetUniformXxx()`'s `name`
parameter is **ignored** — this is a fixed, documented, slot-based contract instead:

| Buffer index | MSL type | Written by |
|---|---|---|
| `buffer(2)` | `constant float4x4&` (64 bytes) | `SetUniformMat4(name, matrix)` |
| `buffer(3)` | `constant float4&` (16 bytes) | `SetUniformVec4/Vec3/Vec2(name, ...)` (unset trailing components keep their last-written value) |
| `buffer(4)` | `constant float&` (4 bytes) | `SetUniformFloat(name, value)` / `SetUniformInt(name, value)` |

Deliberately **three separate buffers**, not one combined struct like Vulkan/D3D11's single
128-byte push-constant/constant-buffer block — Metal's `setVertexBytes:atIndex:`/
`setFragmentBytes:atIndex:` has no push-constant-style single-range restriction, so each slot is
its own natural, unpadded Metal type (`float4x4`/`float4`/`float`). This avoids any
`constant`-address-space struct-padding ambiguity a combined struct would put on the shader
author to get exactly right.

These three buffers are bound identically to **both** the vertex and fragment stage on every draw
(a shader function that does not declare a given buffer index simply never reads it — Metal does
not require a bound-but-unused buffer to be declared).

`SetUniformFloatArray`/`SetUniformVec2Array`/`BindTexture`/`BindTextureCube`/`BindTexture3D` are
**not implemented** (inherit `IEffectBackend`'s own no-op default) — matching
`VulkanEffectBackend`/`D3D11EffectBackend`'s identical scope boundary. Texture unit 0 is always
driven by the caller (`SpriteBatch`'s own texture parameter), per `IEffectBackend::BindTexture()`'s
own doc comment.

## Blend state: real, not hardcoded

Unlike the Vulkan/D3D11/D3D12 precedent (each hardcodes a fixed alpha blend inside the custom
pipeline, silently ignoring whatever `BlendState` the game's own `SpriteBatch.Begin(sortMode,
blendState, ...)` call requested), Metal's custom-effect pipeline is built against the real,
currently-applied `BlendState` — matching real XNA/FNA behavior, where `blendState` and `effect`
are independent `Begin()` parameters. `MetalEffectBackend::pipelineFor()` rebuilds its own
single-entry pipeline cache whenever the applied blend state changes.

## Color attachment format

Every custom pipeline hardcodes `MTLPixelFormatBGRA8Unorm` for its color attachment and
`MTLPixelFormatDepth32Float_Stencil8` for depth/stencil, matching every other pipeline this backend
builds (`plan_metal.md` narrative item 77 confirms every render target this backend creates is
forced to these exact same formats, so this is not a render-target-vs-backbuffer gap).

## Per-draw-call re-evaluation (a deliberate improvement over the D3D11/Vulkan precedent)

`MetalSpriteBatch::Draw()` re-reads this effect's current pipeline/uniform bytes on **every**
`sb.Draw(...)` call, not once per `Begin()`/flush the way D3D11/Vulkan's own batched-flush
architecture does. Metal's `SpriteBatch` issues one immediate draw per sprite rather than batching
a whole `Begin`/`End` block into a single flush, so a `SetUniformXxx()` call between two `Draw()`s
in the same `Begin`/`End` genuinely takes effect starting with the very next sprite — more
fine-grained than the D3D11/Vulkan precedent's own once-per-batch limitation, not a narrower
compatibility gap.

## Worked example

See `examples/metal_spritebatch_customeffect_test.cpp` (registered as the `Metal_SpriteBatch_
CustomEffect` `ctest`) for a complete, real MSL vertex+fragment pair (RGB-inversion) that exercises
every part of this contract, including `SetUniformVec4()`'s real `buffer(3)` wiring.
