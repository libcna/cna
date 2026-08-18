# Who deletes what in `CNA::Graphics`

`plan_modern.md` **MOD-1903**. One table, so that no class in the engine layer is ambiguous about
ownership. Derived by reading every member of every public header in
`modules/graphics-ext/include/CNA/Graphics/` on 2026-08-18; re-derive it if that set changes.

## The three shapes, and nothing else

The layer uses exactly three ownership shapes. If a future class needs a fourth, that is a design
decision worth arguing rather than an implementation detail.

| Shape | Spelling | Who deletes | What it means for the caller |
|---|---|---|---|
| **Owned** | `std::unique_ptr<T>` | The holder, in its destructor | The caller never sees the pointer and cannot outlive it |
| **Borrowed** | `T*`, settable, may be null | Nobody in this layer — the caller | The caller must keep the object alive for as long as it stays set |
| **Attached** | `T&`, constructor-injected, never rebound | Nobody in this layer — the caller | The caller must outlive the whole object |

**Attached** is always `GraphicsDevice&` in practice: every class in the layer that touches the GPU
takes the device by reference at construction and keeps it for life. That is the single most
important ownership fact about the layer — *a `CNA::Graphics` object must not outlive its
`GraphicsDevice`*, and none of them try to detect it if it does.

## The owned/borrowed pair idiom

Three classes accept an object the caller owns *or* build one themselves, and they all spell it the
same way: a `unique_ptr` member that may be empty, plus a raw pointer that names whichever one is in
use. Reading the raw pointer is always correct; the `unique_ptr` only answers "must I delete it".

| Class | Borrowed handle | Owned slot | Set by |
|---|---|---|---|
| `EffectPass` | `effect_` | `ownedEffect_` | `EffectPass(device, Effect&)` borrows; the source-string constructor owns |
| `Skybox` | `environment_` | `ownedEnvironment_` | `setEnvironment(TextureCube*)` borrows; `setOwnedEnvironment(unique_ptr)` owns |
| `PostProcessChain` | `passes_` (the running order) | `ownedPasses_` | `addPass(PostProcessPass*)` borrows; `addOwnedPass(unique_ptr)` owns |

## Per class

| Class | Owns | Borrows | Attached |
|---|---|---|---|
| `AsciiPass` | `AsciiPostProcessEffect` | — | `GraphicsDevice&` |
| `AsciiPostProcessEffect` | — | — | `GraphicsDevice*` — see the note below |
| `AutoExposureEXT` | `ComputeShader`, `StorageBufferT<float>` | — | `GraphicsDevice&` |
| `BlitPass` | `FullscreenPass` | — | `GraphicsDevice&` (through `FullscreenPass`) |
| `BloomPass` | `FullscreenPass`, 4 × `ShaderEffect` | — | `GraphicsDevice&` |
| `CascadedShadowMap` | `RenderTarget2D` atlas, `ShaderEffect` | — | `GraphicsDevice&` |
| `ComputeShader` | `IComputeShaderRenderer` | — | `GraphicsDevice&` |
| `CubeShadowMap` | `RenderTargetCube`, `ShaderEffect` | — | `GraphicsDevice&` |
| `DepthNormalPrepass` | 2 × `RenderTarget2D`, 2 × `ShaderEffect` | — | `GraphicsDevice&` |
| `EffectPass` | `ownedEffect_` (sometimes), `FullscreenPass` | `effect_` | `GraphicsDevice&` |
| `EnvironmentProcessor` | — (returns owned results) | — | `GraphicsDevice&` |
| `FullscreenPass` | `SpriteBatch` | — | `GraphicsDevice&` |
| `FxaaPass` | `FullscreenPass`, `ShaderEffect` | — | `GraphicsDevice&` |
| `InstancedRendererEXT` | 2 × `DynamicVertexBuffer` | `ModelMeshPart* part_` | `GraphicsDevice&` |
| `LodGroupEXT` | — | the `ModelMeshPart*` in each level | — |
| `PbrMaterial` | — | 7 × `Texture2D*` | — |
| `PostProcessChain` | `ownedPasses_`, `copyPass_` | `passes_` | `GraphicsDevice&` |
| `RenderPipeline` | scene `RenderTarget2D`, bloom/tonemap/FXAA/SSAO passes | `skybox_`, `shadowMap_`, `sceneDepth_`, `sceneNormals_`, `userPasses_` | `GraphicsDevice&` |
| `RenderTargetPool` | every `Entry` it hands out | — | `GraphicsDevice&` |
| `ScopedRenderTarget` | — | the targets it restores | `GraphicsDevice&` |
| `ShaderEffectFactory` | — (returns owned results) | — | `GraphicsDevice&` |
| `ShadowMap` | `RenderTarget2D`, 2 × `ShaderEffect` | — | `GraphicsDevice&` |
| `Skybox` | `ownedEnvironment_` (sometimes), dummy `Texture2D`, `ShaderEffect`, `FullscreenPass` | `environment_` | `GraphicsDevice&` |
| `SpotShadowMap` | `RenderTarget2D`, `ShaderEffect` | — | `GraphicsDevice&` |
| `SsaoPass` | `FullscreenPass`, 2 × `ShaderEffect`, noise `Texture2D` | — | `GraphicsDevice&` |
| `StorageBuffer` | `IStorageBufferRenderer` | — | — (holds no device) |
| `TonemapPass` | `FullscreenPass`, `ShaderEffect` | — | `GraphicsDevice&` |

Every class marked "Owns" in that table releases exactly what it owns in its destructor, and none of
them own anything a caller can also see. There is no `shared_ptr` anywhere in the layer, and no
class deletes something it did not create.

## Two things that are inconsistent, and why they stay

- **`AsciiPostProcessEffect` stores `GraphicsDevice*`, not `GraphicsDevice&`.** It is the one class
  here that predates the engine layer and the `MOD-6` naming rules, and the pointer is a leftover of
  that, not a design decision: its constructor takes `GraphicsDevice&` like everything else and
  immediately takes its address, so the member is never null and the class is not copy-assignable
  in any way the reference would have prevented. Changing it is a mechanical edit with no
  behavioural effect and no caller-visible one; it is recorded here rather than done in an ownership
  review, because the review's job is to say what is true.
- **`PbrMaterial` borrows seven raw texture pointers.** This is the interface a material needs: the
  textures belong to the content pipeline and are shared between materials, so a material that owned
  them would either duplicate them or need reference counting the rest of the layer does not use.
  The rule for a caller is the ordinary borrowed one — keep the textures alive as long as the
  material is bound.

## What a caller has to remember

Three sentences, and they cover the whole layer:

1. Nothing in `CNA::Graphics` may outlive its `GraphicsDevice`.
2. Anything you pass in as a raw pointer, you keep alive; anything you pass as `unique_ptr`, you
   have handed over.
3. Nothing here is thread-safe, and nothing here is reference-counted.
