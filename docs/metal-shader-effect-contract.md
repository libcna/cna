# Metal custom shader-effect contract

## Current status

Custom effects are supported on Metal as a **SpriteBatch facility** written in the Metal Shading
Language (`plans/plan_apple_m4.md` `AM4-077`):

- `SupportsCapability(CNA::GraphicsCapability::CustomEffects)` returns `true`;
- `GetShaderDialectEXT()` is `ShaderDialectEXT::Msl`, and `SupportsShaderLanguageEXT` accepts MSL
  vertex and fragment stages only;
- `ShaderEffect(device, vertexSource, fragmentSource)` compiles each source as its own runtime
  `MTLLibrary`. A source that does not compile gives an effect whose `IsEffectValid()` is false and
  whose `GetCompileErrorEXT()` carries the Metal compiler's message; construction does not throw;
- `SpriteBatch.Begin(..., effect)` draws the batch's sprites through the effect;
- an ordinary 3D draw with a custom effect is refused with `System::NotSupportedException`, as on
  Direct3D 11 and Vulkan, whose custom-effect facilities are SpriteBatch-shaped too;
- `ExecutesShaderEffectSourceEXT()` stays `false`: that query is about the GLSL the CNAEXT engine
  layer writes, which Metal does not run.

Evidence: `Metal_SpriteBatch_CustomEffect` (registered with `MTL_DEBUG_LAYER=1` and
`MTL_SHADER_VALIDATION=1`) on a Mac mini M4, macOS 27: compilation, the inversion through the
colour slot, placement through the automatic transform, the matrix and scalar slots, the batch's
blend state, a render-target destination read back through `GetData`, an invalid source, and
effect lifetime (a disposed effect leaves the stock pipeline working and a new effect drawing).

## The interface a custom effect is written against

Each source declares exactly one stage function; its name is free, because the renderer reads the
library's single function name instead of requiring one.

| Binding | Contents | Set by |
|---|---|---|
| vertex `buffer(0)` | the sprite's six vertices: `float2 position; float2 uv; float4 color` (32 bytes) | SpriteBatch |
| vertex `buffer(1)` | `float2 scale; float2 offset` mapping sprite positions to clip space, letterbox included | SpriteBatch |
| `buffer(2)`, both stages | `float4x4` (column-major) | `SetUniformMat4` |
| `buffer(3)`, both stages | `float4` | `SetUniformVec4` / `Vec3` / `Vec2` |
| `buffer(4)`, both stages | `float` | `SetUniformFloat` / `SetUniformInt` |
| fragment `texture(0)`, `sampler(0)` | the sprite's texture and the batch's sampler state | SpriteBatch |

The uniform setters ignore their `name` argument -- MSL has no named loose uniforms, which is the
same fixed-slot answer Direct3D 11 and Vulkan give. A uniform set between two `Draw` calls of one
batch takes effect on the next sprite, because Metal's SpriteBatch issues one draw per sprite.
`SetUniformFloatArray`/`Vec2Array` and the extra texture units of `BindTexture` (`METAL-147`) are not
implemented.

The pipeline is built for the active blend state and attachment layout (BGRA8 colour,
`Depth32Float_Stencil8`); a valid effect whose pipeline cannot be built throws from the draw rather
than being replaced by the stock shader. With several render targets bound (`AM4-097`) only the
first receives the effect's output: attachments 1 and up are declared with an empty write mask, so a
fragment function's `[[color(1)]]` and higher outputs are discarded. No Metal draw can write those
attachments yet, although `SetRenderTargets` binds and clears them. An effect that is not valid draws with the stock sprite
pipeline, as Direct3D 11's SpriteBatch does.

## History

Historical `feature/metal` implemented this SpriteBatch-scoped path and compiled it on the
`macos-14` runner, but the production run (GitHub Actions `29814126178`) could not prove its pixels:
readback returned the clear colour for this and unrelated 2D/3D tests alike (`METAL-258`). The path
was therefore disabled until it had post-adaptation Apple evidence. `AM4-025` repaired readback and
`AM4-028` the 3D transform; `AM4-077` supplied the evidence above and enabled the facility.
