# Who deletes what in `CNA::Graphics`

`plans/plan_modern.md` **MOD-1903**. One table, so that no class in the engine layer is ambiguous about
ownership. Derived by reading every member of every public header in
`modules/graphics-ext/include/CNA/Graphics/` on 2026-08-18; re-derive it if that set changes.

## The three public shapes, and nothing else

The layer's public API uses exactly three ownership shapes. If a future public API needs a fourth,
that is a design decision worth arguing rather than an implementation detail. Renderer-internal
records may use shared lifetime identity solely to retain already accepted work, as required by
ADR 0001; no public `shared_ptr` is exposed.

| Shape | Spelling | Who deletes | What it means for the caller |
|---|---|---|---|
| **Owned** | `std::unique_ptr<T>` | The holder, in its destructor | The caller never sees the pointer and cannot outlive it |
| **Borrowed** | `T*`, settable, may be null | Nobody in this layer — the caller | The caller must keep the object alive for as long as it stays set |
| **Attached** | `T&`, constructor-injected, never rebound | Nobody in this layer — the caller | The caller must outlive the whole object |

**Attached** is always a `GraphicsDevice` in practice: most classes retain a reference; tracked
`GraphicsResource` subclasses retain the base class's non-owning pointer. Every GPU-facing class
takes the device by reference at construction. That is the single most important ownership fact
about the layer — *a `CNA::Graphics` object must not outlive its `GraphicsDevice`*. A tracked
resource is disposed by the device, but its C++ wrapper must still be destroyed before the device
object's storage disappears.

Public ownership and in-flight native lifetime are deliberately different. A deferred renderer may
retain an internal native resource record after a public wrapper is disposed, solely until the
accepted command and its completion token are finished. This is required by
`docs/adr/0001-modern-gpu-ordering-lifetime.md`; it neither makes the public object shared nor lets a
caller keep using it after disposal.

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
| `Texture2DArray` | shared internal `ITexture2DArrayRenderer` record | — | `GraphicsDevice*` through `GraphicsResource` (tracked) |
| `TonemapPass` | `FullscreenPass`, `ShaderEffect` | — | `GraphicsDevice&` |

Every class marked "Owns" in that table releases exactly what it owns in its destructor or
`Dispose()` path, and none of them own anything a caller can also see. `Texture2DArray`'s shared
record is deliberately internal: today the wrapper is its only owner; future accepted bindings may
retain that record without retaining or dereferencing the public wrapper. There is no public
`shared_ptr` ownership in the layer, and no class deletes something it did not create.

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

Four rules cover the whole layer:

1. Nothing in `CNA::Graphics` may outlive its `GraphicsDevice`.
2. GPU-facing use and disposal are serialized on the device's graphics thread; the public objects
   are not concurrently thread-safe.
3. Anything you pass in as a raw pointer, you keep alive; anything you pass as `unique_ptr`, you
   have handed over.
4. Public objects are not reference-counted; internal renderers may retain only the native record
   needed by already accepted work.
