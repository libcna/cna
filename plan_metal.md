# Native Metal Graphics Backend

## Scope

`METAL` is a native Apple Metal backend. SDL is used only for CNA's existing window lifecycle and
`SDL_Metal_CreateView` / `SDL_Metal_GetLayer` platform glue. Rendering itself must never be routed
through SDL_Renderer or SDL_GPU. Per `cmake/BackendSelection.cmake`'s own gate message ("METAL
backend only builds when targeting macOS/iOS/tvOS"), the intended platform scope is all three Apple
OS families, not macOS alone — see Phase 29 below, which this document's previous revision did not
mention at all.

**Status legend** (matches this project's established convention in `plan_dx3.md`/`plan_webgpu.md`):
✅ implemented *and verified against its stated acceptance criteria* (a real CTest passing on the
macOS CI job or a physical Mac); 🟨 code exists and is believed correct by source-level review, but
has **not** been build- or runtime-verified — this Linux machine has no Apple toolchain, so nothing
in this backend can honestly be marked ✅ from here; ⬜ not implemented.

## 2026-07-19 revision — why this document changed

This plan was a 20-bullet sketch (62 lines) with no task IDs, no phases, and no traceability to the
actual implementation. Meanwhile the real `MetalGraphicsBackend` is 420 lines across one `.hpp` and
one `.mm` — versus EasyGL's 5,362, D3D11's 4,521, WebGPU's 10,411, SDL_GPU's 9,877, Vulkan's 16,144,
D3D9's 16,847, and Bgfx's 20,904. Metal implements exactly 5 fixed shader pipelines (colored-16,
textured-20, colortex-24, normaltex-32, sprite-2d), reads none of `GpuDrawParams`' ~35 effect fields
beyond `texture0`, and returns `nullptr`/no-ops for render targets, cube/3D textures, occlusion
queries, custom effects, MRT, and per-slot sampler state. This revision reads the full
`IGraphicsBackend` contract (1,023 lines, `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`)
and cross-references EasyGL's actual method inventory (`EasyGLGraphicsBackend::SelectProgram()`,
`BindDrawParams()`, and its 12 `Ensure*Program()` shader-variant builders) to turn "eventually cover
CNA graphics as well as EasyGL" into ~255 concrete, individually actionable tasks across 30 phases,
each citing the exact interface method, `GpuDrawParams` field, or EasyGL/Vulkan/WebGPU reference
implementation it ports from — not a speculative wishlist.

**On the requested task count**: the request was for as many tasks as the real gap needs, "even if
1000." ~255 is the actual number of non-redundant, individually verifiable engineering tasks this
audit found — mechanically padding further (e.g. one row per enum *value* instead of per enum,
or pre-registering hundreds of not-yet-real `Check X` sub-assertions for code that doesn't exist
yet) would manufacture false precision, which is exactly the overclaiming failure mode this
project's own history (`plan_dx3.md`'s multiple correction notices, this repo's `CLAUDE.md`/memory
of "don't claim plan complete") has repeatedly had to catch and fix. Every phase below is expected
to grow further Check-letter-level sub-tasks once its code actually lands and needs a CTest written
against it — exactly how `plan_dx3.md` Phase X4 (tasks DX3-30..39) and `plan_webgpu.md` grew their
own detail, incrementally, not upfront.

**What actually landed in this revision** (source-complete on this Linux machine, **not yet
build-verified** — no Apple toolchain here; needs the `metal-macos-ci.yml` job or a physical Mac):
- `ApplySamplerState(slot, filter, addressU, addressV, maxAnisotropy)` — previously entirely
  unimplemented (`IGraphicsBackend`'s base no-op). Now backed by a real `MTLSamplerState` cache
  (`Impl::samplerFor()`, keyed by filter/address/anisotropy) with 16 texture-unit slots, wired into
  both `drawMetal3D`'s texture-unit-0 fragment binding and `MetalSpriteBatch::Draw()` (which
  previously had a dead `filter_` field it read but never used, and no `SetSamplerAddressMode`
  override at all). See Phase 1, `METAL-1`/`METAL-2`.
- `SupportsCapability(GraphicsCapability)` — previously unimplemented, meaning the inherited
  `IGraphicsBackend` default (unconditional `true`) was actively lying about 3 capabilities this
  backend does not yet have: `MultipleRenderTargets` (`SetRenderTargets()` still only binds the
  first target), `OcclusionQuery` (`CreateOcclusionQuery()` still returns `nullptr`), and
  `CustomEffects` (`CreateEffectBackend()` still returns `nullptr`). Now overridden to answer `false`
  for exactly those three and defer to the (correct) default otherwise. See Phase 20,
  `METAL-192`/`METAL-195`/`METAL-196`/`METAL-197`.

## 2026-07-19 (continued, autonomous overnight session) — Phase 1/2/4/5 real progress

The project owner asked for autonomous continuation overnight, explicitly accepting that no
Apple/macOS toolchain is available on this Linux machine to build/verify with — so, per this
document's own status-legend rules, **everything below is still 🟨** (source-complete, reasoned
carefully against the exact XNA ordinal values in this repo's own `.hpp` files and cross-referenced
line-for-line against EasyGL's/Vulkan's already-shipped, already-tested equivalent mapping
functions — never guessed), not ✅. All of it needs the `metal-macos-ci.yml` job or a physical Mac
before any status here can honestly change.

**Landed this pass:**
- **Real enum mappings** (`METAL-6`–`METAL-13`): `metalCompareFunction`/`metalStencilOp`/
  `metalBlendFactor`/`metalBlendOp`, each cross-checked against `VulkanGraphicsBackend`'s and
  `EasyGLGraphicsBackend`'s own already-tested `ToVk*`/`ToEasyGL*` mapping functions (not
  independently guessed) — including confirming XNA's `StencilOperation.Increment` wraps while
  `IncrementSaturation` clamps, and that `Blend.BlendFactor`/`InverseBlendFactor` get no special
  RGB-vs-Alpha-channel treatment (matching an established, deliberate simplification all 3 other
  backends already share, not a gap unique to Metal). Also fixed `PrimitiveType.PointListEXT`
  (ordinal 4), previously silently mismapped to `Triangle`/`count*3` in both `metalPrimitive()` and
  `primitiveVertexCount()`.
- **Real depth/stencil state** (`METAL-7`/`METAL-9`/`METAL-10`): `ApplyDepthStencilState` now wires
  all 16 of its parameters — previously only `depthEnable`/`depthWriteEnable`/`referenceStencil`
  had any effect, with `depthFunc` hardcoded to `LessEqual` and all 8 stencil-op/mask/two-sided
  fields silently dropped. Front-face stencil carries XNA's normal fields; back-face carries the
  `CounterClockwise*` fields when `TwoSidedStencilMode` is set, else mirrors front — **deliberately
  NOT applying `VulkanGraphicsBackend::FillDepthStencilState`'s own empirically-found front/back
  swap**, since that swap was a documented compensation for Vulkan's own NDC Y-flip (absent from
  Metal's vertex shaders in this codebase) — but this reasoning has explicitly **not** been verified
  on real Metal hardware and is flagged as such in-code, not asserted as fact.
- **Real per-`BlendState` pipeline selection** (`METAL-6`/`METAL-24`, a simplified first pass at
  the Phase 2 pipeline-state cache, `METAL-22`/`METAL-23`/`METAL-25`): `ApplyBlendState` was a
  complete no-op before this pass — every pipeline hardcoded the same straight-alpha blend
  regardless of the game's actual requested `BlendState`. The 5 fixed named pipeline fields
  (`pipe3Color` etc.) are replaced with a `PipelineKind` enum (one entry per concrete shader+
  vertex-layout combination, deliberately **not** yet the fully generic `VertexElement`-hashed key
  `METAL-27` describes — a lower-risk design to get right without a compiler, explicitly scoped
  down from the original Phase 2 task text) plus a cache keyed by `(PipelineKind, BlendKey)`,
  built lazily on first use. `SetBlendFactor` (previously entirely unimplemented, not even declared
  in the header) now wires `setBlendColorRed:green:blue:alpha:`. Found and fixed a real,
  independent pre-existing bug along the way: `clear()`'s fresh-encoder path never reapplied
  cull/fill/depth-bias/stencil-reference (only `ensureFrame()`'s did) — both now share one
  `applyTrackedEncoderState()`.
- **`DiffuseColor`/`VertexColorEnabled`/`AlphaTest` actually reach the shader** (`METAL-35`–
  `METAL-37`, `METAL-51`–`METAL-55`): previously **zero** `GpuDrawParams` fields beyond `texture0`
  affected rendering at all. A new `UMaterialParams` uniform (mirrored byte-for-byte between the
  MSL shader source and a plain C++ struct — 3 consecutive `float4`s, deliberately avoiding any
  `float3` member to sidestep MSL `constant`-address-space padding rules entirely) now carries
  `diffuseColor`/`alphaTest`/`vertexColorEnabled` into every unlit fragment shader. AlphaTestEffect
  needed **no separate pipeline at all**: with `alphaTest`'s documented default (`{0,0,1,1}`)
  the discard check is provably a no-op, so it was folded unconditionally into the existing
  textured fragment shaders — the same design choice `EasyGLGraphicsBackend::
  EnsureDualTextured3DProgram()`'s own fragment source already makes (confirmed by reading it,
  not assumed).
- **DualTextureEffect** (`METAL-58`–`METAL-61`): a real `cna_f3d_dualtex` fragment shader, ported
  from FNA's actual `DualTextureEffect.fx` `PSDualTexture` (`/rv/data/library/github.com/FNA-XNA/
  FNA/src/Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx`) — including the real `color.rgb
  *= 2` lightmap-style doubling factor on the *first* texture only (already found, fixed, and
  pixel-verified on EasyGL/Vulkan/Bgfx per `docs/dualtextureeffect-support.md` Task 383, ported
  here rather than re-derived). Confirmed CNA's cross-backend convention deliberately samples both
  textures at one shared UV (stride 20/24), not FNA's real separate `TexCoord`/`TexCoord2` — traced
  through `WebGPUGraphicsBackend`'s shipped dispatch logic, not assumed from the plan's own earlier
  (now confirmed accurate) description.
- **Real window↔logical coordinate transforms and the 2D letterbox bug** (`METAL-153`–`METAL-159`,
  `METAL-182`–`METAL-184`): `TransformWindowToLogical`/`TransformLogicalToWindow` were entirely
  unimplemented — a real, currently-shipping mouse-input bug on any letterboxed/scaled window, not
  just a graphics gap. Ported `SdlGpuGraphicsBackend::ComputeLogicalViewport()`'s already-shipped
  letterbox/overscan/stretch/native/fixed-height-dynamic-width math near-verbatim rather than
  re-deriving it. Found and fixed a second, independent, previously-invisible bug along the way:
  the constructor never read `args.presentationMode` at all (silently defaulted to `Letterbox`
  instead of the real XNA-matching `FixedHeightDynamicWidth` default every other backend's
  constructor already forwards) — harmless before this pass since nothing consumed the field yet,
  now load-bearing. Separately, `cna_v2d`'s NDC math used raw physical drawable pixels, completely
  ignoring virtual resolution — sprites at "virtual" coordinates rendered in the wrong place/scale
  whenever the physical window size differed from the requested virtual resolution; replaced with a
  scale+offset uniform derived from the same logical-viewport math, hand-verified algebraically to
  degrade to the exact old formula when no virtual resolution is set (zero behavior change for
  every existing draw that isn't using it). `SpriteBatch.SetTransformMatrix` (previously a
  no-op) is now applied CPU-side as a 2D point transform on the screen-space quad corners. Real
  per-`BlendState` 2D blending (`METAL-184`) turned out to need no new code at all: `Sprite2D` is
  just another `PipelineKind` sharing the same `(kind, BlendKey)`-keyed cache every 3D pipeline
  already uses.
- **`TextureCube`/`Texture3D`** (`METAL-120`/`121`/`123`/`124`): both entirely absent before this
  pass (`nullptr` via `IGraphicsBackend`'s own base default). Deliberately ignore `surfaceFormat`
  and hardcode RGBA8Unorm, matching `EasyGLTextureCubeBackend`'s/`EasyGLTexture3DBackend`'s own
  established convention exactly (confirmed by reading their constructors — the parameter is
  literally named `int /*surfaceFormat*/` there too), not a new gap. This unblocked `METAL-65`
  (`EnvironmentMapEffect`'s cube-map bind), though Phase 6 turned out to have a second, deeper
  blocker — see its own note below.
- **BasicEffect per-pixel lighting/fog/specular/emissive** (`METAL-38`/`40`/`42`–`44`/`46`/`47`,
  the highest-leverage remaining piece — Phase 6/8 both depend on it): ported
  `EnsureLit3DProgram()`'s real GLSL line-for-line into MSL. The normal-matrix formula
  (`transpose(inverse(world3x3))` via the cofactor/determinant shortcut) was independently
  re-derived and hand-verified against EasyGL's own `nm[9]` construction, not just transcribed —
  EasyGL's array is `inv(M)` stored row-major, fed to a column-major GL uniform upload with no
  transpose flag, which is *why* it ends up as `transpose(inv(M))` without an explicit transpose
  step; the MSL port reaches the identical matrix via 3 separate float4 "columns" reassembled with
  `float3x3(col0,col1,col2)` instead, for the same float3-padding-safety reason as `UMaterialParams`.
  Every uniform crossing the CPU/GPU boundary is 4-float-group padded (17 `float4`s for
  `LitUniforms`, none of them a bare `float3`) for the same reason. Confirmed via a real, load-bearing
  finding: `VertexPositionNormalTexture` (stride 32) draws **always** go through the lit shader in
  the real reference implementation, even with `lightingEnabled=false` — `BindDrawParams()` sets
  `ambient=(1,1,1)` and zeroes every light's contribution in that case, which makes the lit formula
  degenerate to plain `DiffuseColor * texture`, the same result an unlit shader would give — so the
  separate unlit `NormalTex32` pipeline this file had until tonight was actually the wrong design
  and is now replaced by `LitTex32` unconditionally. The object-space-only fog-factor formula (a
  known, already-documented EasyGL simplification, only exactly correct when World/View are
  identity) was copied bug-for-bug on purpose, not "improved," matching this project's own
  match-the-reference discipline. **Not ported this pass**: the per-vertex (Gouraud) lit variant
  (`METAL-39`) — every lit draw currently takes the per-pixel path regardless of
  `preferPerPixelLighting`, the same already-documented, already-accepted divergence every backend
  except D3D9 has. *(Closed later the same session — see item 23 below: `METAL-39` now has a real
  `cna_v3d_lit_vertexlit`/`cna_f3d_lit_vertexlit` pair, selected correctly.)*

**A real, previously-unknown blocking dependency was found and documented in-place** (not worked
around, not silently skipped) while investigating Phase 9 — see that phase's own header note in
the table below for the full detail: real, useful instancing in this codebase always goes through
a custom `ShaderEffect` (Phase 14, not started), so Phase 9 stays deliberately un-attempted.

- **`EnvironmentMapEffect`** (`METAL-64`/`66`–`69`, same session as the `TextureCube`/lighting work
  above that unblocked it): ported `EnsureEnvMapped3DProgram()`'s real GLSL line-for-line —
  world-space reflection vector (`reflect(-E,N)`) sampled against the cube map, flat and
  Fresnel-weighted blend (computed per-VERTEX from each vertex's own un-interpolated normal/eye
  vector then Gouraud-interpolated, matching real XNA `EnvironmentMapEffect.fx` exactly — not a
  per-fragment recompute from an interpolated normal, a real, already-documented distinction, Task
  1112). Confirmed a real, non-obvious finding while reading the reference shader: real XNA
  `EnvironmentMapEffect` has **no separate ambient uniform at all** — `GpuDrawParams::
  emissiveColor`'s own doc comment already said as much ("emissive+ambient combined") but this
  pass verified it against the actual GLSL (no `uAmbientColor` declared), so the Metal port
  correctly omits a separate ambient term rather than guessing one belongs. `specularEnabled`
  (D9-81 finding #4) stays in the same accepted "not fixed outside D3D9" bucket as AlphaTestEffect's
  `isEqNe` — `envMapSpecular`'s tint itself is real and wired, just not the discrete
  shader-variant-selection flag.
- **`SkinnedEffect`** (`METAL-72`–`75`/`77`/`78`): ported `EnsureSkinnedProgram()`'s real GLSL
  line-for-line, including the safe-normalize NaN guard for a near-180°-relative-bone-rotation
  blend (falls back to the bind-pose normal for that one vertex rather than propagating NaN) and
  the `WeightsPerVertex` (1/2/4) branching (Task 895: real XNA `Skin()` only sums that many
  weight/index pairs). The 72-bone transform array (4,608 floats / 18KB) genuinely exceeds
  `setVertexBytes:`'s 4KB inline limit — the one uniform in this whole file that *must* be a real
  `MTLBuffer`, reallocated fresh each draw via `newBufferWithBytes:`, matching
  `MetalVertexBuffer::SetData`'s own already-established "always reallocate" pattern (not a new
  resource-lifetime risk category). Real, load-bearing finding from reading the reference shader
  closely: skinned draws apply **no** world-space normal-matrix step at all — only the bone blend's
  own `mat3(skinMat)` — so `SkinnedTransform` correctly carries no normal-matrix columns, unlike
  `LitTransform`/`EnvTransform`. `params->pbr` now throws a clear, honest "not yet implemented"
  error instead of silently falling through to a non-PBR shader when both `pbr` and `skinned` are
  set (Phase 8 hasn't landed). **Not ported this pass**: the per-vertex (Gouraud) lit variant
  (`METAL-76`), the same class of divergence `METAL-39` had. *(Both closed later the same session —
  see items 23/24 below.)*
- **`ReadBackbuffer`** (`METAL-130`/`132`/`133`): real `MTLBlitCommandEncoder` copy into a
  `MTLResourceStorageModeShared` staging buffer. Needs no Y-flip at all (unlike GL's
  `ReadBackbuffer`, whose framebuffer origin is bottom-left) — Metal's own texture origin is
  already top-left, matching D3D/XNA convention directly. **Documented, not hidden, tradeoff**:
  since the drawable's color texture is `MTLStorageModePrivate`, reading it back requires
  committing the current command buffer (there's no way to resume encoding into an
  already-committed one for a later, separate `Present()`) — so this function ends the current
  encoder and commits+presents+waits as one unit, behaving like an early, forced end-of-frame; the
  game's own subsequent `Present()` becomes a safe no-op rather than a double-present. Also found a
  pre-existing, cross-backend (not Metal-specific) gap while checking `METAL-132`: neither Metal's
  nor EasyGL's `ReadBackbuffer` applies logical→physical letterbox scaling to `x,y,w,h` — both use
  raw physical-drawable coordinates.
- **Real occlusion queries** (`METAL-136`–`139`): `MetalOcclusionQueryBackend` via a shared
  `MTLVisibilityResultBuffer` (1024 slots, one 8-byte `uint64_t` count per live query, allocated
  once and attached to every render pass — Metal requires this at render-pass-creation time, unlike
  `setVisibilityResultMode:offset:` itself which can be called mid-encoder). Completion is tracked
  via a `[MTLCommandBuffer addCompletedHandler:]` block writing into a heap-allocated
  `std::shared_ptr<std::atomic<bool>>` (kept alive by the block itself, not just this object).
  `SupportsCapability(GraphicsCapability::OcclusionQuery)` flipped back to the real, correct `true`
  default (previously explicitly `false`, from the `METAL-1`/`METAL-197` pass). **Documented
  scope limitation**: a `Clear()` call between `Begin()`/`End()` (which commits+waits synchronously,
  starting a fresh command buffer) would split the visibility write across two command buffers,
  which this single-completion-handler design does not track correctly — real game code's tight
  `Begin()`/`End()` brackets around a small draw set don't hit this, but it is a real, narrow,
  documented gap, not silently absent. Metal reports a genuine `uint64_t` sample-passed **count**
  (`PixelCount()`), a real capability advantage over EasyGL's GLES3
  `GL_ANY_SAMPLES_PASSED`-derived boolean.
- **`RenderTarget2D`** (`METAL-98`–`101`/`106`/`107`, the architecturally riskiest piece landed
  this session — it touches `ensureFrame()`/`endFrame()`/`clear()`, the core frame lifecycle every
  other draw path depends on): `MetalRenderTargetBackend` with real bind/unbind. Metal render
  passes are fixed-attachment for their whole encoder lifetime (unlike GL's dynamic FBO rebinding),
  so switching targets means ending the current encoder/command-buffer and starting a new one
  against different attachments — `Impl::resolveActiveAttachments()` centralizes "which
  color/depth texture should the next render pass use" (the bound render target, or the backbuffer
  drawable) so `ensureFrame()`/`clear()` share one answer instead of duplicating the branch.
  `Impl::endActiveEncoding()` (shared by `endFrame()` and the render target's own `Bind`/
  `UnbindAsRenderTarget()`) guards `presentDrawable:` with `if (drawable)` so it's a correct no-op
  for a render-target-only encoder cycle. Every `RenderTarget2D` always gets a real depth+stencil
  texture regardless of the requested `DepthFormat` — the same simplification tier `Vulkan`'s own
  `CreateRenderTarget2D` doc comment already documents and the project already accepts (not EasyGL/
  Bgfx's more honest "only allocate what was requested" tier), because every pipeline in this file
  hardcodes `depthAttachmentPixelFormat`/`stencilAttachmentPixelFormat` unconditionally already.
  **Not done this pass**: MSAA/mip (`METAL-103`/`104`, silently ignored, matching the interface's
  own established "backends that cannot honor a preference silently clamp" convention), the
  `preserveContents`-driven `DontCare`-on-rebind optimization (`METAL-102`, always `Load` — correct,
  just not optimized), `RenderTargetCube`/MRT (Phase 10's remaining rows), and `GetData()`
  (`METAL-131`, left at the interface's own no-op default).
  - **Four real, independently-found-and-fixed bugs surfaced by self-review before this was
    committed** (not caught by a compiler — none is available here — caught by re-reading the diff
    line-by-line against what each function actually needs, which is exactly why that discipline
    matters most on exactly this kind of core-lifecycle change): (1) a genuine pipeline/attachment
    **format mismatch** — `MetalRenderTargetBackend`'s color texture was first written as
    `RGBA8Unorm`, but every pipeline in this file hardcodes `colorAttachments[0].pixelFormat =
    BGRA8Unorm` (matching the backbuffer) — fixed to `BGRA8Unorm` (zero shader-visible difference
    either way; BGRA/RGBA naming is memory byte order only, `texture.sample()` always presents
    `.rgba` to MSL code regardless). (2) a **compile error** in the refactored `clear()`: `w`/`h`
    were referenced for the post-encoder-creation viewport/scissor setup but no longer declared
    after replacing the old `drawable`-only texture-fetch with the new shared attachment-resolution
    helper — fixed by re-adding the declaration. (3) an **incomplete-type compile error**: this
    session's Phase 15 `computeSpriteTransform()` was still defined *inline* inside `Impl` (i.e.
    textually before `MetalRenderTargetBackend` is a complete type) but needed updating to read
    `currentRenderTarget->colorTexture()` — C++ does not allow calling a method through a pointer to
    an as-yet-incomplete type even from another member of the same enclosing class; fixed by moving
    it to a declaration-only stub with the body defined out-of-line, after `MetalRenderTargetBackend`
    (the same pattern already used for `resolveActiveAttachments()`). (4) a real, **silent-wrong-answer
    bug**, not a compile error: `computeSpriteTransform()` (and separately, last session's
    `ReadBackbuffer()`) both unconditionally read `drawable.texture.width/height` — but `drawable`
    is `nil` whenever a `RenderTarget2D` is currently bound (`resolveActiveAttachments()`'s
    render-target branch never touches it), so a message-to-nil would have silently produced a
    degenerate identity transform (wrong sprite positions when drawing into a bound render target)
    or, for `ReadBackbuffer`, a nil blit source and a nil argument to `presentDrawable:` (a real
    Metal API misuse, likely an exception) — fixed by giving `computeSpriteTransform()` a real,
    deliberate 1:1 (no window-relative letterbox) mapping when a render target is active, since an
    offscreen texture's own pixel space *is* its logical space, and by making `ReadBackbuffer` throw
    a clear, honest error when a render target is currently bound instead of misreading the wrong
    surface (`ReadBackbuffer`'s own contract is specifically about the backbuffer; a render target's
    own readback is the still-open `GetData()`, `METAL-131`).

12. **Phase 8 — NOXNA PBR** (`METAL-81`/`83`–`87`/`88` partial): a real, unskinned metallic-roughness
    `PbrEffect` path (stride 48: position+normal+tangent(float4)+uv), ported line-for-line from
    `EasyGLGraphicsBackend::EnsurePbrProgram()`'s GLSL — the glTF 2.0 spec's own reference BRDF
    (GGX/Trowbridge-Reitz normal distribution, Smith-Schlick-GGX geometry term with the standard
    direct-lighting `k=(roughness+1)²/8`, Schlick Fresnel). Tangent-space normal mapping via a real
    TBN basis built from the vertex tangent (`float4`, `.w` = bitangent sign) and the interpolated
    normal — not a placeholder. All 4 optional PBR maps (normal/metallic-roughness/emissive/
    occlusion) sample correctly and fall back to shared 1×1 default-white or default-flat-normal
    textures (created once in the constructor, released in the destructor) when unbound, so a
    partially-authored PBR material never silently reads garbage or a stale texture-unit binding —
    a stricter guarantee than this file's own established texture0/texture1/envMap "leave whatever
    was bound before" gap, deliberately, since the constant cost of one extra 1×1-texture bind per
    unbound map is cheap and there is no equivalent safe "leave it" default for a `sampler`-driven
    PBR term. `selectPipelineKind()` now branches on `params->pbr` before `params->skinned`, matching
    `EasyGLGraphicsBackend::SelectProgram()`'s own precedence — `pbr && skinned` throws a clear
    "SkinnedPbrEffect not yet implemented" (`METAL-82`) rather than silently rendering with the wrong
    shader. Self-reviewed line-by-line before commit (uniform struct field order cross-checked
    between the MSL struct, the C++ mirror struct, and `fillPbrUniforms()`; texture-unit indices
    cross-checked between the MSL fragment-shader parameter list and the dispatch-site
    `setFragmentTexture:atIndex:` calls; the stride-48 vertex descriptor cross-checked against
    `VPbrIn`'s attribute layout) — no bugs found this time, unlike the RenderTarget2D phase.

13. **Phase 8 follow-up — `SkinnedPbrEffect`** (`METAL-82` closed same session, right after the
    unskinned path landed): stride-68 vertex layout (position+normal+tangent+uv+boneWeights+
    boneIndices), combining `cna_skin_common`'s real per-vertex weighted-bone-matrix blend (position,
    normal, **and now tangent** all transformed by the same `skinMat`/`mat3(skinMat)` — no separate
    inverse-transpose normal-matrix step, matching `SkinnedEffect`'s own established precedent, not
    unskinned PBR's inverse-transpose path) with the *unmodified* unskinned-PBR fragment shader
    (`cna_f3d_pbr`) — both vertex shaders emit the same `VPbrOut` interpolant struct, so no second
    fragment shader was needed. `selectPipelineKind()`'s `pbr && skinned` branch now returns a real
    `PipelineKind::SkinnedPbr68` (stride-checked) instead of throwing; the not-yet-implemented throw
    now only fires on a genuine stride mismatch. `fillSkinnedPbrUniforms()` delegates its identical
    fragment-uniform fields to `fillPbrUniforms()` (via a throwaway `PbrTransform`) rather than
    duplicating ~15 field assignments a third time, then fills its own `wvp`/`world`/`skinParams`
    separately — same 72-bone `MTLBuffer` upload pattern as Phase 7's `Skinned52`/`56`. Self-reviewed
    the same way as item 12 (struct field order, texture-unit indices, vertex-descriptor offsets, and
    the exhaustive `switch(kind)` in `getOrCreatePipeline()` all cross-checked) — no bugs found. Not
    done: `CTest` probe-pixel coverage (`METAL-89`, no compiler available here) and the `METAL-90`
    doc-ownership decision (deferred, not urgent) — both now the only open items in Phase 8.

14. **`METAL-5` — explicit front-facing winding** (a real risk area this doc had itself flagged as
    "deliberately left untouched," not a formality — see this project's own history of an empirically
    hard-won Vulkan front/back stencil swap for why silently-correct-by-default assumptions in this
    codebase deserve real scrutiny, not just tuning). Verified first, not just tuned, per that same
    lesson: `VulkanGraphicsBackend`'s own tested rasterization state hardcodes `rs.frontFace =
    VK_FRONT_FACE_CLOCKWISE` and maps XNA `CullClockwiseFace`(1)/`CullCounterClockwiseFace`(2) to
    `VK_CULL_MODE_FRONT_BIT`/`BACK_BIT` — exactly what Metal's existing `c==1?Front:(c==2?Back:None)`
    already does. Cross-checking against Apple's own `MTLRenderCommandEncoder.setFrontFacingWinding:`
    documentation (front-facing primitives are clockwise-wound *by default* if never called) confirms
    Metal's default already matches Vulkan's explicit choice — the existing code was not actually
    producing wrong culling, but was depending on an *unstated* SDK default rather than an explicit
    guarantee, exactly the fragile pattern this project has been burned by before. Fixed by adding
    `[encoder setFrontFacingWinding:MTLWindingClockwise]` explicitly at all 3 call sites that set cull
    mode (`Impl::applyTrackedEncoderState()`, `ApplyRasterizerState()`'s live-encoder branch, and
    `drawMetal3D()`'s per-draw state application) rather than changing any actual mapping — this
    removes the risk permanently without altering current (correct) rendering behavior.

15. **Phase 20 — `SupportsCapability` accuracy review** (`METAL-189`/`190`/`193`/`194` confirmed
    correct; `METAL-191` found genuinely wrong): re-reading every `GraphicsCapability` entry against
    this session's now much-more-complete backend surface (rather than re-trusting the answers
    `METAL-192`/`195`/`196`/`197` established earlier in the night) found one more real false
    positive of the exact same class: `MultiSampleAntiAliasing` was still answering the inherited
    `true` default, but `CreateRenderTarget2D()`'s own `multiSampleCount` parameter is silently
    ignored (see its own long-standing comment citing `METAL-103`/`104`) and the backbuffer itself is
    never allocated above 1 sample — genuinely no MSAA support exists yet. Fixed the same way as the
    earlier three cases: an explicit `false` case in `SupportsCapability()`, to be removed once
    `METAL-104` (multisample resolve) actually lands. `ThreeD`/`DepthStencilBuffer`/
    `AnisotropicFiltering`/`WireFrame` were each individually re-verified against this session's real
    code (vertex/index buffers + 3D draws throughout; real `Depth32Float_Stencil8` on both the
    backbuffer and every `RenderTarget2D`; `METAL-1`'s sampler cache genuinely applies
    `maxAnisotropy`; `FillMode::WireFrame`→`MTLTriangleFillModeLines` genuinely wired) — all four
    confirmed correct, no further code change needed for them.

16. **Phase 18 — resource-lifetime / command-buffer synchronization audit** (`METAL-173`–`181`
    answered; one real bug found and fixed; one real gap found and deliberately left open as
    `METAL-256`):
    - **`METAL-173`/`174` (`MetalVertexBuffer`/`MetalIndexBuffer`'s reallocate-on-`SetData()`
      pattern) — safe.** Apple's Metal documentation states a command buffer automatically retains
      every resource it references for its entire GPU execution lifetime ("Metal automatically
      tracks resources referenced by a command buffer and keeps them alive until the command buffer
      has finished executing"). Releasing this code's own strong reference after `SetData()`
      reallocates does not deallocate a buffer a prior frame's still-in-flight command buffer is
      reading — Metal's own internal retain keeps it alive until that GPU work genuinely completes.
      Confirmed safe, no code change needed.
    - **`METAL-175` (`MetalTexture::UpdatePixels()`/`UpdatePixelsLevel()`'s in-place
      `replaceRegion:`) — genuinely NOT safe as written.** Unlike buffer reallocation,
      `replaceRegion:` mutates the *same* `id<MTLTexture>` object's storage in place. Apple's own
      Metal synchronization guidance is explicit that the app, not Metal, is responsible for not
      modifying a resource's contents while a not-yet-completed command buffer may still read it —
      there is no automatic protection for in-place content mutation the way there is for
      reference-counted object lifetime. A game calling `Texture2D.SetData()` on a texture drawn in
      a still-GPU-executing prior frame (the ordinary case in any pipelined/multi-frame-in-flight
      game loop) risks the GPU read observing torn or partially-updated content — a real, currently
      unmitigated hazard. **Deliberately not fixed this pass**: mirroring the buffer pattern
      (reallocate a fresh `id<MTLTexture>` per update) is not a safe drop-in fix here, because
      `UpdatePixels()` only ever rewrites level 0 — a texture with other, separately-uploaded mip
      levels (via `UpdatePixelsLevel()`) would silently lose that content in the fresh, otherwise-
      uninitialized replacement texture. A correct fix needs either a per-level blit-copy of every
      untouched level into the new texture, or an explicit completion-handler/`MTLSharedEvent`-gated
      update queue — real surgery I should not attempt without a compiler to verify against. Tracked
      as the new `METAL-256` rather than silently left as part of `METAL-175`'s own now-answered
      audit question.
    - **`METAL-176` (buffer sub-allocation ring/pool)** — scope decision recorded: defer until
      profiling on real hardware shows the current one-`MTLBuffer`-per-`SetData()` pattern is
      actually a bottleneck; no code written speculatively.
    - **`METAL-177` (`SetDataWithOptions` hint handling)** — decided the current behavior already
      satisfies both hints' *observable* contract: `Discard` promises the old contents may be
      thrown away (a fresh allocation trivially satisfies this — it does not need to preserve
      anything), and `NoOverwrite` promises the caller will not write to a region still in GPU use
      (a fresh allocation can never alias a still-in-flight buffer, so this can never be violated
      either). A real override would only matter for a *performance* difference (recycling a
      completed buffer instead of allocating fresh), not a correctness one — deferred alongside
      `METAL-176` for the same profile-driven reason.
    - **`METAL-178`/`179` (context-loss no-ops)** — confirmed intentional by checking which other
      backends override these: only `D3D9GraphicsBackend`/`EasyGLGraphicsBackend` do, both genuinely
      context-loss-prone APIs; Vulkan/D3D11/D3D12/WebGPU/Bgfx/SdlGpu/DX3 all inherit the same no-op
      Metal now correctly also relies on. No code change needed.
    - **`METAL-180` (render-target-switch scaling) — a real bug found and fixed.** The original
      `endActiveEncoding()` unconditionally called `presentDrawable:` whenever ending an encoder with
      a non-nil `drawable`, with no distinction between a genuine end-of-frame and a mid-frame
      boundary (a `Clear()` call starting a fresh render pass, or `SetRenderTarget2D()` switching
      to/from an offscreen target). Every such mid-frame boundary would present whatever partial
      backbuffer content existed at that moment — visible tearing/flicker — and then null `drawable`,
      so the *next* backbuffer touch that same frame fetched an entirely new drawable via
      `nextDrawable`, meaning a single logical frame with N target switches could present and
      re-acquire a drawable up to N+1 times instead of exactly once. Fixed by giving
      `endActiveEncoding()` a `bool presentBackbuffer` parameter: `command` is still always committed
      (a Metal command buffer cannot be resumed once an encoder ends), but only `endFrame()` (called
      from the real `Present()`) passes `true` — `clear()` and every `MetalRenderTargetBackend`
      bind/unbind/destructor call site now passes `false`, so `drawable` survives mid-frame
      boundaries and is presented+released exactly once per game-initiated `Present()` call,
      matching `GraphicsDevice.Present()`'s real XNA contract regardless of how many render-target
      switches happened in between. This is exactly the "get this right architecturally before
      Phase 10 lands, not as a retrofit" this task asked for — found only now, on a dedicated audit
      pass, despite Phase 10 having already shipped and been self-reviewed once before.
    - **`METAL-181` (final lifecycle model, written here)**: one `id<MTLCommandQueue>` for the
      backend's lifetime. Within a single logical frame, zero or more `id<MTLCommandBuffer>`/
      `id<MTLRenderCommandEncoder>` pairs may be created and ended — a fresh pair is created lazily
      by `ensureFrame()` whenever none is active, and `endActiveEncoding(bool presentBackbuffer)` is
      the *only* function allowed to end one (always commits; only presents+releases `drawable` when
      `presentBackbuffer` is true). Exactly one call site passes `true`: `endFrame()`, itself only
      reachable from the public `Present()`. Every other encoder-ending call site (`clear()` starting
      a fresh pass, `MetalRenderTargetBackend::BindAsRenderTarget()`/`UnbindAsRenderTarget()`/its own
      destructor) passes `false`. The backbuffer's `drawable` is acquired at most once per real frame
      (lazily, the first time `resolveActiveAttachments()` needs it with no `RenderTarget2D` bound)
      and persists across any number of mid-frame encoder boundaries until genuinely presented.
      `ReadBackbuffer()` is the one documented exception: it deliberately forces an early, self-
      contained end-of-frame (ends encoder, blits, presents, commits, waits, all as one unit) because
      once a command buffer is committed it cannot be resumed for a later, separate `Present()` — the
      game's own subsequent `Present()` call becomes a safe no-op via `endFrame()`'s
      `if (!command) return;` guard rather than a double-present.

17. **Phase 16 — resize/fullscreen/drawableSize/Retina research** (`METAL-162`–`167` answered; one
    new real cross-backend gap found and deliberately NOT fixed here, `METAL-257`): this repo
    vendors SDL3's actual source, not just headers — `third_party/SDL/src/video/cocoa/
    SDL_cocoametalview.m` (present in sibling repos this session could read, e.g. `cnanet`/
    `cnagraphics`) settles every question in this phase without writing a single line of new Metal
    code. `SDL3_cocoametalview.updateDrawableSize` already sets both `contentsScale` and
    `drawableSize` correctly (from `[self convertSizeToBacking:size]`, matching Apple's own
    `CAMetalLayer` HiDPI guidance exactly), called once at view creation and again on every
    `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` (covering plain resize, fullscreen toggle, and Space-exit
    resize alike, all one mechanism) — gated by `SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE`, which
    defaults to `true`. `ensureFrame()`/`resolveActiveAttachments()`'s existing depth-texture-
    recreate-on-size-mismatch logic was independently confirmed sufficient (it already re-reads
    `drawable.texture.width/height` — which reflects SDL's own already-updated `drawableSize` — on
    every fresh-drawable fetch). Found one genuinely new gap while tracing this, though: SDL3's
    `highDPI` behavior above is itself gated on the window being created with
    `SDL_WINDOW_HIGH_PIXEL_DENSITY`, and `GraphicsDevice.cpp`'s shared `getBackendWindowFlags()`/
    `SDL_CreateWindow()` call never sets it, for *any* backend — meaning even Metal's own
    fully-correct layer setup would still render 1x/blurry on a real Retina Mac today. Deliberately
    NOT fixed in this Metal-only plan: it's cross-backend shared window-creation code (also read by
    Vulkan-via-MoltenVK's own identical `Cocoa_Metal_CreateView` path), and CLAUDE.md's own
    WebGPU-precedent guidance is explicit that such changes need cross-backend understanding first,
    not a Metal-plan drive-by — tracked precisely as `METAL-257` instead.

18. **Phase 17 — frame pacing / presentation policy** (`METAL-168` fixed; `169`–`172` decided/
    confirmed, no code needed): `swapInterval` was a real, previously-dead field — stored by the
    constructor and `SetSwapInterval()` but never read by anything that touched the actual
    `CAMetalLayer`. Fixed by applying `layer.displaySyncEnabled = (swapInterval != 0)` in both
    places. `CAMetalLayer` has no true per-value interval knob the way `SDL_GL_SetSwapInterval`'s
    0/1/-1 or Vulkan's present-mode choice (`VulkanGraphicsBackend`'s own real code, already choosing
    `IMMEDIATE`/`MAILBOX`/`FIFO_RELAXED`/`FIFO` per the exact same 0/1/2 XNA `PresentInterval`
    convention) do — only a boolean, so `PresentInterval.Two` maps to the same real vsync as
    `PresentInterval.One`, an honest, documented approximation rather than a silent gap.
    `maximumDrawableCount` (`METAL-169`): decided to leave at Apple's platform default rather than
    force an explicit value — unlike Vulkan's own `minImageCount+1` (a safe pattern derived from
    querying the real surface capability minimum), Metal's property is a fixed app-chosen 2-or-3
    latency/throughput tradeoff with no objectively-more-correct answer reachable without real
    hardware measurement, so forcing a value now would be a guess dressed up as a decision.
    `presentsWithTransaction`/`allowsNextDrawableTimeout` (`METAL-170`): both audited and confirmed
    correct at their defaults — `presentsWithTransaction=NO` is right since this is a dedicated,
    full-window layer with no other Core Animation content to transaction-synchronize against, and
    `allowsNextDrawableTimeout=YES` was specifically checked against this file's own existing
    minimized/occluded-window handling (`resolveActiveAttachments()`'s `if (!drawable) return false`
    path) — the default's ~1-second-timeout-then-nil behavior is exactly what that existing code
    already expects and tolerates; forcing it to `NO` would instead risk `nextDrawable` blocking the
    CPU thread indefinitely in that same scenario, actively fighting rather than complementing
    existing code. `METAL-171`'s deferral (precise-pacing `presentDrawable:atTime:`/completion
    handlers) reconfirmed correct with no new information — there is still no concrete stutter/tear
    problem to fix without real hardware to observe one on.

19. **Phase 10 partial — `preserveContents`/mip generation/`GetColorGLHandle`** (`METAL-102`/`108`
    confirmed already-correct with no code needed; `METAL-103` newly implemented): reading
    `GraphicsDevice::SetRenderTarget()`'s real code (not just the interface doc comment) settled
    `METAL-102` outright — `RenderTargetUsage.DiscardContents` is handled entirely at the *shared*
    layer via an explicit `Clear()` call issued right after binding (see its own real code, around
    `renderTarget->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents`), and
    `PreserveContents` needs nothing extra since `ensureFrame()`'s already-unconditional
    `MTLLoadActionLoad` already preserves correctly. `EasyGLGraphicsBackend::CreateRenderTarget2D`
    itself already ignores this exact parameter for the identical reason (its own commented-out
    `/*preserveContents*/`) — confirming this isn't a Metal-specific gap at all, just an unread
    parameter by design across backends. `METAL-108` confirmed the same way: grepped every `.hpp`/
    `.cpp` in this repo for `GetColorGLHandle` — only its own declaration and
    `EasyGLRenderTargetBackend`'s GL-real override exist; nothing anywhere branches on its value, so
    Metal's inherited `return 0` default is safely correct as-is.
    `METAL-103` (real mip generation) **was** a genuine gap, now fixed: `CreateRenderTarget2D`'s
    `mipMap` parameter now actually threads into `MetalRenderTargetBackend`'s constructor (previously
    silently discarded), which now allocates its color texture with `mipmapped:mipMap` (Apple's own
    convenience-initializer semantics allocate the full `floor(log2(max(w,h)))+1`-level chain when
    true). `UnbindAsRenderTarget()` now regenerates the full chain via
    `MTLBlitCommandEncoder::generateMipmapsForTexture:` on every unbind when `mipMap` was requested —
    unconditionally, not gated on whether anything was actually drawn that bind session, deliberately
    matching `EasyGLRenderTargetBackend::UnbindAsRenderTarget()`'s own already-tested precedent
    exactly (itself citing FNA3D's `OPENGL_ResolveTarget`: `if (target->levelCount > 1) { ...
    glGenerateMipmap... }` with the identical no-gating behavior). Two real Metal-specific mechanics
    had to be gotten right, unlike GL's single `glGenerateMipmap` call: (1) a blit encoder cannot
    coexist with an already-open render encoder on the same command buffer, so any still-active
    `owner_.encoder` is explicitly ended first; (2) if nothing was ever drawn/cleared this bind
    session `owner_.command` is still nil (`ensureFrame()` never ran), so a fresh command buffer is
    created (and retained, matching this file's own established retain convention) purely to host the
    blit, mirroring `ReadBackbuffer()`'s own precedent of using a small standalone command buffer for
    a one-off GPU operation — chosen specifically so mip regeneration still happens even for a target
    that was bound and immediately unbound with zero draws, matching EasyGL's own unconditional
    behavior rather than silently skipping it as a missed "optimization."

20. **Phase 10 continued — `RenderTargetCube`** (`METAL-109`/`110` landed; `METAL-111` found to
    need real code, not just verification): a single `MTLTextureTypeCube` color texture (6 slices)
    plus ONE shared 2D depth texture reused across whichever face is currently bound — deliberately
    matching `EasyGLRenderTargetCubeBackend`'s own already-tested precedent exactly (its own
    comment: only one face is ever rendered into at a time, matching FNA's `RenderTargetCube.cs`
    itself allocating a single depth/color buffer regardless of face), not a Metal-specific
    shortcut. Per-face binding uses `MTLRenderPassColorAttachmentDescriptor.slice` rather than 6
    separate texture views, which required widening `resolveActiveAttachments()` with a third
    `NSUInteger& sliceOut` parameter (0 for every existing branch, the bound face index for the new
    cube branch) threaded through into both `ensureFrame()`'s and `clear()`'s
    `rp.colorAttachments[0].slice=slice` — the third pass over this exact core rendering path this
    session, done carefully given the first two passes each found real bugs. `computeSpriteTransform()`
    got a third branch mirroring the `RenderTarget2D` one exactly (a bound face's own `size×size`
    pixel space is its own logical space). `METAL-111` turned out to need a genuine override, not
    just a "confirm the base default composes correctly" check as originally scoped: reading
    `EasyGLGraphicsBackend::SetRenderTarget2D`/`SetRenderTargetCubeFace`'s own real code (not just
    the interface's doc comment) showed both methods cross-unbind whichever *other* kind of target
    (2D vs. cube) is currently active before binding a new one — the base `IGraphicsBackend`
    default's simple `rt ? rt->BindAsRenderTargetFace(face) : SetRenderTarget2D(nullptr)` composition
    does no such cross-unbinding at all. Without it, switching directly from render target A to
    render target B (a normal XNA multi-pass pattern — no intervening `SetRenderTarget2D(nullptr)`
    call) would never call `UnbindAsRenderTarget()` on A, so A's mip chain (`METAL-103`, landed
    earlier this same session) would silently never regenerate — a real correctness gap that was
    latent and harmless before mip generation existed, but became a real, observable bug the instant
    it did. Fixed by giving `SetRenderTarget2D()` the same cross-unbind logic and adding a real
    `SetRenderTargetCubeFace()` override, both mirroring EasyGL's exact contract. A `RenderTargetCube`
    can now also be sampled as an `EnvironmentMapEffect` reflection source immediately after being
    rendered into — arguably `RenderTargetCube`'s single most common real use — via a new
    `nativeCubeTextureFor()` helper (mirroring `nativeTextureFor()`'s own established pattern
    exactly) used at the `EnvMap32` draw site instead of a bare `dynamic_cast<MetalTextureCube*>`
    that would have silently failed for a render-target cube. Self-review before commit caught one
    more real regression this introduced: `ReadBackbuffer()`'s existing "throw if a render target is
    active" guard only checked `currentRenderTarget` (the 2D case, fixed in an earlier phase this
    session) — a bound `RenderTargetCube` face hits the exact same message-to-nil-`drawable` hazard
    and was not yet covered; extended the guard to check both.

21. **Phase 12 continued — real `GetData()` on `RenderTarget2D`/`RenderTargetCube`** (`METAL-131`):
    a shared `blitTextureToClientBuffer()` helper (blit into an `MTLResourceStorageModeShared`
    staging buffer, `waitUntilCompleted`, `memcpy` out) replaces the inherited no-op default for
    both render-target classes, instead of writing two near-duplicate blit implementations — exactly
    what the task itself asked for. Uses its own fresh, independent command buffer rather than
    reusing `owner.command`, which is correct specifically *because* each `GetData()` override first
    checks whether the target being read is still the currently-active render target and, if so,
    calls `endActiveEncoding(false)` to force its pending command buffer to actually execute before
    the read — the identical reasoning `ReadBackbuffer()` already established for the same hazard
    (an independent command buffer's blit could otherwise run before the still-uncommitted render
    pass that produced the content it's supposed to read, since nothing else orders them relative to
    each other). `RenderTargetCube`'s own override additionally guards on `currentRenderTargetCube
    == this` regardless of *which* face is currently bound, since every face shares one underlying
    `MTLTexture` object. Plain `MetalTexture3D` left at the inherited no-op *at the time this was
    written* — reasoned (incorrectly, corrected in item 25 below) to match plain `MetalTexture`'s
    own precedent for `SetData()`-populated textures never needing `GetData()`; it turns out that
    precedent is 2D-texture-specific and does not hold for `Texture3D`/`TextureCube`.

22. **Phase 1 remainder — format-table research** (`METAL-15`/`16`/`17`/`18`/`20` answered, several
    found to be based on a false premise; `METAL-14`/`19` genuinely still open): reading
    `include/CNA/Internal/Graphics/ImageData.hpp` (a plain struct, doc comment: "RGBA8 pixel data,"
    no format field at all) and `Texture2D.cpp`'s own real code (`DxtUtil::DecompressDxt1`/
    `DecompressDxt5` called *before* every `CreateTexture(img)` call, for every backend uniformly)
    settles `METAL-15` outright: there is no "real `SurfaceFormat`" for any backend's `CreateTexture`
    to diverge from — DXT is already decompressed to RGBA8 at the shared `Texture2D` layer, so
    Metal's hardcoded `RGBA8Unorm` was never a per-backend gap, just the one true format the
    `ImageData` contract has ever carried. This also settles `METAL-17` (BC-compression device query)
    as currently moot — nothing anywhere uploads real compressed bytes to any backend today, so
    there's nothing for the query to gate — and reduces `METAL-18`'s scope (there's much less left
    to "centralize" than the task assumed). `METAL-16` (depth format) was confirmed to be a
    deliberate, already-accepted simplification rather than an oversight: it matches
    `VulkanGraphicsBackend`'s own identical "always allocate depth+stencil" tier, explicitly
    documented as the chosen approach back when `RenderTarget2D` first landed (`METAL-101`'s own
    note) — consistent with an already-mature backend's precedent, not a Metal-specific shortcut.
    `METAL-20` (anisotropy clamp range) was re-derived from Apple's own `MTLSamplerDescriptor`
    documentation rather than just re-trusting the existing `1`–`16` clamp: Metal's ceiling is a
    fixed, hardware-independent API constant (unlike `VulkanGraphicsBackend`'s/EasyGL's own
    genuinely device-queried `maxSamplerAnisotropy`/`GL_MAX_TEXTURE_MAX_ANISOTROPY` caps, which vary
    by GPU) — confirming the existing hardcoded clamp needs no device query at all, for a
    Metal-specific reason worth recording rather than just copying the Vulkan/GL pattern blindly.
    `METAL-14` (the real, general `VertexElementFormat`→`MTLVertexFormat` table) remains genuinely
    open — unlike the texture-format question, vertex layouts genuinely do carry arbitrary
    application-defined element formats via `VertexDeclaration`, with no equivalent "always
    normalized upstream" simplification — but stays correctly scoped under Phase 2's already-deferred
    generic `VertexElement`-driven descriptor builder (`METAL-26`/`27`), not attempted in isolation.
    `METAL-19` (enum-reordering regression guard) remains open, needing a real compile-time check or
    CTest unreachable from this Linux machine.

23. **Phase 3 — `METAL-39`, BasicEffect's real per-vertex (Gouraud) lighting default**: reading
    `EasyGLGraphicsBackend::SelectProgram()`'s own real stride-32 dispatch (not just its per-pixel
    shader's own code, already ported earlier this session) revealed a genuine, currently-shipping
    visual divergence from XNA/FNA: real `BasicEffect` defaults `PreferPerPixelLighting=false`,
    which selects a *per-vertex* Gouraud-shaded lighting path — lighting computed once per vertex and
    linearly interpolated across the triangle — not the per-pixel (Phong-style) path Metal has used
    unconditionally for every lit draw all session. This is a real, visible difference (most
    noticeable on large, sparsely-tessellated triangles), not a cosmetic nuance, and would fail a
    real pixel-parity test against EasyGL/FNA. Fixed by porting
    `EasyGLGraphicsBackend::EnsureLit3DVertexLitProgram()`'s real GLSL line-for-line into a new
    `cna_v3d_lit_vertexlit`/`cna_f3d_lit_vertexlit` MSL pair — identical Blinn-Phong math to the
    existing per-pixel shader, just computed in the vertex stage and passed as interpolated `litRGB`/
    `specularRGB` varyings instead of interpolating `normal`/`worldPos` and recomputing per pixel.
    Deliberately reuses the *exact same* `LitTransform`/`LitUniforms` uniform structs as the
    per-pixel variant (only the shader source differs), so `fillLitUniforms()` needed zero changes.
    New `PipelineKind::LitTex32VertexLit` entry, selected by `selectPipelineKind()`'s stride-32 case
    only when `lightingEnabled && !preferPerPixelLighting` — matching
    `EasyGLGraphicsBackend::SelectProgram()`'s own exact precedence, including its own documented
    reasoning for the `else` case: with lighting *disabled*, both shaders degenerate to the identical
    trivial `ambient=(1,1,1)` case, so the existing per-pixel pipeline correctly stays selected there
    too, avoiding an unnecessary extra pipeline-cache entry — confirming Metal's prior "always
    `LitTex32`" behavior was already correct for that specific case, not a second bug. `drawMetal3D`'s
    dispatch widened to treat both `PipelineKind`s identically (same uniform fill, same texture
    binding) since only `getOrCreatePipeline()`'s shader-pair selection actually differs between them.
24. **Phase 7 — `METAL-76`, `SkinnedEffect`'s identical per-vertex-lit gap, closed the same
    session**: ported `EasyGLGraphicsBackend::EnsureSkinnedVertexLitProgram()`'s real GLSL line-
    for-line into a new `cna_v3d_skinned_vertexlit`/`cna_v3d_skinned_color_vertexlit`/
    `cna_f3d_skinned_vertexlit` MSL set, using the exact same technique `METAL-39` just proved for
    `BasicEffect` — Blinn-Phong math moved from the fragment stage into the vertex stage and
    Gouraud-interpolated, via a new shared `cna_skin_vertexlit_common()` helper mirroring
    `cna_skin_common()`'s own existing shape (same bone blend, same degenerate-normal safety guard,
    only the lighting-evaluation stage differs). No separate ambient uniform, matching
    `cna_f3d_skinned`'s own established shape (`SkinnedEffect` pre-folds ambient into
    `emissiveColor` at the C++ effect layer) — `SkinnedUniforms`/`fillSkinnedUniforms()` needed zero
    changes, same as `METAL-39`'s finding for `LitUniforms`. Two new `PipelineKind` entries
    (`Skinned52VertexLit`/`Skinned56VertexLit`, stride 52/56 matching their existing per-pixel
    siblings), selected by `selectPipelineKind()`'s skinned branch using the identical
    `lightingEnabled && !preferPerPixelLighting` precedence `METAL-39` established, cross-checked
    against `EasyGLGraphicsBackend::SelectProgram()`'s own real skinned branch (same
    lighting-disabled-degenerates-to-per-pixel reasoning). `drawMetal3D`'s skinned dispatch widened
    to treat all 4 skinned `PipelineKind`s identically (same uniform fill, same bone-buffer upload,
    same texture binding) since only the shader pair differs. Both `METAL-39` and `METAL-76` are now
    closed — `BasicEffect` and `SkinnedEffect` both correctly default to per-vertex Gouraud lighting,
    matching real XNA/FNA behavior instead of silently always using per-pixel shading.

25. **`METAL-122`/`125` — real `GetData()` on plain `TextureCube`/`Texture3D`, a genuinely new
    finding, not scoped from the plan's own original text**: while implementing `RenderTargetCube`'s
    own `GetData()` (item 21 above), a check of `TextureCube.cpp`'s/`Texture3D.cpp`'s *real* code
    (not just their interface's doc comments) found neither has a CPU-side pixel-shadow shortcut the
    way `Texture2D` does (confirmed: `Texture2D::GetData()` skips the backend entirely when
    `cpuPixels_` already holds the answer; `TextureCube::GetData()`/`Texture3D::GetData()` both
    unconditionally call `backend_->GetData(...)` every time, no exception). This means my earlier
    reasoning for `METAL-131` — "plain `MetalTexture3D` deliberately left at the inherited no-op...
    matches plain `MetalTexture`'s own identical established precedent" — **was wrong**: that
    precedent only actually holds for the 2D case. Any `TextureCube`/`Texture3D` that was ever
    `SetData()`-populated (never rendered into) would have silently no-op'd on `GetData()` on Metal,
    a real, currently-shipping bug, not a deliberate, narrow scope match. Fixed by extending
    `MetalTextureCube`/`MetalTexture3D` to store a retained `id<MTLCommandQueue>` (threaded through
    their constructors from `CreateTextureCube()`/`CreateTexture3D()`) and adding real `GetData()`
    overrides via the same shared `blitTextureToClientBuffer()` helper Phase 12's render-target work
    already built. While doing this, also **hardened `blitTextureToClientBuffer()` itself** against a
    real, previously-unverified Metal API risk in the already-committed `METAL-131` code: its
    `copyFromTexture:...destinationBytesPerImage:` argument was passing the same byte count as
    `destinationBytesPerRow*height` for every 2D/cube-face copy, but Apple's own documentation for
    this parameter states it is only meaningful for a genuine multi-image (`depth>1`) copy and should
    be `0` otherwise — a real, plausible Metal validation-layer risk this session hadn't confirmed
    either way without a compiler. Generalized the helper to take explicit `z`/`depth` parameters
    (needed anyway for `Texture3D`'s genuine volume copies) and pass `0` for every `depth<=1` call,
    which is unambiguously correct regardless of which exact interpretation of the parameter's
    "ignored vs. validated" behavior is right — retroactively hardening the render-target `GetData()`
    code from item 21 at the same time, not just the two new call sites.

26. **Full-file structural consistency audit** (a dedicated pass acting as the "compiler" this
    session has never had access to): after roughly a dozen substantial, interlocking changes to
    this file across the night — each individually self-reviewed at the time — a separate, focused
    pass re-read the entire 2500+-line file start to finish and specifically cross-checked: every
    `PipelineKind` enum value has exactly one case in `getOrCreatePipeline()`'s exhaustive switch
    (no missing, no duplicate); every `vs=@"..."`/`fs=@"..."` MSL function name referenced by string
    actually exists in `kMetalShaderSource` (a typo here would fail at Metal shader-library lookup
    *at runtime*, not compile time, so extra load-bearing to catch by inspection); every C++ uniform
    mirror struct (`LitUniforms`/`SkinnedUniforms`/`PbrUniforms`/etc.) has identical field order and
    count against its MSL counterpart; every `buffer(N)`/`texture(N)`/`sampler(N)` index used at a
    `drawMetal3D` call site matches what the corresponding MSL function actually declares; every
    `[[attribute(N)]]` in each `VXxxIn` struct matches `vertexDescriptorForStride()`'s offset/format
    for that stride, across all 8 strides now in use; manual retain/release balance (this file is
    not ARC) for every object, with particular attention to this session's newest additions
    (`MetalTextureCube`/`MetalTexture3D`'s new `dev_`/`queue_` members, `blitTextureToClientBuffer`'s
    local objects, both bone-buffer allocations); and no leftover call site still using
    `blitTextureToClientBuffer`'s old `Impl&`-based signature after it was refactored to take
    `id<MTLDevice>`/`id<MTLCommandQueue>` directly. **Result: no compile/link/runtime-structural
    bugs found** — the first fully clean audit pass of the night, after the RenderTarget2D phase's
    own self-review had found 4 real bugs and several later phases each found one more. One harmless
    piece of dead code was found and removed: `cna_v3d_normaltex`, a leftover unlit stride-32 vertex
    shader from before this session's own Phase 3 work made `LitTex32` handle every stride-32 draw
    unconditionally (its own earlier narrative note: "the separate unlit `NormalTex32` pipeline this
    file had until tonight was actually the wrong design") — never referenced by name anywhere in the
    C++ dispatch code since, confirmed by grep before removal. This is real, if partial, evidence
    toward `METAL-19`'s own goal (a guard against silent structural regressions) — not a substitute
    for the compile-time/CTest check that task still asks for, but real confirmation this session's
    accumulated changes are at least internally consistent with each other.

27. **Phase 29 — `METAL-243`/`246`, iOS/tvOS scope audit** (answered from real vendored source, not
    assumption): `MetalGraphicsBackend.mm` itself was grepped for `NSWindow`/`NSView`/`#import
    <AppKit`/`#import <Cocoa`/`MTLFeatureSet_macOS`/`MTLGPUFamilyMac` — zero matches. Every
    platform-specific piece of window/view creation this file touches goes exclusively through
    SDL3's generic, cross-platform `SDL_Metal_CreateView`/`SDL_Metal_GetLayer`/
    `SDL_Metal_DestroyView` calls, never a macOS-only API directly. Reading this repo's own vendored
    SDL3 source further confirms those generic functions have a real, complete iOS/tvOS
    implementation: `third_party/SDL/src/video/uikit/SDL_uikitmetalview.m`'s
    `UIKit_Metal_CreateView`/`UIKit_Metal_GetLayer`/`UIKit_Metal_DestroyView` (present in sibling
    repos this session could read, e.g. `cnaopengl1`) implement the exact same function-pointer
    shape SDL3 dispatches to internally on iOS — and the SAME `uikit` video-driver directory handles
    tvOS too, via `#ifdef SDL_PLATFORM_TVOS` branches for the platform's own real behavioral
    differences (e.g. `SDL_uikitview.m`'s several `#if !defined(SDL_PLATFORM_TVOS)` guards), not a
    separate driver. This means `cmake/BackendSelection.cmake`'s own iOS/tvOS claim is **likely
    true, not just aspirational** — the `.mm` file itself has no known platform-incompatible code —
    though this remains genuinely unverified until a real iOS/tvOS build-only CI job (`METAL-244`/
    `245`, deliberately not attempted this session: writing CMake/CI configuration blind, with no
    iOS toolchain available here to test it against, carries the same risk profile as every other
    "needs a compiler this machine doesn't have" item, just for a different toolchain) actually
    exercises it. A real, concrete finding surfaced while reading `SDL_uikitmetalview.m`'s own
    `UIKit_Metal_CreateView`: it too gates its own HiDPI `contentsScale`/`nativeScale` handling on
    `window->flags & SDL_WINDOW_HIGH_PIXEL_DENSITY` — the exact same flag `METAL-257` (Phase 16)
    found missing from `GraphicsDevice.cpp`'s shared window creation. This means `METAL-257`'s real
    gap is not macOS-only as originally scoped — it would affect Retina rendering on iOS/tvOS too,
    once either platform is ever actually built; `METAL-257`'s own note should be read as covering
    both, still deliberately not fixed here for the identical cross-backend-scope reasoning.
    `METAL-246` (touch input ownership) confirmed the same way: grepped every graphics backend in
    this repo, Metal included, for `SDL_EVENT_FINGER`/`TouchID`/`SDL_Finger` — zero matches
    anywhere, confirming touch input is architecturally independent of the graphics backend layer
    entirely (owned by CNA's shared input/device layer, exactly as this task already hypothesized).

28. **`METAL-34` — the first, and only, genuine ✅ this entire plan earns tonight.** Every other
    task in this document, however carefully self-reviewed, is honestly 🟨 (source-complete,
    unverified) because Objective-C++/MSL literally cannot compile without a real Apple toolchain,
    unavailable on this Linux machine. `METAL-34` is explicitly the one exception the plan itself
    already called out: `PipelineKind`/`BlendKey`/`PipelineCacheKey`/`PipelineCacheKeyHash` are pure
    C++ (`uint8_t`, `bool`, an `enum class`, `std::hash`) with zero Objective-C or Apple-framework
    dependency — they only happened to live inside an `.mm` file because that's where the pipeline
    cache that uses them lives. Extracted verbatim (no logic changes) into a new
    `include/CNA/Internal/Backends/Metal/MetalPipelineKey.hpp`, with `MetalGraphicsBackend.mm`
    including it and aliasing the short names back (`using PipelineKind = MetalPipelineKind;` etc.)
    so all 43 existing call sites throughout the file — already audited clean by item 26's full-file
    pass — needed zero changes. Wrote a real `tests/CNA/Internal/Backends/Metal/
    MetalPipelineKeyTests.cpp` (deliberately **not** gated behind `#if defined(CNA_BACKEND_METAL)`,
    unlike every other backend's own test files, since gating it would defeat the entire point) with
    8 tests covering: the default `MetalBlendKey` genuinely matches `BlendState.Opaque`'s real values
    (`Blend::One=0`/`Blend::Zero=1`/`BlendFunction::Add=0`, checked against the real enum ordinals,
    not re-trusted from the original comment's own claim); `operator==` correctness for both
    `MetalBlendKey` and `MetalPipelineCacheKey` (equal only when every field matches, unequal when
    any single field differs, tested field-by-field); equal keys produce equal hashes (a hard
    `std::unordered_map` contract requirement, not a nicety); no hash collisions across all 15 real
    `PipelineKind` values with a shared default blend; and — the property that actually matters in
    production — a real `std::unordered_map<PipelineCacheKey, ..., PipelineCacheKeyHash>` round trip
    proving the cache correctly distinguishes every `PipelineKind` from every other, and correctly
    treats the *same* shader kind bound with *two different* `BlendState`s as two genuinely separate
    cache entries (exactly what `getOrCreatePipeline()` depends on in production — the wrong outcome
    here would mean a re-bound `BlendState` silently reusing a pipeline baked with the *previous*
    draw's blend factors). Initialized the `vendor/googletest` submodule (was never checked out in
    this session's working tree), configured a real `HEADLESS`-backend build with
    `-DCNA_BUILD_TESTS=ON`, and built the actual `CnaTests` binary end to end (SDL3/SDL3_mixer built
    from source as part of the same configure step, ~13 minutes total). **All 8 tests pass, both via
    direct `CnaTests --gtest_filter=...` invocation and via the officially blessed `ctest -R
    'MetalBlendKey\|MetalPipelineCacheKey'` path (CTest #83–90, 100% pass, 1.03s total)** — a real,
    reproducible, CI-equivalent result, not a simulation or a standalone scratch compile (an earlier,
    faster `g++`-only compile was used first for quick iteration confidence, then superseded by this
    full, real `CnaTests` integration build as the actual, final verification). The entire rest of
    this plan remains honestly 🟨 — this single task is the sole *fully* genuine ✅, and is marked
    that way only because it was actually exercised by a real compiler and a real test runner,
    exactly the bar `METAL-238` sets for any future ✅ claim in this document.

29. **`METAL-40`'s CPU-side normal-matrix formula, machine-verified the same way**: having just
    proven `MetalPipelineKey.hpp`'s extraction technique works end-to-end, the same treatment was
    applied to `computeNormalMatrixCols()` — also pure C++ arithmetic with zero Objective-C
    dependency, extracted to `MetalNormalMatrix.hpp` (`MetalGraphicsBackend.mm` keeps a one-line
    same-signature wrapper so its 3 existing call sites are unaffected). The MSL shader source's own
    comment already claimed this formula was "independently re-derived and hand-verified" to equal
    `transpose(inverse(world3x3))` — that claim is now real and repeatable, not just reasoning
    trusted on faith: 4 new `CnaTests` cover an identity input (trivial baseline), a uniform-scale
    input (`transpose(inverse(diag(2,2,2))) = diag(0.5,0.5,0.5)`, a simpler independent derivation
    path), a genuinely non-diagonal non-uniform-scale case whose expected columns were hand-derived
    via the *classic adjugate/cofactor method* — deliberately a different derivation path than the
    row-0-cofactor-expansion shortcut the function itself uses, so the test isn't just re-checking
    the function against its own logic — and, the strongest check, a property-based test verifying
    the actual mathematical guarantee a normal matrix exists to provide: a tangent vector and a
    normal that start perpendicular in object space stay perpendicular after the tangent is
    transformed by the ordinary world matrix and the normal by this computed normal matrix, for a
    deliberately non-uniform-scale, non-block-diagonal `M` chosen specifically so that naively
    transforming the normal by `M` directly (the wrong, common bug) measurably breaks
    perpendicularity — confirmed by an explicit negative-control assertion in the test itself. All 4
    tests pass on this Linux machine (`ctest -R MetalNormalMatrix`). This is real, additional
    evidence for `METAL-40`'s own correctness — the single riskiest hand-derived formula in the
    entire lighting pipeline (misusing it would silently distort every lit normal under any
    non-uniform-scale `World` transform) — though `METAL-40` itself stays 🟨 overall since its MSL-
    side consumption remains genuinely unverified.

30. **`METAL-13`, machine-verified the same way**: `primitiveVertexCount()`'s formula only reads a
    plain XNA framework enum (`PrimitiveType`) and plain `int`s — zero Objective-C dependency,
    extracted to `MetalPrimitiveVertexCount.hpp`. This is the exact function `METAL-12`/`13` fixed
    earlier this session (`PointList` was silently falling through to the triangle-count `*3`
    default) — 5 new `CnaTests`, one per `PrimitiveType` value, with a dedicated `PointListEXT`
    regression test locking that fix in against ever silently regressing back to the `*3` default,
    every formula additionally cross-checked against `EasyGLGraphicsBackend`'s own already-tested
    equivalent switch statement, not just re-derived from memory. All 5 pass on this Linux machine.
    Three real, machine-verified pieces of the Metal backend now exist tonight (`METAL-34`'s
    pipeline-cache key/hash, `METAL-40`'s normal-matrix formula, and this) — every one of them found
    by the same discipline: after landing `METAL-34`, deliberately went looking for *more* pure-C++
    logic hiding inside Objective-C++ files specifically *because* the technique had just been
    proven to work, rather than treating it as a one-off.

31. **`METAL-155`'s letterbox/virtual-resolution formula, a fourth real ✅**: `computeLogicalViewport()`'s
    real arithmetic (the exact function behind `METAL-153`–`159`'s own documented real bugs earlier
    this session) only reads plain ints/floats and `CnaPresentationMode` (a shared, zero-Objective-C
    enum) — extracted to `MetalLogicalViewport.hpp`, leaving only the "ask SDL for the real physical
    window size" step in the `.mm` wrapper. 8 new `CnaTests`, one per `CnaPresentationMode` value
    plus edge cases, each with hand-computed expected numbers for a concrete scenario (e.g. 800×600
    virtual content in a 1000×600 physical window: `Letterbox` centers it with `x=100` pillarbox
    bars, `Overscan` instead crops it with a `y=-75` overflow, `FixedHeightDynamicWidth` derives
    `logicalWidth=640` from the real physical aspect ratio while filling the window edge-to-edge).
    Writing the very first test caught a real mistake in the *test's own* assumption, not the
    function under test: an invalid physical `width` does **not** force `height` to zero too (or
    vice versa) — each axis clamps to zero independently before the early-return check even runs,
    a real, previously-undocumented behavior this test now locks in precisely rather than glossing
    over. All 8 pass on this Linux machine. A fourth genuinely real, fully-earned ✅ tier tonight.

32. **The WVP matrix helper set — `Mat4`/`multiply`/`fromXna`/`transpose` — a fifth real ✅, and the
    strongest cross-validation of the night**: `drawMetal3D()` builds every single 3D draw's WVP
    matrix via `transpose(multiply(multiply(fromXna(world),fromXna(view)),fromXna(projection)))` —
    genuinely foundational, load-bearing infrastructure underneath every `PipelineKind` this whole
    plan implements, not tied to one specific `METAL-N` task the way the previous four extractions
    were. Extracted to `MetalMat4.hpp` (row-major `float[16]` storage plus
    `Microsoft::Xna::Framework::Matrix`, itself plain C++). The strongest available check isn't a
    hand-derived expected value at all: it's cross-validating `MetalMat4Multiply(MetalMat4FromXna(A),
    MetalMat4FromXna(B))` against `Microsoft::Xna::Framework::Matrix::Multiply(A,B)` — the REAL,
    independently-implemented, already-in-production XNA `Matrix` type this whole engine already
    relies on — for translation×scale, scale×translation (proving the two truly differ, since matrix
    multiplication doesn't commute — a real sanity check on the test itself, not just the function),
    and a rotation×translation×scale chain shaped like a genuine WVP computation. 7 tests total:
    field-order copy fidelity (`FromXna`, using 16 distinct values so a transposition bug would be
    caught), identity-multiply is a no-op, the two cross-validation cases above, and transpose
    correctness (identity, involution — `transpose(transpose(X))==X` — and a hand-verified concrete
    asymmetric case using a translation matrix's own real M14/M41 row↔column swap). All 7 passed on
    the very first run. Ran the full Metal-tagged `ctest` subset again afterward (38 tests across all
    5 extracted headers) to confirm zero regressions: 100% pass.

33. **`selectPipelineKind()` — a sixth real ✅, and the single highest-value extraction of the
    night**: this is the actual shader-variant dispatch decision every 3D draw call goes through —
    arguably the single most safety-critical function in the whole backend, since a bug here silently
    routes a draw to the *wrong* MSL shader pair (wrong lighting model, wrong vertex layout
    interpretation) rather than crashing, exactly the class of bug that is hardest to notice by
    inspection alone. It only reads `GpuDrawParams` (plain C++, zero Objective-C dependency) and a
    stride, so — like the previous five — it was extracted verbatim into `MetalSelectPipelineKind.hpp`
    with `MetalGraphicsBackend.mm` reduced to a one-line same-signature wrapper, all existing call
    sites unaffected. 15 new `CnaTests` cover every real branch (colored/textured/dualTexture/
    envMapping/skinned/pbr/skinnedPbr, every stride gate and every invalid-stride throw) **and,
    deliberately, the precedence order itself** — matching `EasyGLGraphicsBackend::SelectProgram()`'s
    own real precedence (`pbr(+skinned) > skinned > envMapping > dualTexture > textured > colored`) —
    by setting *multiple* flags simultaneously and asserting only the higher-precedence kind is ever
    selected (e.g. `pbr && skinned && envMapping && dualTexture` all `true` at once must still select
    `SkinnedPbr68`, not silently fall through to a lower-precedence branch). All 15 passed on the very
    first run, CTest #115–129, no bugs found in either the extraction or the tests this time. Ran the
    full Metal-tagged `ctest` subset again afterward (53 tests across all 6 extracted headers) to
    confirm zero regressions: 100% pass. A sixth genuinely real, fully-earned ✅ tier tonight — same
    discipline as the WVP matrix helpers: foundational dispatch infrastructure, not tied to one single
    pre-existing `METAL-N` task ID.

**Explicitly still open / not attempted across this whole overnight session** (do not assume these
are done — this list is kept current as the authoritative "what's actually left" summary, updated
at the end of each landed phase rather than trusted from an earlier revision):
`METAL-14`/`19` (the real `VertexElementFormat` table, scoped under the fully generic
`VertexElement`-driven descriptor builder below, and the enum-reordering regression guard —
`METAL-15`–`18`/`20` are now closed, several found to be based on a false premise, see above); the
fully generic `VertexElement`-driven descriptor builder
(`METAL-26`/`27`); attachment-format/sample-count-keyed pipelines (`METAL-31`/`32`); Phase 8's
remaining `CTest` coverage/doc-ownership tasks (`METAL-89`/
`90` — both unskinned `PbrEffect` and `SkinnedPbrEffect` themselves landed this session); the rest of
Phase 10 (MRT `METAL-112`/`113`, MSAA `METAL-104`/`105`, all `CTest`s `METAL-114`–`118`, docs
`METAL-119` — `preserveContents`/mip/`GetColorGLHandle` `METAL-102`/`103`/`108`, `RenderTargetCube`
`METAL-109`–`111`, and `GetData()` `METAL-131` are now closed, see above); Phase 14 (custom
`ShaderEffect`, which
Phase 9 Instancing is itself blocked on, and which is itself further blocked on Phase 2's generic
`VertexElement`-driven descriptor builder — see Phase 14's own header note); Phase 9 itself (blocked
on Phase 14); `METAL-256` (the real texture-update
CPU/GPU-sync hazard Phase 18's audit found but did not fix) and `METAL-257` (the cross-backend
missing-`SDL_WINDOW_HIGH_PIXEL_DENSITY` gap Phase 16's research found but deliberately left for a
cross-backend task, not this Metal-only plan — now also confirmed to affect iOS/tvOS, not just
macOS, per Phase 29's own research); the rest of Phase 20 (`METAL-198`'s `CTest`); Phases 21–28 and
30 in full (all NOXNA extensions, testing infrastructure, CI, docs, cross-backend pixel parity); the
rest of Phase 29 (`METAL-244`/`245`/`247`–`251` — a real iOS/tvOS build-only CI job and everything
downstream of it, `METAL-243`/`246` themselves answered, see above).

## Implemented initial foundation

- Compile-time backend selection: `CNA_GRAPHICS_BACKEND=METAL` and `CNA_BACKEND_METAL`.
- Apple-only CMake hard gate and Objective-C++ enablement only when METAL is selected.
- Native `MTLDevice`, `MTLCommandQueue`, `CAMetalLayer`, drawable acquisition and presentation.
- BGRA8 swapchain drawable rendering.
- Native depth32+stencil8 attachment.
- Color/depth/stencil clear combinations.
- Native `MTLBuffer` vertex and 16/32-bit index buffers.
- Native RGBA8 `MTLTexture` creation and updates.
- Runtime-compiled Metal Shading Language library.
- Colored/textured/**lit** 3D draw paths, now dispatched through a `PipelineKind`-keyed lazy
  pipeline cache (7 variants: colored-16, textured-20, colortex-24, **lit-32 with real per-pixel
  lighting/fog/specular/emissive**, dualtex-20, dualtex-colored-24, sprite-2d) instead of 5
  eagerly-built fixed fields (🟨 landed 2026-07-19 — `METAL-22`/`METAL-23`/`METAL-25`/`METAL-38`/
  `40`/`42`–`44`/`46`/`47`).
- `TextureCube`/`Texture3D` backends, and a real `EnvironmentMapEffect` (world-space cube-map
  reflection, flat + Fresnel-weighted blend, lit+fogged) built on top of them (🟨 landed
  2026-07-19 — `METAL-64`/`66`–`69`/`120`/`121`/`123`/`124`).
- Real per-vertex (Gouraud) `BasicEffect` lighting (`PipelineKind::LitTex32VertexLit`, ported from
  `EasyGLGraphicsBackend::EnsureLit3DVertexLitProgram()`'s real GLSL), correctly selected only when
  `lightingEnabled && !preferPerPixelLighting` (XNA's real default) — closes a real, visible,
  currently-shipping divergence from XNA/FNA where every lit draw silently used per-pixel shading
  regardless of the requested lighting model, for both `BasicEffect` and `SkinnedEffect`
  (`PipelineKind::Skinned52/56VertexLit`, ported from
  `EasyGLGraphicsBackend::EnsureSkinnedVertexLitProgram()`'s real GLSL) (🟨 landed 2026-07-19 —
  `METAL-39`/`76`).
- Real `SkinnedEffect` (72-bone GPU skinning via a real `MTLBuffer`, `WeightsPerVertex` branching,
  a NaN-safety guard for near-180°-relative-bone-rotation blends, lit/fog/specular/emissive) (🟨
  landed 2026-07-19 — `METAL-72`–`75`/`77`/`78`).
- Real `ReadBackbuffer` (blit-to-staging-buffer, no Y-flip needed) and real occlusion queries
  (`MTLVisibilityResultBuffer`, genuine `uint64_t` pixel counts — a real capability advantage over
  EasyGL's GLES3 boolean) (🟨 landed 2026-07-19 — `METAL-130`/`132`/`133`/`136`–`139`).
- Real `RenderTarget2D` (bind/unbind, sampleable afterward as an ordinary texture or sprite,
  correct sprite-transform math when rendering into it) — the architecturally riskiest change this
  session, since it touches the core frame/encoder lifecycle every draw path depends on; 4 real
  bugs found and fixed by self-review before commit (a pipeline/attachment pixel-format mismatch,
  a compile error, an incomplete-type compile error, and a silent-wrong-transform bug) (🟨 landed
  2026-07-19 — `METAL-98`–`101`/`106`/`107`; `RenderTargetCube`/MRT/MSAA/mip still open).
- Command-buffer/encoder lifecycle formally audited and a real premature-`presentDrawable:` bug
  fixed: mid-frame render-target switches previously presented the still-in-progress backbuffer up
  to once per switch instead of exactly once per `Present()` (🟨 landed 2026-07-19 — `METAL-173`–
  `181`; the texture-update CPU/GPU-sync hazard the same audit found is real but deliberately left
  open as `METAL-256`, see its own note on why a naive fix would lose mip-level content).
- Resize/fullscreen/Retina behavior confirmed correct **by reading this repo's own vendored SDL3
  Cocoa Metal-view source** rather than guessing — `drawableSize`/`contentsScale` are already
  managed automatically by SDL3 itself, no CNA Metal code needed; found one real, precisely-scoped,
  cross-backend HiDPI gap in shared window-creation code in the process, deliberately left open as
  `METAL-257` since fixing it is out of a Metal-only plan's scope (🟨 landed 2026-07-19 —
  `METAL-162`–`167`).
- Real `swapInterval` handling: previously stored but completely dead, now actually applied via
  `layer.displaySyncEnabled` at construction and on every `SetSwapInterval()` call, with the
  `CAMetalLayer`-has-no-true-half-rate-knob limitation for `PresentInterval.Two` explicitly
  documented rather than silently approximated (🟨 landed 2026-07-19 — `METAL-168`).
- Real `RenderTarget2D` mip-chain generation on unbind (`generateMipmapsForTexture:`, unconditional
  when `mipMap` was requested, matching `EasyGLRenderTargetBackend`'s own already-tested
  precedent); `preserveContents`/`GetColorGLHandle` confirmed already-correct with no code needed
  (🟨 landed 2026-07-19 — `METAL-102`/`103`/`108`).
- Real `RenderTargetCube` (per-face bind via `MTLRenderPassColorAttachmentDescriptor.slice`, one
  shared depth texture across faces, mip generation over the whole cube on unbind, sampleable as an
  `EnvironmentMapEffect` reflection source immediately after rendering into it); found and fixed a
  real latent `SetRenderTarget2D()`/`SetRenderTargetCubeFace()` cross-target-unbind gap (mip chains
  would never regenerate when switching directly between two render targets) and a `ReadBackbuffer()`
  guard gap, both in the same pass that made them observable (🟨 landed 2026-07-19 —
  `METAL-109`–`111`).
- Real `GetData()` readback on `RenderTarget2D`/`RenderTargetCube` via one shared blit-to-staging-
  buffer helper instead of two near-duplicate implementations, correctly flushing any still-pending
  render encoding for the target being read first (🟨 landed 2026-07-19 — `METAL-131`).
- Phase 1's remaining format-table questions resolved from real code, not assumption: `ImageData`
  has no format field at all (DXT is decompressed to RGBA8 at the shared `Texture2D` layer before
  reaching *any* backend), so the "hardcoded `RGBA8Unorm`"/BC-compression-query tasks were never
  real per-backend gaps; the depth-format simplification matches `VulkanGraphicsBackend`'s own
  already-accepted precedent; the anisotropy clamp is correct for a Metal-specific reason (a fixed
  API ceiling, unlike Vulkan/GL's genuinely device-queried ones) (🟨 landed 2026-07-19 —
  `METAL-15`–`18`/`20`; `METAL-14`/`19` remain genuinely open).
- Real `GetData()` on plain (non-render-target) `TextureCube`/`Texture3D` — a genuinely new finding,
  not scoped from this plan's own original text: `TextureCube.cpp`/`Texture3D.cpp`'s real code
  always calls `backend_->GetData()` unconditionally, unlike `Texture2D`'s own CPU-shadow-first
  shortcut, so both had a real, currently-shipping no-op-readback bug for any `SetData()`-populated
  (never rendered-into) cube/3D texture. Also retroactively hardened `blitTextureToClientBuffer()`'s
  `destinationBytesPerImage` argument against a real, previously-unverified Metal API risk found
  while generalizing it for `Texture3D`'s genuine volume copies (🟨 landed 2026-07-19 —
  `METAL-122`/`125`).
- iOS/tvOS platform-scope audit, from real vendored SDL3 source: `MetalGraphicsBackend.mm` itself
  has zero macOS-only API usage, and SDL3's own real `UIKit_Metal_CreateView`/`GetLayer`/
  `DestroyView` (the same `uikit` driver handling tvOS too) implement the identical generic
  function-pointer shape this file already exclusively calls — `cmake/BackendSelection.cmake`'s
  iOS/tvOS claim is likely true, not aspirational, though genuinely unverified until a real
  build-only CI job exists. Found the missing-`SDL_WINDOW_HIGH_PIXEL_DENSITY` gap (`METAL-257`)
  also affects iOS/tvOS, not just macOS. Touch input confirmed architecturally independent of the
  graphics backend layer entirely (🟨 landed 2026-07-19 — `METAL-243`/`246`).
- **The one genuine ✅ this entire plan earns tonight**: `MetalPipelineKey.hpp`, a plain-C++
  extraction of the pipeline-cache key/hash types with zero Objective-C dependency, real-built and
  real-tested via the actual `CnaTests` binary and `ctest` on this Linux machine — 8/8 tests, CTest
  #83–90, 100% pass (✅ landed 2026-07-19 — `METAL-34`).
- `METAL-40`'s CPU-side normal-matrix formula machine-verified the same way: `MetalNormalMatrix.hpp`,
  4 real `CnaTests`/`ctest` tests (identity, uniform scale, a hand-derived-via-independent-adjugate
  non-diagonal case, and a property-based perpendicularity-preservation check with an explicit
  negative-control assertion) — all pass on this Linux machine, real evidence for the single
  riskiest hand-derived formula in the whole lighting pipeline, though `METAL-40` overall stays 🟨
  pending the still-unverifiable MSL-side consumption (🟨 landed 2026-07-19 — `METAL-40`'s formula).
- `METAL-13`'s `primitiveVertexCount()` formula machine-verified the same way: `MetalPrimitiveVertexCount.hpp`,
  5 real `CnaTests`/`ctest` tests (one per `PrimitiveType`, including a dedicated `PointListEXT`
  regression test locking in the earlier `*3`-default bug fix), cross-checked against
  `EasyGLGraphicsBackend`'s own already-tested equivalent switch — all pass on this Linux machine
  (✅ landed 2026-07-19 — `METAL-13`, a third genuinely real, fully-earned tier tonight alongside
  `METAL-34`).
- `METAL-155`'s letterbox/overscan/stretch/native/fixed-height-dynamic-width formula machine-
  verified the same way: `MetalLogicalViewport.hpp`, 8 real `CnaTests`/`ctest` tests (one per
  `CnaPresentationMode`, plus edge cases) with hand-computed expected numbers for concrete
  pillarbox/overscan-crop/fixed-height scenarios — all pass on this Linux machine. Writing the tests
  found and locked in a real, previously-undocumented behavior (each physical axis clamps to zero
  independently, not both together) after an initial wrong test expectation caught it — a fourth
  genuinely real, fully-earned ✅ tier tonight, alongside `METAL-13`/`34` (✅ landed 2026-07-19 —
  `METAL-155`'s formula).
- The foundational WVP-matrix helper set (`Mat4`/`multiply`/`fromXna`/`transpose`, underlying every
  single 3D draw path this whole plan implements) machine-verified via `MetalMat4.hpp` and 7 real
  `CnaTests`/`ctest` tests — the strongest cross-validation of the night, checking the extracted
  `multiply`/`fromXna` combination against `Microsoft::Xna::Framework::Matrix::Multiply`'s own real,
  independently-implemented, already-in-production arithmetic rather than only hand-derived expected
  values — a fifth genuinely real, fully-earned ✅ tier tonight (🟨→✅, no single dedicated `METAL-N`
  task ID, foundational infrastructure underneath Phase 1's own WVP computation).
- `selectPipelineKind()`, the shader-variant dispatch decision every 3D draw call goes through,
  machine-verified via `MetalSelectPipelineKind.hpp` and 15 real `CnaTests`/`ctest` tests (CTest
  #115–129) covering every branch, every stride gate, and — deliberately — the full precedence order
  between `pbr`/`skinned`/`envMapping`/`dualTexture`/`textured`/`colored` by setting multiple flags at
  once and asserting only the highest-precedence kind wins — a sixth genuinely real, fully-earned ✅
  tier tonight (🟨→✅, no single dedicated `METAL-N` task ID, foundational dispatch infrastructure).
- Real `PbrEffect`, both unskinned and skinned (glTF 2.0 metallic-roughness Cook-Torrance BRDF,
  tangent-space normal mapping, all 4 optional PBR maps with safe default-texture fallbacks,
  `SkinnedPbrEffect` sharing the same fragment shader as its unskinned counterpart while adding the
  same 72-bone GPU-skinning blend Phase 7's `SkinnedEffect` uses) (🟨 landed 2026-07-19 —
  `METAL-81`–`88`; only `CTest` coverage `METAL-89` and the `METAL-90` doc decision remain open).
- Triangle list/strip, line list/strip, and point-list topology mapping, all verified against the
  real `PrimitiveType` ordinals (🟨 `PointListEXT` fix landed 2026-07-19 — `METAL-12`/`METAL-13`).
- Real cull/fill/depth-bias/viewport/scissor **and now depth-func/front+back-stencil/blend-factor/
  blend-color/per-`BlendState` blending** state plumbing, all XNA-enum-to-Metal mappings
  cross-checked against EasyGL's/Vulkan's own already-tested equivalents (🟨 landed 2026-07-19 —
  `METAL-6` through `METAL-13`, `METAL-24`).
- Explicit `setFrontFacingWinding:MTLWindingClockwise` at all 3 cull-mode call sites, verified (not
  just assumed) to match `VulkanGraphicsBackend`'s own explicit `VK_FRONT_FACE_CLOCKWISE` choice and
  Apple's documented Metal default — removes a real "silently correct by unstated default" risk
  without changing current rendering behavior (🟨 landed 2026-07-19 — `METAL-5`).
- `DiffuseColor`/`VertexColorEnabled`/`AlphaTest` (AlphaTestEffect, needing no separate pipeline —
  see the 2026-07-19 section above) and `DualTextureEffect` (real FNA-verified `*2` doubling
  formula) now reach the shader for every textured draw (🟨 landed 2026-07-19 — `METAL-35`–
  `METAL-37`, `METAL-51`–`METAL-61`).
- Native SpriteBatch path with texture sampling, source/destination rectangles, tint, rotation,
  origin, flip effects, **`SetTransformMatrix`, real per-`BlendState` 2D blending, and correct
  virtual-resolution/letterbox scaling** (no custom effect yet — see Phase 14) (🟨 landed
  2026-07-19 — `METAL-157`/`METAL-158`, `METAL-182`, `METAL-184`).
- Real window↔logical coordinate transforms (`TransformWindowToLogical`/`TransformLogicalToWindow`)
  and real `SetPresentationMode`/letterbox math, previously entirely absent — a real, currently-
  shipping mouse-input-on-scaled-windows bug, not just a graphics gap (🟨 landed 2026-07-19 —
  `METAL-153`–`METAL-159`).
- Per-slot sampler state with filter/address/anisotropy, backed by a real `MTLSamplerState` cache
  (🟨 landed 2026-07-19, not yet hardware-verified — `METAL-1`/`METAL-2`).
- Honest `SupportsCapability()` for the 3 capabilities this backend does not yet have (🟨 landed
  2026-07-19, not yet hardware-verified — `METAL-197`), later found to have a 4th real false
  positive (`MultiSampleAntiAliasing`) on a fresh Phase 20 review pass and fixed the same way — the
  remaining 4 capabilities' `true` defaults individually re-verified against real code, not just
  re-trusted (🟨 landed 2026-07-19 — `METAL-189`–`191`/`193`/`194`).
- Metal debug signposts via `insertDebugSignpost`.
- Dedicated macOS GitHub Actions compile job.

## Required next parity work — now tracked as phased tasks below

The original 20-item sketch is preserved here only as a cross-reference into the detailed phases
that replace it; do not re-derive scope from this table, use the phases:

| Original item | Now tracked in |
|---|---|
| 1. Pipeline-state cache keyed by blend/depth-stencil/rasterizer | Phase 2 |
| 2. Exact XNA enum → Metal mappings | Phase 1 |
| 3. Vertex-descriptor cache from `VertexDeclaration` | Phase 2 |
| 4. All BasicEffect shader variants | Phase 3 |
| 5. AlphaTestEffect/DualTextureEffect/EnvironmentMapEffect/SkinnedEffect | Phases 4–7 |
| 6. NOXNA PBR and instancing paths | Phases 8–9 |
| 7. RenderTarget2D/RenderTargetCube/MRT/MSAA/mip | Phase 10 |
| 8. TextureCube and Texture3D | Phase 11 |
| 9. GPU readback | Phase 12 |
| 10. Occlusion queries | Phase 13 |
| 11. Custom ShaderEffect / MSL contract | Phase 14 |
| 12. Sampler cache and anisotropic filtering | Phase 1 — **partially landed**, see above |
| 13. Virtual-resolution/letterbox transforms | Phase 15 |
| 14. Runtime resize/fullscreen/Retina | Phase 16 |
| 15. Frame pacing/presentation policy | Phase 17 |
| 16. Resource lifetime/command-buffer sync audit | Phase 18 |
| 17. Argument buffers / bindless NOXNA | Phase 21 |
| 18. Indirect command buffers / GPU-driven NOXNA | Phase 22 |
| 19. MetalFX NOXNA upscaling | Phase 23 |
| 20. GPU counter capture / Xcode Frame Capture docs | Phase 24 |
| *(new)* SpriteBatch full parity | Phase 19 |
| *(new)* `SupportsCapability` accuracy | Phase 20 — **partially landed**, see above |
| *(new)* Testing infrastructure | Phase 25 |
| *(new)* CI / tooling | Phase 26 |
| *(new)* Documentation | Phase 27 |
| *(new)* Cross-backend pixel parity | Phase 28 |
| *(new)* iOS / tvOS platform scope | Phase 29 |
| *(new)* Additional NOXNA opportunities found during this audit | Phase 30 |

---

## Phase 1 — Exact enum mappings and state-cache correctness (METAL-1 – METAL-20)

| ID | Task | Status |
|---|---|---|
| METAL-1 | `ApplySamplerState(slot,filter,addressU,addressV,maxAnisotropy)` + `Impl::samplerFor()` cache (`TextureFilter`→min/mag/mip, `TextureAddressMode`→address mode, `Anisotropic`→`maxAnisotropy`), wired into `drawMetal3D` texture unit 0 | 🟨 |
| METAL-2 | Wire the same cache into `MetalSpriteBatch::Draw()` via a new `SetSamplerAddressMode()` override (previously `filter_` was set but never read, and address mode had no override at all) | 🟨 |
| METAL-3 | Extend sampler-slot consultation beyond unit 0 in `drawMetal3D` — needed once DualTextureEffect (Phase 5, unit 1) / EnvironmentMapEffect (Phase 6) / PBR (Phase 8, up to 4 map units) land | ⬜ |
| METAL-4 | Audit `TextureFilter::Anisotropic` mapping (min/mag/mip = Linear + `maxAnisotropy`) against real Apple GPU behavior — no surprising clamp/driver quirk | ⬜ |
| METAL-5 | `CullMode`→`MTLCullMode` + `MTLWinding` audit: current code (`c==1?Front:(c==2?Back:None)`) never calls `setFrontFacingWinding:`, relying on Metal's default winding — cross-check against `VulkanGraphicsBackend`'s tested `VkFrontFace`/`VkCullModeFlags` mapping and set winding explicitly instead of assuming a default | 🟨 |
| METAL-6 | `Blend`/`BlendFunction`→`MTLBlendFactor`/`MTLBlendOperation` full table (Zero/One/SourceColor/InverseSourceColor/SourceAlpha/InverseSourceAlpha/DestinationAlpha/InverseDestinationAlpha/DestinationColor/InverseDestinationColor/SourceAlphaSaturation/BlendFactor/InverseBlendFactor; Add/Subtract/ReverseSubtract/Max/Min) — `ApplyBlendState` is currently a complete no-op | 🟨 |
| METAL-7 | `CompareFunction`→`MTLCompareFunction` full table (Always/Never/Less/LessEqual/Equal/GreaterEqual/Greater/NotEqual) — `rebuildDepthState()` currently hardcodes only LessEqual/Always, ignoring the real `depthFunc` parameter | 🟨 |
| METAL-8 | `StencilOperation`→`MTLStencilOperation` full table (Keep/Zero/Replace/Increment/Decrement/IncrementSaturation/DecrementSaturation/Invert) — no stencil-op plumbing exists at all today, only a depth compare function and reference value | 🟨 |
| METAL-9 | Wire the real front-face stencil test (`stencilEnable`/`stencilFunc`/`stencilPass`/`stencilFail`/`stencilDepthFail`/`stencilMask`/`stencilWriteMask`) into `MTLDepthStencilDescriptor.frontFaceStencil`, currently entirely ignored by `ApplyDepthStencilState` | 🟨 |
| METAL-10 | Wire two-sided stencil (`twoSidedStencilMode`/`ccwStencilFunc`/`ccwStencilPass`/`ccwStencilFail`/`ccwStencilDepthFail`) into `MTLDepthStencilDescriptor.backFaceStencil` | 🟨 |
| METAL-11 | `SetBlendFactor(r,g,b,a)` via `[encoder setBlendColor:...]` — currently unimplemented (base no-op) | 🟨 |
| METAL-12 | `PrimitiveType`→`MTLPrimitiveType`: add the missing `PointList` case (currently falls through `metalPrimitive()`'s default to `Triangle`, silently wrong) | 🟨 |
| METAL-13 | `primitiveVertexCount()`: fix the `PointList` case (currently falls to default `count*3`; should be `count`) | 🟨→✅ **the formula itself is now real-build-verified** — extracted to `MetalPrimitiveVertexCount.hpp` (same `METAL-34` technique) and covered by 5 real `CnaTests`/`ctest` tests, one per `PrimitiveType` value including a dedicated `PointListEXT` regression test locking this exact fix in, all passing on this Linux machine, cross-checked against `EasyGLGraphicsBackend`'s own already-tested equivalent switch |
| METAL-14 | `VertexElementFormat`→`MTLVertexFormat` full table (Single/Vector2/Vector3/Vector4/Color/Byte4/Short2/Short4/NormalizedShort2/NormalizedShort4/HalfVector2/HalfVector4) — today only 3 hand-picked fixed vertex descriptors exist, no general element-format mapping | ⬜ (still genuinely open — this is real, unlike `METAL-15`/`17` below, and stays scoped under Phase 2's already-deferred generic `VertexElement`-driven descriptor builder, `METAL-26`/`27`; do not attempt in isolation, see that phase's own note) |
| METAL-15 | `SurfaceFormat`→`MTLPixelFormat` table (Color/Bgr565/Bgra5551/Bgra4444/Dxt1/Dxt3/Dxt5/NormalizedByte2/NormalizedByte4/Rgba1010102/Rg32/Rgba64/Alpha8/Single/Vector2/Vector4/HalfSingle/HalfVector2/HalfVector4/HdrBlendable) — every texture is currently hardcoded `RGBA8Unorm` regardless of `ImageData`'s real format | 🟨 (**found to be based on a false premise**: `include/CNA/Internal/Graphics/ImageData.hpp` has no format field at all — its own doc comment states "RGBA8 pixel data," and `Texture2D.cpp`'s own real code confirms DXT1/DXT5 are decompressed to RGBA8 via `DxtUtil::DecompressDxt1`/`DecompressDxt5` *before* `CreateTexture(img)` is ever called, for every backend uniformly, not just Metal. There is no "real format" for `CreateTexture()` to diverge from — this was never a Metal-specific gap) |
| METAL-16 | `DepthFormat`→`MTLPixelFormat` table (None/Depth16/Depth24/Depth24Stencil8) — backbuffer currently always allocates `Depth32Float_Stencil8` regardless of what `PresentationParameters` requested | 🟨 (confirmed intentional, not overlooked: matches `VulkanGraphicsBackend`'s own already-accepted "always allocate depth+stencil" simplification, explicitly called out as the deliberately-chosen tier back in `METAL-101`'s own note — not a priority fix) |
| METAL-17 | Query `MTLDevice.supportsBCTextureCompression` and document the real, device-dependent DXT/BC boundary (no native support on Apple Silicon without emulation; yes on Intel Macs) rather than assuming universal support | 🟨 (confirmed moot under the current architecture: since `METAL-15`'s finding means nothing ever uploads real DXT/BC bytes to any backend today, this query has nothing to gate yet — would only become relevant if a future, genuinely different, cross-backend "upload real compressed texture data" path bypassing `ImageData` were ever added, which is a project-wide feature, not a Metal-only one) |
| METAL-18 | Centralize every mapping above into one shared location so Phase 2's pipeline cache and Phase 10's render-target/format work reuse one source of truth instead of duplicating switch statements | 🟨 (scope reduced by the `METAL-15`/`17` findings above — the enum-mapping tables that genuinely exist and matter today, e.g. `metalCompareFunction`/`metalStencilOp`/`metalBlendFactor`/`metalBlendOp`/`metalPrimitive`, already live together near the top of `kMetalShaderSource`'s surrounding helpers; no separate centralization pass is needed until `METAL-14`'s real `VertexElementFormat` table actually gets built) |
| METAL-19 | Guard against silent enum-reordering regressions (a compile-time or `GraphicsBackendCompileDefinitionsTest`-style check that these ordinal assumptions still match the real `.hpp` files) | ⬜ (genuinely still open — needs a real CTest/compile-time check, not reachable from this Linux machine without an Apple toolchain) |
| METAL-20 | `MTLSamplerDescriptor.maxAnisotropy` valid-range audit (clamped 1–16 in `samplerFor()` today) — confirm this matches `TextureFilter`/`SamplerState.MaxAnisotropy`'s real XNA range | 🟨 (confirmed correct, and for a different reason than `VulkanGraphicsBackend`'s own analogous clamp: Apple's own `MTLSamplerDescriptor.maxAnisotropy` documentation states 16 is a *fixed, hardware-independent* API ceiling — unlike Vulkan's `VkPhysicalDeviceLimits.maxSamplerAnisotropy`/GL's `GL_MAX_TEXTURE_MAX_ANISOTROPY`, both genuinely device-queried because their real upper bound varies by GPU, Metal's existing hardcoded `std::clamp(maxAnisotropy,1,16)` needs no device query at all) |

## Phase 2 — Pipeline-state cache and generic `VertexDeclaration`-driven vertex descriptor (METAL-21 – METAL-34)

Metal bakes the shader pair, vertex descriptor, blend equation, and attachment pixel formats into
`MTLRenderPipelineState` at creation time — unlike depth-stencil/cull/fill/viewport/scissor, which
are genuinely dynamic encoder state already correctly handled. This phase is the real answer to
"pipeline-state cache keyed by the full CNA blend/depth-stencil/rasterizer state": only the *baked*
subset (shader variant, vertex layout, blend, attachment formats) needs a cache; the dynamic subset
already works and must not be redesigned by mistake.

| ID | Task | Status |
|---|---|---|
| METAL-21 | Written classification of which CNA render state is Metal *pipeline* state vs. *encoder* state — drives every task below, must be correct once, not re-derived per task | ⬜ |
| METAL-22 | `struct MetalPipelineKey` (shader variant + vertex layout hash + blend fields + attachment pixel formats) with hash/equality for `std::unordered_map` | 🟨 |
| METAL-23 | Replace the 5 fixed named pipeline fields (`pipe3Color`/`pipe3Tex20`/`pipe3ColorTex24`/`pipe3NormalTex32`/`pipe2`) with `std::unordered_map<MetalPipelineKey, id<MTLRenderPipelineState>>` + `getOrCreatePipeline(key)` | 🟨 |
| METAL-24 | Extend `makePipeline()` to accept full blend-attachment fields driven by `METAL-6`'s table, instead of the currently hardcoded straight-alpha blend baked into every pipeline | 🟨 |
| METAL-25 | Release-on-destruction for the new pipeline cache (mirrors the sampler-cache destructor pattern added in `METAL-1`) | 🟨 |
| METAL-26 | `SetVertexDeclaration(const std::vector<VertexElement>&)` override on `MetalVertexBuffer` (the `IVertexBufferBackend` contract already documents this as Task 1080's generic-layout hook) | ⬜ |
| METAL-27 | Build a generic `MTLVertexDescriptor` from a `VertexElement` list via `METAL-14`'s format table — replaces the 4 hand-written `vd16`/`vd20`/`vd24`/`vd32` descriptors with a path that also covers strides 28/36/40/44/48/52/56/68 once Phases 5–8 need them | ⬜ |
| METAL-28 | Fallback: when `SetVertexDeclaration` was never called, keep the existing stride-based inference for the 4 strides that already work — no regression | ⬜ |
| METAL-29 | `selectPipelineKey(stride, elements, GpuDrawParams)` dispatcher replicating `EasyGLGraphicsBackend::SelectProgram()`'s exact precedence (pbr+skinned → pbr → skinned(±vertexlit) → envMapping → dualTexture(stride-24 colored variant) → stride switch 20/24/32(±vertexlit) → default colored) | 🟨 |
| METAL-30 | Regression-proof: every existing stride-16/20/24/32 path must select byte-identical pipelines before/after the cache rewrite — a Linux-side manual trace against the current 5-pipeline logic, ahead of any macOS build | ⬜ |
| METAL-31 | Key pipelines by color/depth/stencil attachment pixel format (backbuffer BGRA8 vs. an RGBA8/other `RenderTarget2D` once Phase 10 lands — Metal pipelines are format-specific) | ⬜ |
| METAL-32 | Key pipelines by attachment sample count once Phase 10 adds MSAA | ⬜ |
| METAL-33 | Document the expected cache size/no-eviction-needed-for-v1 assumption (mirrors EasyGL's own per-field `Prog3D` bound-variant assumption); flag unbounded-growth as a NOXNA follow-up only if a real pathological case appears | ⬜ |
| METAL-34 | Extract `MetalPipelineKey`'s hash/equality into an `#ifdef __OBJC__`-free plain-C++ header so it can be exercised by a normal GoogleTest binary **without an Apple toolchain** — the one piece of Phase 2 genuinely build-verifiable on this Linux machine today | ✅ **real, on this Linux machine, 2026-07-19** — 8/8 new tests (`MetalBlendKey.*`/`MetalPipelineCacheKey.*`/`MetalPipelineCacheKeyHash.*`, CTest #83–90) pass under the real `CnaTests` binary (`cmake -DCNA_GRAPHICS_BACKEND=HEADLESS -DCNA_BUILD_TESTS=ON`, `cmake --build --target CnaTests`, `ctest -R 'MetalBlendKey\|MetalPipelineCacheKey'`) — the first, and only, task in this entire plan to genuinely earn this tier tonight, matching `METAL-238`'s own "cite the actual CTest name" discipline |

## Phase 3 — BasicEffect full shader parity (METAL-35 – METAL-50)

Reference implementations already shipped and tested: `EasyGLGraphicsBackend::EnsureColored3DProgram`/
`EnsureTextured3DProgram`/`EnsureColoredTextured3DProgram`/`EnsureLit3DProgram`/
`EnsureLit3DVertexLitProgram`, and `GpuDrawParams`' fog/lighting/specular fields.

| ID | Task | Status |
|---|---|---|
| METAL-35 | `colored3d.metal`: honor `diffuseColor`/`vertexColorEnabled` (fragment shader currently just returns the raw vertex color, ignoring both fields) | 🟨 |
| METAL-36 | `textured3d.metal`: multiply by `diffuseColor` (currently hardcodes `color=(1,1,1,1)`, i.e. `DiffuseColor` has zero effect) | 🟨 |
| METAL-37 | `colortex3d.metal`: same `diffuseColor` multiply for the vertex-color+texture combined path | 🟨 |
| METAL-38 | Per-pixel lit shader (`lit_textured3d.metal`, stride 32): port FNA's `Lighting.fxh`/`ComputeLights()` (3 directional lights, ambient, Blinn-Phong specular, emissive) — direct MSL port from `VulkanGraphicsBackend`'s already-shipped GLSL or `EnsureLit3DProgram()`, not new design | 🟨 |
| METAL-39 | Per-vertex (Gouraud) lit shader (`lit_textured3d_vertexlit.metal`), selected when `lightingEnabled && !preferPerPixelLighting` (XNA's real default, Task 1102) — Metal currently has **no** lighting shader of either kind | 🟨 |
| METAL-40 | Normal matrix (`inverse(world3x3)`, no shader-side transpose given this codebase's column-major GPU convention) computed CPU-side, cross-verified against `BgfxGraphicsBackend::ComputeNormalMatrix3x3` | 🟨→✅ **the CPU-side formula itself is now real-build-verified** — extracted to `MetalNormalMatrix.hpp` (same `METAL-34` technique) and covered by 4 real `CnaTests`/`ctest` tests (identity, uniform scale, a hand-derived-via-independent-adjugate-method non-diagonal case, and a property-based perpendicularity-preservation check), all passing on this Linux machine — stronger than the originally-planned "cross-verify against Bgfx's implementation" since it proves the math is correct in an absolute sense, not just consistent with another backend. The MSL-side *consumption* of this data (`float3x3(col0,col1,col2)` in the shader) remains unverified — needs a real Metal compiler — so the task as a whole stays 🟨, but its riskiest, most error-prone piece (the arithmetic) is no longer just "hand-verified by reasoning," it is machine-verified |
| METAL-41 | Safe-normalize guard for a disabled/zero-direction light — **audited 2026-07-19**: `cna_f3d_lit` (ported verbatim from `EnsureLit3DProgram()`, not WebGPU's own shader) never normalizes a raw light-direction vector directly — only `dot(N,-lightDir)` (zero when `lightDir=0`, safe) and `normalize(E-lightDir)` (degrades to `normalize(E)`, already unit-length, safe) — so this specific formula structure has no zero-vector `normalize()` call to guard, unlike WebGPU's own shader shape. Left ⬜ rather than claiming done: not verified on real hardware, and EasyGL's own GLSL (the ground truth here) has no matching guard either | ⬜ |
| METAL-42 | Fog: `fogEnabled`/`fogColor`/`fogStart`/`fogEnd` plumbing + eye-space-Z linear blend in every textured/lit fragment variant — zero fog support exists today | 🟨 |
| METAL-43 | Specular: `specularColor`/`specularPower` Blinn-Phong term using `eyePositionWorld`, applied once at material level | 🟨 |
| METAL-44 | Emissive: `emissiveColor` additive term after the ambient/light-sum multiply, matching CNA's ambient-folded-into-multiply convention (documented on the field itself) | 🟨 |
| METAL-45 | Audit whether a `oneLight`-only fast-path shader variant is worth adding (EasyGL/Vulkan precedent check) or whether the general 3-light path is sufficient since disabled lights are already zeroed | ⬜ |
| METAL-46 | Extend the single `U3D{float4x4 wvp}` uniform to a full `BasicEffectUniforms` struct (world matrix, ambient, 3 lights' dir/diffuse/specular, material diffuse/specular/emissive, specular power, fog, eye position), 16-byte-aligned | 🟨 |
| METAL-47 | `BindDrawParams()`-equivalent Metal-side function filling `BasicEffectUniforms` from `GpuDrawParams`, field-for-field matching `EasyGLGraphicsBackend::BindDrawParams()` | 🟨 |
| METAL-48 | `VertexColorEnabled=false` path must ignore the vertex color attribute even when physically present in the buffer — verify against FNA's exact semantics | ⬜ |
| METAL-49 | Regression tests, one axis at a time: texture on/off, vertex-color on/off, fog on/off, lighting off/vertex/pixel, specular zero/non-zero, emissive zero/non-zero — same coverage `docs/basiceffect-support.md` tracks for other backends | ⬜ |
| METAL-50 | Add a `Metal` column to `docs/basiceffect-support.md` (currently absent) | ⬜ |

## Phase 4 — AlphaTestEffect (METAL-51 – METAL-57)

| ID | Task | Status |
|---|---|---|
| METAL-51 | `alpha_test3d.metal` fragment `discard_fragment()` using `alphaTest[4]={refVal,tolerance,passWeight,failWeight}`, porting the exact formula already documented on `GpuDrawParams::alphaTest` and shipped in `VulkanGraphicsBackend`/`WebGPUGraphicsBackend` | 🟨 |
| METAL-52 | Stride 20 (`VertexPositionTexture`) and stride 24 (`VertexPositionColorTexture`) variants, one shared vertex shader where position+UV suffices (EasyGL/WebGPU precedent) | 🟨 |
| METAL-53 | Stride 32 (`VertexPositionNormalTexture`) variant — normal attribute unused, no lighting in this effect | 🟨 |
| METAL-54 | Confirm whether Metal needs the `isEqNe` real-XNA-shader-variant divergence tracking (`plan_dx9.md` D9-81) the way D3D9 does, or stays in the "not fixed outside D3D9" bucket every other backend is already in | 🟨 (confirmed: stays in the "not fixed outside D3D9" bucket — `isEqNe` exists only because real D3D9/XNA precompiles fixed shader-index *permutations*, `ComputeAlphaTestEffectShaderIndex()`'s `if (isEqNe) shaderIndex += 4`, and `Equal`/`NotEqual` need a distinct bucket in that scheme; Metal, like EasyGL/Vulkan/Bgfx before it, uses one generic runtime-compiled shader with a branching `alphaTest` uniform comparison (`cna_alpha_test_fails()`) that already handles all 8 `CompareFunction` values with no separate variant needed at all) |
| METAL-55 | Dispatch: replicate `EasyGLGraphicsBackend::SelectProgram()`'s exact "default `{0,0,1,1}` = always-pass = skip the alpha-test shader" selection signal precisely | 🟨 |
| METAL-56 | `CTest`: `Metal_AlphaTest` — pass/fail/tolerance-boundary cases | ⬜ |
| METAL-57 | Add a `Metal` column to `docs/alphatesteffect-support.md` | ⬜ |

## Phase 5 — DualTextureEffect (METAL-58 – METAL-63)

| ID | Task | Status |
|---|---|---|
| METAL-58 | `dual_texture3d.metal` (stride 20, colorless) — second sampler/texture (`texture1`, unit 1) blended with `texture0` per FNA's `DualTextureEffect.fx` | 🟨 |
| METAL-59 | `dual_textured_colored3d.metal` (stride 24, vertex-color-aware) — EasyGL Task 889's exact stride-24-needs-its-own-program finding, replicated not merged | 🟨 |
| METAL-60 | Wire `ApplySamplerState(1,...)` (`METAL-3`) so `texture1` gets its own independent filter/address mode, matching real XNA `DualTextureEffect.Texture2` semantics | 🟨 |
| METAL-61 | Dispatch: `params.dualTexture` branch checked before the plain stride switch, after `envMapping`, matching EasyGL's exact precedence | 🟨 |
| METAL-62 | `CTest`: `Metal_DualTexture` — two distinct textures visibly blend, probe-pixel-verified | ⬜ |
| METAL-63 | Add a `Metal` column to `docs/dualtextureeffect-support.md` | ⬜ |

## Phase 6 — EnvironmentMapEffect (METAL-64 – METAL-71)

> **Second real dependency found 2026-07-19** (Phase 11's cube-texture blocker on `METAL-65` is now
> closed — `MetalTextureCube` landed): reading `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()`
> in full shows real XNA `EnvironmentMapEffect.fx` routes through the exact same 3-directional-light
> `ComputeLights()`/normal-matrix/fog machinery as `BasicEffect`'s own lit path (`litRGB =
> lightSum*DiffuseColor.rgb + EmissiveColor`, fog applied after) — it is not a simpler,
> lighting-free reflection-only shader. That lighting/normal-matrix/fog infrastructure is exactly
> what Phase 3's still-open `METAL-38`–`METAL-50` would build and hasn't yet. Shipping a
> lighting-free approximation here would silently diverge from XNA's real formula, not just be
> incomplete — worse than leaving this phase open. **Phase 6 is therefore blocked on Phase 3, not
> just Phase 11**; do not attempt `METAL-64`/`METAL-66`–`METAL-68` before `METAL-38`–`METAL-47` land.

| ID | Task | Status |
|---|---|---|
| METAL-64 | `env_mapped3d.metal` (stride 32) — reflection vector from `eyePositionWorld`/normal/world position, sampling `envMap` as a `texturecube<float>` | 🟨 |
| METAL-65 | Bind `ITextureCubeBackend`'s underlying cube `id<MTLTexture>` to the shader's `texturecube<float>` argument — **unblocked 2026-07-19**: `MetalTextureCube::native()` exists (`METAL-120`/`METAL-121`) | 🟨 |
| METAL-66 | Flat env-map blend (`envMapAmount`, constant factor) | 🟨 |
| METAL-67 | Fresnel-weighted env-map blend (`fresnelEnabled`/`fresnelFactor`), ported from the already-shipped WebGPU/Vulkan formula | 🟨 |
| METAL-68 | `envMapSpecular` tint and `specularEnabled` real-shader-variant flag (D9-81 finding #4) — same "known, not fixed outside D3D9" bucket as `METAL-54` | 🟨 |
| METAL-69 | Dispatch: `params.envMapping` checked before `dualTexture`, matching EasyGL's exact precedence | 🟨 |
| METAL-70 | `CTest`: `Metal_EnvironmentMap` — flat and Fresnel-weighted variants, reflection-angle-dependent probe pixel | ⬜ |
| METAL-71 | Add a `Metal` column to `docs/environmentmapeffect-support.md` | ⬜ |

## Phase 7 — SkinnedEffect (METAL-72 – METAL-80)

| ID | Task | Status |
|---|---|---|
| METAL-72 | Skinned vertex layout: stride 52 (no vertex color) and stride 56 (with vertex color, per `WebGPUGraphicsBackend::GetOrCreatePipelineSkinned3D`'s `hasVertexColor=(stride==56)` precedent) | 🟨 |
| METAL-73 | `boneTransforms[72*16]`/`boneCount` uniform — 4,608 floats (18KB) **exceeds** `setVertexBytes:`'s 4KB inline limit; must be a real `MTLBuffer`, not the inline path every other uniform in this file currently uses — a concrete, easy-to-get-silently-wrong detail called out explicitly | 🟨 |
| METAL-74 | `weightsPerVertex` (1/2/4) — real XNA `Skin(vin, boneCount)` only sums the first N pairs (Task 895); the MSL vertex shader must branch on this, not always sum all 4 | 🟨 |
| METAL-75 | Per-pixel-lit skinned variant (`EnsureSkinnedProgram()` equivalent) | 🟨 |
| METAL-76 | Per-vertex-lit skinned variant (`EnsureSkinnedVertexLitProgram()` equivalent), same `preferPerPixelLighting` XNA-default logic as BasicEffect (Task 1102b) | 🟨 |
| METAL-77 | Confirm skinned normals are transformed by each bone's own 3×3, not the single mesh-level normal matrix `METAL-40` computes for unskinned draws — **confirmed 2026-07-19, more precisely than originally worded**: the reference shader applies *no* world-space normal-matrix step at all for skinned draws (only `mat3(skinMat)`, the bone blend's own upper-left 3x3), so `SkinnedTransform` correctly carries no `normalCol0/1/2` fields at all, unlike `LitTransform`/`EnvTransform` | 🟨 |
| METAL-78 | Dispatch: `params.skinned` checked before `envMapping`/`dualTexture`, combined with `params.pbr` for the skinned-PBR case (Phase 8) | 🟨 |
| METAL-79 | `CTest`: `Metal_Skinned` — a 2-bone rig with known transforms, probe-vertex-position-dependent pixel check | ⬜ |
| METAL-80 | Add a `Metal` column to `docs/skinnedeffect-support.md` | ⬜ |

## Phase 8 — NOXNA PBR / SkinnedPbr (METAL-81 – METAL-90)

| ID | Task | Status |
|---|---|---|
| METAL-81 | `pbr3d.metal` (stride 48, unskinned) — metallic-roughness Cook-Torrance BRDF, ported from `EasyGLGraphicsBackend::EnsurePbrProgram()`/Vulkan's shipped GLSL (`plan_cnj.md` CNB-58, Phase 13A) | 🟨 |
| METAL-82 | `pbr_skinned3d.metal` (stride 68) — same BRDF, combined with Phase 7's skinned vertex path | 🟨 |
| METAL-83 | `pbrNormalMap` tangent-space perturbation — requires adding a tangent attribute not present in any current Metal vertex layout | 🟨 |
| METAL-84 | `pbrMetallicRoughnessMap` (glTF packing: G=roughness, B=metallic) + `pbrMetallicFactor`/`pbrRoughnessFactor` fallback/multiply | 🟨 |
| METAL-85 | `pbrEmissiveMap` sampling + constant `EmissiveFactor` fallback | 🟨 |
| METAL-86 | `pbrOcclusionMap` sampling (R channel) darkening term | 🟨 |
| METAL-87 | Default-white / default-flat-normal fallback textures for unbound PBR maps, mirroring `EnsureDefaultWhiteTexture()`/`EnsureDefaultFlatNormalTexture()` exactly | 🟨 |
| METAL-88 | Dispatch: `params.pbr && params.skinned` → PBR-skinned, `params.pbr` alone → PBR-unskinned, both checked before the plain `skinned` branch, matching EasyGL's top-of-function precedence | 🟨 |
| METAL-89 | `CTest`: `Metal_Pbr`/`Metal_PbrSkinned` — known-material probe-pixel checks (fully metallic vs. dielectric, rough vs. smooth), same fixture as Vulkan/EasyGL's own PBR tests | ⬜ |
| METAL-90 | Confirm whether a project-wide PBR support doc should exist (out of Metal's own scope) or `plan_cnj.md` remains the single source of truth — note the decision | ⬜ |

## Phase 9 — Instancing (METAL-91 – METAL-97)

> **Real dependency found 2026-07-19 (investigated, not implemented)**: reading
> `EasyGLGraphicsBackend::DrawInstancedPrimitivesEx` line-by-line shows its **non**-custom-effect
> path (i.e. a stock `BasicEffect`-style draw) binds no per-instance vertex attributes at all and
> just issues `draw_elements_instanced(..., instanceCount)` — every instance renders at the
> identical position, since none of CNA's stock shaders read any per-instance offset. Real, useful
> instancing in this codebase **always** goes through `GpuDrawParams::customEffectBackend` (a
> custom `Effect` whose own shader reads per-instance data via a `VertexBufferBinding` with
> `InstanceFrequency>0`, matching FNA's own `InstancedModel.fx`/`instanceTransform:BLENDWEIGHT`
> pattern). That means `METAL-91`–`METAL-96` below have a **hard prerequisite this task list
> under-specified**: Phase 14 (custom `ShaderEffect`/MSL contract, not started) and the generic
> `VertexElement`-driven vertex descriptor (`METAL-26`/`METAL-27`, deliberately deferred in
> Phase 2). Attempting a Metal-specific instancing design ahead of those would either diverge from
> the real, established contract or ship a technically-present-but-functionally-inert override —
> correctly not attempted this pass. Revisit only after Phase 14 lands.

| ID | Task | Status |
|---|---|---|
| METAL-91 | `DrawInstancedPrimitivesEx` override — currently unimplemented, inherits the base's unconditional throw | ⬜ |
| METAL-92 | Per-instance vertex buffer (`GpuDrawParams::instanceVb`) bound at a distinct index with `stepFunction:MTLVertexStepFunctionPerInstance` | ⬜ |
| METAL-93 | `instanceCount` plumbed into `drawIndexedPrimitives:...instanceCount:`/`drawPrimitives:...instanceCount:` | ⬜ |
| METAL-94 | Confirm the exact per-instance vertex layout CNA's existing instancing call sites expect, rather than assuming one — **confirmed 2026-07-19**: it is whatever `VertexElement` list the bound custom `Effect`'s per-instance `VertexBufferBinding` declares, not a fixed layout | ⬜ |
| METAL-95 | `MetalPipelineKey` (`METAL-22`) must include an "is instanced" bit — instanced vs. non-instanced draws of the same shader variant need distinct vertex descriptors | ⬜ |
| METAL-96 | `CTest`: `Metal_Instancing` — N instances at N distinct positions, N distinct probe pixels | ⬜ |
| METAL-97 | GPU-driven/indirect instancing explicitly out of this phase's scope — see Phase 22 | ⬜ |

## Phase 10 — RenderTarget2D/Cube, MRT, MSAA, mip generation (METAL-98 – METAL-119)

| ID | Task | Status |
|---|---|---|
| METAL-98 | `CreateRenderTarget2D(w,h,depthFormat,preserveContents,mipMap,multiSampleCount)` — currently returns `nullptr` (base default); confirm the exact current upstream failure mode as a baseline first | 🟨 |
| METAL-99 | `MetalRenderTargetBackend : IRenderTargetBackend, ITextureBackend` — private `id<MTLTexture>` with `MTLTextureUsageRenderTarget \| MTLTextureUsageShaderRead` | 🟨 |
| METAL-100 | `BindAsRenderTarget()`/`UnbindAsRenderTarget()` — swap the active `MTLRenderPassDescriptor`'s attachments, ending/starting encoders (Metal render passes are fixed-attachment for their whole encoder lifetime, unlike GL's dynamic FBO rebinding) | 🟨 |
| METAL-101 | Honor `depthFormat` exactly per target (`METAL-16`'s table) — aim for EasyGL/Bgfx's "honor the exact requested format" tier, not Vulkan's documented "always allocate depth+stencil" simplification (Task 911/877) | 🟨 |
| METAL-102 | `preserveContents` → `MTLLoadActionDontCare`/`MTLLoadActionLoad` on rebind, matching the shared `GraphicsDevice.cpp` contract every backend already honors identically | 🟨 (found: already correctly handled, no code needed — see narrative) |
| METAL-103 | `mipMap` — full mip chain via `MTLBlitCommandEncoder::generateMipmapsForTexture:` on unbind, matching FNA3D's `OPENGL_ResolveTarget` auto-mip semantics (Task 336/878/906 precedent) | 🟨 |
| METAL-104 | `multiSampleCount` — multisampled attachment resolved via the render pass's own `MTLStoreActionMultisampleResolve` (a cheaper, first-class Metal path vs. GL's separate blit) | ⬜ |
| METAL-105 | `GetMultiSampleCount()` — real, device-queried clamp via `MTLDevice.supportsTextureSampleCount:`, matching every backend's "report the real clamped value" contract | ⬜ |
| METAL-106 | `HasRealDepthBuffer(bool)` — confirm the default `= depthFormatWasRequested` is already correct once real depth-format honoring (`METAL-101`) lands | 🟨 |
| METAL-107 | `SetRenderTarget2D(IRenderTargetBackend*)` — real bind/unbind dispatch, currently a no-op | 🟨 |
| METAL-108 | `GetColorGLHandle()` — confirm the default `return 0` is correct (GL-specific, N/A on Metal) and no caller assumes nonzero means "has a render target" | 🟨 (confirmed: zero callers anywhere in `include/`/`src/` outside its own declaration/`EasyGLRenderTargetBackend` override — nothing branches on it) |
| METAL-109 | `CreateRenderTargetCube(size,depthFormat,mipMap,multiSampleCount)` — `MetalRenderTargetCubeBackend : IRenderTargetCubeBackend`, `id<MTLTexture>` with `MTLTextureTypeCube` | 🟨 |
| METAL-110 | `BindAsRenderTargetFace(int face)` — per-face `MTLRenderPassDescriptor` color attachment using `slice:face` | 🟨 |
| METAL-111 | `SetRenderTargetCubeFace(rt,face)` — verify the base default's composition (`rt ? rt->BindAsRenderTargetFace(face) : SetRenderTarget2D(nullptr)`) is already correct once `METAL-110`/`METAL-107` land, before writing a redundant override | 🟨 (found: base default is NOT sufficient once mip-gen-on-unbind exists — a real override was needed, see narrative) |
| METAL-112 | `SetRenderTargets(rts[],count)` — real MRT (up to 8 simultaneous color attachments), replacing the base default's "bind only the first target" | ⬜ |
| METAL-113 | `MetalPipelineKey` must include the *set* of attachment pixel formats, not just one, once MRT lands | ⬜ |
| METAL-114 | `CTest`: `Metal_RenderTarget2D` — bind+clear+draw+unbind+readback (depends on Phase 12) | ⬜ |
| METAL-115 | `CTest`: `Metal_RenderTargetCube` — per-face bind+clear+readback+independence check | ⬜ |
| METAL-116 | `CTest`: `Metal_RenderTarget_MSAA` — device-clamped MSAA clear+resolve, pixel-verified | ⬜ |
| METAL-117 | `CTest`: `Metal_RenderTarget_Mip` — auto-mip-on-unbind, sampled at a non-zero mip level | ⬜ |
| METAL-118 | `CTest`: `Metal_MRT` — 2+ simultaneous targets, independent per-target clear-color proof | ⬜ |
| METAL-119 | Add a `Metal` column to `docs/rendertarget-support.md` | ⬜ |

## Phase 11 — TextureCube / Texture3D (METAL-120 – METAL-129)

| ID | Task | Status |
|---|---|---|
| METAL-120 | `CreateTextureCube(size,mipMap,surfaceFormat)` — `MetalTextureCubeBackend : ITextureCubeBackend`, `id<MTLTexture>` with `MTLTextureTypeCube` | 🟨 |
| METAL-121 | `SetData(face,level,x,y,w,h,data,dataLength)` via `replaceRegion:...slice:face mipmapLevel:level` | 🟨 |
| METAL-122 | `GetData(face,level,...)` — decide real implementation vs. deferring entirely to Phase 12's blit-based readback | 🟨 (decided and implemented: real, via the shared `blitTextureToClientBuffer()` helper — found `TextureCube.cpp`'s real code always calls `backend_->GetData()` unconditionally, unlike `Texture2D`'s own CPU-shadow-first shortcut, so this was a genuine, previously-shipping gap for *any* cube texture, not just render targets) |
| METAL-123 | `CreateTexture3D(w,h,depth,mipMap,surfaceFormat)` — `MetalTexture3DBackend : ITexture3DBackend`, `id<MTLTexture>` with `MTLTextureType3D` | 🟨 |
| METAL-124 | `SetData(level,x,y,z,w,h,depth,data,dataLength)` via `replaceRegion:` with a full 3D `MTLRegion` | 🟨 |
| METAL-125 | Mip levels for both cube and 3D textures, driven by `METAL-15`'s format table | 🟨 (already satisfied by existing code — `MetalTextureCube`'s `mipmapped:mipMap` and `MetalTexture3D`'s own manual level-count loop both already allocate the full chain; the described `METAL-15` format-table dependency doesn't actually apply, same false premise `METAL-15` itself was found to rest on. Also added real per-level `GetData()` readback for both, `METAL-122`/this task) |
| METAL-126 | Cross-reference: `METAL-65` (EnvironmentMapEffect) is blocked on this phase — do not attempt Phase 6's cube sampling before `METAL-120` lands | ⬜ |
| METAL-127 | `CTest`: `Metal_TextureCube` — 6-face `SetData` round-trip + a real render-into-cube-face draw sampling it back | ⬜ |
| METAL-128 | `CTest`: `Metal_Texture3D` — `SetData` round-trip + a minimal `texture3d<float>` sample-back shader | ⬜ |
| METAL-129 | Add a `Metal` column to `docs/texture3d-texturecube-support.md` | ⬜ |

## Phase 12 — GPU readback (METAL-130 – METAL-135)

| ID | Task | Status |
|---|---|---|
| METAL-130 | `ReadBackbuffer(x,y,w,h,pixels)` — currently throws (base default); implement via `MTLBlitCommandEncoder` copy into a `MTLResourceStorageModeShared` staging buffer, `memcpy` out after `waitUntilCompleted` | 🟨 |
| METAL-131 | Render-target/cube/3D-texture `GetData()` overrides sharing one blit-to-staging-buffer helper instead of 4 near-duplicate implementations | 🟨 (all 4 real, via `blitTextureToClientBuffer()`: `RenderTarget2D`/`RenderTargetCube` here, plain `TextureCube`/`Texture3D` closed later the same session as `METAL-122`/`125` once `Texture2D`'s own CPU-shadow precedent was found NOT to extend to them) |
| METAL-132 | Confirm `x,y` are top-left in *game* (virtual/logical) coordinates per the interface doc — **checked against the reference 2026-07-19, found a pre-existing cross-backend gap, not just a Metal one**: `EasyGLGraphicsBackend::ReadBackbuffer` also uses `x,y,w,h` directly against the physical framebuffer with no logical→physical letterbox scaling applied (only a Y-flip). Metal's new implementation matches that same behavior exactly (raw physical-drawable coordinates, no scaling) — consistent with the established reference, not a new Metal-specific divergence, but real callers passing genuinely logical coordinates on a letterboxed window would get the wrong region on *either* backend today | 🟨 |
| METAL-133 | Document that `waitUntilCompleted` on readback is an intentional correctness-over-throughput stall, matching every other backend's own readback tradeoff — not something a future perf pass should "fix" into a race | 🟨 |
| METAL-134 | `CTest`: `Metal_Readback` — clear to a known color, read back, assert exact match (the `Software_Smoke`/`Headless_Smoke`/`Dx3_Smoke` proof pattern) | ⬜ |
| METAL-135 | Extend `Metal_RenderTarget2D` (`METAL-114`) to actually exercise readback now that it's real | ⬜ |

## Phase 13 — Occlusion queries (METAL-136 – METAL-141)

| ID | Task | Status |
|---|---|---|
| METAL-136 | `CreateOcclusionQuery()` — currently returns `nullptr`; `MetalOcclusionQueryBackend : IOcclusionQueryBackend` using `MTLVisibilityResultBuffer` + `setVisibilityResultMode:offset:` | 🟨 |
| METAL-137 | `Begin()`/`End()` — visibility-result-mode toggling and offset management within a shared visibility-result buffer (allocated up front, one per encoder generation) | 🟨 |
| METAL-138 | `IsComplete()` — tied to command-buffer completion; needs a completion handler or `waitUntilCompleted`-gated flag, not a true async poll | 🟨 |
| METAL-139 | `PixelCount()` — Metal reports a real `uint64_t` sample-passed **count**, a genuine capability advantage over EasyGL's GLES3 any-samples-passed boolean (noted on `IOcclusionQueryBackend`'s own doc comment) — worth calling out, not just matching parity | 🟨 |
| METAL-140 | `CTest`: `Metal_OcclusionQuery` — known occluded vs. visible geometry, count comparison | ⬜ |
| METAL-141 | Add a `Metal` column to `docs/occlusionquery-support.md`, noting the real-pixel-count advantage | ⬜ |

## Phase 14 — Custom ShaderEffect / MSL contract (METAL-142 – METAL-152)

> **Third real dependency found 2026-07-19** (after the Phase 6→3 and Phase 9→14 blockers already
> documented above): a custom `ShaderEffect` is, by definition, free to use an arbitrary vertex
> layout the author chooses — it is not restricted to the fixed handful of concrete strides
> (16/20/24/32/48/52/56) this session's `PipelineKind` enum + `vertexDescriptorForStride()` switch
> statement hardcodes (see Phase 2's own still-open `METAL-26`/`METAL-27`, "the fully generic
> `VertexElement`-driven descriptor builder"). `METAL-144`'s `newLibraryWithSource:` compile step and
> `METAL-146`'s uniform-buffer contract can be built and even unit-tested for compilation success
> independent of this, but `METAL-145`/`METAL-148` (actually binding and drawing with a custom
> pipeline) cannot honestly support an arbitrary custom vertex layout until Phase 2's generic
> descriptor builder exists — attempting it against only the current fixed-stride switch would mean
> either silently rejecting any custom vertex format that doesn't happen to match one of the 7
> built-in strides, or crashing on a `MTLVertexDescriptor` built from the wrong attribute offsets for
> that particular custom shader. Phase 14 is therefore genuinely blocked on Phase 2's
> `METAL-26`/`METAL-27`, not independently landable in full — `METAL-142`–`144`/`146`/`147`
> (2D-texture case)/`150`–`152` can still land standalone; `METAL-145`/`148`/`149`/`147`'s cube/3D
> cases should wait.

| ID | Task | Status |
|---|---|---|
| METAL-142 | Design decision: raw MSL source via `CompileProgram(vertSrc,fragSrc)`, mirroring every other backend's existing GLSL/HLSL-source `IEffectBackend` convention | ⬜ |
| METAL-143 | Evaluate and explicitly accept/reject a cross-compiler alternative (e.g. SPIRV-Cross GLSL/HLSL→MSL transpile, itself Linux-buildable) vs. raw-MSL-only scope — document the decision | ⬜ |
| METAL-144 | `MetalEffectBackend : IEffectBackend` — `CompileProgram()` via `newLibraryWithSource:options:error:`, mirroring the existing `kMetalShaderSource` runtime-compile pattern | ⬜ |
| METAL-145 | `Bind()`/`Unbind()` — set/clear the custom pipeline (built via Phase 2's generic cache) as the active shader, restoring built-in dispatch afterward | ⬜ |
| METAL-146 | `SetUniformFloat/Int/Vec2/Vec3/Vec4/Mat4/FloatArray/Vec2Array` — MSL has no GLSL-style named-uniform reflection; pick a fixed documented buffer-layout contract or `MTLRenderPipelineReflection`-based introspection, and document the choice as this backend's MSL contract | ⬜ |
| METAL-147 | `BindTexture`/`BindTextureCube`/`BindTexture3D` — 2D case can land immediately; cube/3D depend on Phase 11 | ⬜ |
| METAL-148 | `customEffectBackend` (`GpuDrawParams`) — when non-null, bypass built-in shader selection and draw with the custom pipeline directly, mirroring EasyGL's Task 1079 contract exactly | ⬜ |
| METAL-149 | `SpriteBatch.SetCustomEffect(Effect*)` — real override once `MetalEffectBackend` exists (currently base no-op) | ⬜ |
| METAL-150 | `SupportsCapability(GraphicsCapability::CustomEffects)` flips to `true` once this phase lands — remove the `false` case added in `METAL-197` | ⬜ |
| METAL-151 | `CTest`: `Metal_CustomEffect` — the same color-inversion custom-shader methodology D3D9/D3D11/D3D12/Vulkan/Bgfx already use | ⬜ |
| METAL-152 | Document the MSL uniform-contract choice (`METAL-146`) — a new `docs/metal-shader-effect-contract.md`, genuinely Metal-specific with no FNA precedent to copy | ⬜ |

## Phase 15 — Virtual resolution / letterbox / window↔logical transforms (METAL-153 – METAL-161)

| ID | Task | Status |
|---|---|---|
| METAL-153 | `TransformWindowToLogical()` — was unimplemented (base default `false`), a real input bug (`SdlInputBridge` depends on this). **Fixed 2026-07-19**: real implementation ported from `SdlGpuGraphicsBackend::TransformWindowToLogical` | 🟨 |
| METAL-154 | `TransformLogicalToWindow()` — inverse transform used by `Mouse.SetPosition` | 🟨 |
| METAL-155 | Implement/reuse the shared letterbox/overscan/stretch/native/fixed-height-dynamic-width math (`CnaPresentationMode`) every other 3D-capable backend already shares — check for an existing common helper before re-deriving formulas | 🟨→✅ **the formula itself is now real-build-verified** — extracted to `MetalLogicalViewport.hpp` (same `METAL-34` technique) and covered by 8 real `CnaTests`/`ctest` tests, one per `CnaPresentationMode` value plus edge cases, with hand-computed expected numbers for concrete letterbox/overscan/fixed-height scenarios — all pass on this Linux machine. Writing the tests found the function clamps each physical axis (width/height) to zero independently rather than zeroing both together on any single invalid axis — a real, previously-undocumented behavior, not a bug, now locked in by a dedicated test after an initial wrong test expectation caught it |
| METAL-156 | Audit `GetViewportSize()`'s actual contract (currently returns virtual size with no scaling math) — confirm this is correct for its specific contract rather than assuming it's broken | 🟨 |
| METAL-157 | **Real bug, fixed 2026-07-19**: `cna_v2d`'s NDC mapping used raw `drawable.texture.width/height` (physical pixels), completely bypassing virtual resolution/letterboxing. Now derives scale+offset from `computeLogicalViewport()`, hand-verified to degrade to the exact old formula when no virtual resolution is set | 🟨 |
| METAL-158 | Fix `METAL-157` by deriving the 2D projection from the same letterbox viewport rectangle the 3D path and window-transform functions use | 🟨 |
| METAL-159 | `SetPresentationMode(mode)` — was stored with zero effect (same bug class DX3-16 was caught and downgraded for). **Fixed 2026-07-19**: `computeLogicalViewport()` now branches on it. Also found+fixed a related bug: the constructor never read `args.presentationMode` at all (silently defaulted to `Letterbox` instead of XNA's real `FixedHeightDynamicWidth` default — invisible until this task made the field meaningful) | 🟨 |
| METAL-160 | `CTest`: `Metal_Letterbox` — virtual resolution narrower/wider than the physical window, verify sprite/3D positions match predicted letterbox math | ⬜ |
| METAL-161 | `CTest`: `Metal_WindowToLogical` — synthetic window-coordinate inputs round-trip through `TransformWindowToLogical`/`TransformLogicalToWindow` | ⬜ |

## Phase 16 — Resize / fullscreen / drawableSize / Retina (METAL-162 – METAL-167, +METAL-257)

> **Answered from real vendored source, not assumption (2026-07-19)**: this repo vendors SDL3's
> actual Cocoa Metal view implementation
> (`third_party/SDL/src/video/cocoa/SDL_cocoametalview.m`, present in sibling repos e.g.
> `cnanet`/`cnagraphics`). Reading it directly settles every question below without writing any new
> Metal code: `SDL3_cocoametalview.updateDrawableSize` (lines ~113–128) already sets both
> `metalLayer.contentsScale` and `metalLayer.drawableSize` from `[self convertSizeToBacking:size]`,
> called once at view creation AND on every subsequent `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` event
> (via `SDL_AddWindowEventWatch`, gated by `SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE` which
> **defaults to `true`**) — covering ordinary resize, fullscreen toggle, and Space-exit resize
> alike, all through the exact same generic mechanism.

| ID | Task | Status |
|---|---|---|
| METAL-162 | `CAMetalLayer.drawableSize`/`contentsScale` explicit management for HiDPI/Retina — currently unmanaged, behavior is whatever `SDL_Metal_GetLayer` defaults to, unverified | 🟨 (answered: SDL3 already manages both automatically — no CNA Metal code needed, see note above) |
| METAL-163 | Confirm SDL resize-event plumbing reaches Metal and updates `layer.drawableSize`/depth texture correctly (`ensureFrame()` already recreates the depth texture on a size mismatch — confirm sufficiency) | 🟨 (confirmed sufficient: `resolveActiveAttachments()`'s backbuffer branch re-reads `drawable.texture.width/height` — which reflects SDL's own already-updated `drawableSize` — on every new-drawable fetch, and its `depthTexture.width!=w \|\| depthTexture.height!=h` check already recreates the depth texture on any such change) |
| METAL-164 | Fullscreen toggle — confirm `GraphicsBackendCreateArgs::isFullScreen`/`UpdatePresentationFormatEXT` (currently not overridden) interplay with SDL's own fullscreen window management | 🟨 (confirmed: fullscreen toggling resizes the view like any other resize, driven by the same `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` mechanism — no separate handling needed; `UpdatePresentationFormatEXT`'s own doc comment already documents its default no-op as correct for every backend except D3D9, Metal needs no override) |
| METAL-165 | `layer.contentsScale` correctness paired with `drawableSize` per Apple's `CAMetalLayer` guidance | 🟨 (answered: SDL3's `updateDrawableSize` sets `contentsScale = backingSize.height / size.height`, i.e. exactly the ratio Apple's own `CAMetalLayer` guidance calls for — paired atomically with `drawableSize` in the same method) |
| METAL-166 | Document HiDPI/Retina verification as a **physical-Mac-only** item — the macOS CI runner's virtual display cannot prove this | 🟨 |
| METAL-167 | Update this plan's testing-strategy section once implemented, distinguishing "compiles/runs on CI" from "visually correct on a real Retina Mac" | 🟨 (folded into the physical-Mac tier already described in the Testing strategy section) |
| METAL-257 | *(new, found during this audit — NOT Metal-specific, deliberately not fixed here)* SDL3's `highDPI` behavior above is itself gated on the window having been created with `SDL_WINDOW_HIGH_PIXEL_DENSITY` (`Cocoa_Metal_CreateView` reads `window->flags & SDL_WINDOW_HIGH_PIXEL_DENSITY` into its `highDPI` bool). `GraphicsDevice.cpp`'s `getBackendWindowFlags()`/`SDL_CreateWindow()` call (around line 99/1410) never sets this flag for *any* backend — so even a fully-correct Metal layer setup would still render at 1x (non-Retina, blurry) on a real Retina Mac today. This is shared, cross-backend window-creation code, not Metal-only — CLAUDE.md's own WebGPU-precedent guidance ("changes should remain backend-local or common only where a common-interface change is genuinely required and verified across existing backends") applies directly: fixing it means understanding every other SDL-Cocoa-backed backend's own DPI story first (Vulkan-via-MoltenVM also rides `Cocoa_Metal_CreateView`; GL's DPI handling is a separate code path entirely) — deliberately left as a precisely-scoped, well-researched, NOT-attempted finding for a cross-backend task, not folded into this Metal-only plan | ⬜ |

## Phase 17 — Frame pacing / presentation policy (METAL-168 – METAL-172)

| ID | Task | Status |
|---|---|---|
| METAL-168 | `swapInterval` is stored but has zero effect on `Present()` — `CAMetalLayer` has no direct interval knob; map via `layer.displaySyncEnabled` (0→`NO`, 1→`YES`) and document the honest "no true half-rate" caveat for `swapInterval=2` | 🟨 (fixed: real bug, was a stored-but-dead field, now applied at construction and in `SetSwapInterval()`) |
| METAL-169 | `CAMetalLayer.maximumDrawableCount` (double vs. triple buffering) — currently unset (Apple default); decide whether CNA needs explicit control for cross-backend frame-pacing consistency | 🟨 (decided: leave at Apple's platform default — see narrative) |
| METAL-170 | Audit `presentsWithTransaction`/`allowsNextDrawableTimeout` relevance — likely leave at defaults, document that this was considered | 🟨 (audited: both defaults confirmed correct, `allowsNextDrawableTimeout`'s default specifically confirmed to align with, not fight, this file's own minimized/occluded-window handling — see narrative) |
| METAL-171 | Audit whether `presentDrawable:atTime:`/scheduled/completed handlers are needed for precise frame pacing, deferring unless a concrete stutter/tear problem is found on real hardware | 🟨 (deferral confirmed still correct — no real hardware available to find a concrete problem to fix) |
| METAL-172 | Document vsync/frame-pacing verification as a **physical-Mac-only manual item** (timing-sensitive, not CTest-provable) | 🟨 |

## Phase 18 — Resource lifetime / command-buffer synchronization audit (METAL-173 – METAL-181, +METAL-256)

| ID | Task | Status |
|---|---|---|
| METAL-173 | Definitive answer (from Apple documentation, not assumption) on whether `[buffer_ release]`-then-reallocate in `MetalVertexBuffer::SetData()` is safe while the old buffer is still referenced by an in-flight command buffer from a prior frame — a correctness question, not style | 🟨 (answered: safe — see narrative) |
| METAL-174 | Same audit for `MetalIndexBuffer::upload()`'s identical pattern | 🟨 (answered: safe, same reasoning) |
| METAL-175 | Same audit for `MetalTexture`'s in-place `replaceRegion:` calls while the texture may be bound to an in-flight command buffer | 🟨 (answered: **genuinely unsafe as written** — a real, currently-unmitigated hazard; follow-up fix tracked separately as `METAL-256`, not fixed in this pass — see narrative for why) |
| METAL-176 | Consider (profile-driven, optional) buffer sub-allocation from a ring/pool instead of one `MTLBuffer` per `SetData()` call, for high-frequency `SpriteBatch` uploads | 🟨 (scope decision recorded: defer, profile-driven only) |
| METAL-177 | `SetDataWithOptions`/`SetData16WithOptions`/`SetData32WithOptions` — no override exists today (falls to the base default that ignores the hint); decide whether the current always-reallocate behavior already satisfies both `Discard`/`NoOverwrite` hints' *observable* contract, or whether a real perf-motivated distinction is worth adding | 🟨 (decided: already satisfies both hints observably, no override needed — see narrative) |
| METAL-178 | `SetContextRecoveryEnabled(bool)` — Metal has no OpenGL-style context loss on desktop macOS; confirm this should stay a documented intentional no-op | 🟨 (confirmed correct, matches Vulkan/D3D11/D3D12/WebGPU/Bgfx/SdlGpu/DX3's own established precedent — only D3D9/EasyGL override it, both genuinely context-loss-prone APIs) |
| METAL-179 | `DebugSimulateContextLoss()`/`DebugRestoreContext()` — same reasoning as `METAL-178`, document as an intentional no-op with justification | 🟨 (confirmed correct, same precedent) |
| METAL-180 | Audit whether the current one-command-buffer-per-frame model scales once render-target switches (Phase 10) force ending/starting encoders mid-frame — get this right architecturally before Phase 10 lands, not as a retrofit | 🟨 (found and fixed a real premature-present bug — see narrative) |
| METAL-181 | Document the final command-buffer/encoder lifecycle model once `METAL-173`–`METAL-180` resolve — currently only exists as scattered `ensureFrame()`/`endFrame()`/`clear()` logic with no written model, despite being the single most safety-critical part of this backend | 🟨 (written below; to be extracted into `docs/metal-backend.md` when `METAL-234` lands) |
| METAL-256 | *(new, found during this audit)* Fix `MetalTexture::UpdatePixels()`/`UpdatePixelsLevel()`'s in-place `replaceRegion:` CPU/GPU synchronization hazard (`METAL-175`) — genuinely non-trivial: a naive "always reallocate the `id<MTLTexture>`" fix (mirroring the already-safe `MetalVertexBuffer`/`MetalIndexBuffer` pattern) would silently lose any *other*, already-uploaded mip level's content, since a fresh `newTextureWithDescriptor:` texture starts uninitialized and `UpdatePixels()` only ever rewrites level 0 — needs either a per-level blit-copy of untouched levels into the new texture, or an explicit GPU-completion-gated update queue; deliberately not attempted without a compiler to verify against | ⬜ |

## Phase 19 — SpriteBatch full parity (METAL-182 – METAL-188)

| ID | Task | Status |
|---|---|---|
| METAL-182 | `SetTransformMatrix(const Matrix&)` — was unimplemented (base no-op). **Fixed 2026-07-19**: applied CPU-side as a 2D point transform on the already-screen-space quad corners (Software's own convention), not threaded through the shader | 🟨 |
| METAL-183 | Extend `cna_v2d`/`U2D` to carry the transform matrix — **superseded**: `METAL-182`'s CPU-side approach needed no shader uniform change for the matrix itself (`U2D` was still extended, but for the letterbox scale/offset fix, `METAL-157`) | 🟨 |
| METAL-184 | Real `ApplyBlendState`-driven 2D blending — **closed as a side effect of `METAL-23`/`METAL-24`**: `Sprite2D` is now just another `PipelineKind` in the same `(kind, BlendKey)`-keyed cache every 3D pipeline uses, so it automatically gets real per-`BlendState` blending with no Sprite2D-specific code needed | 🟨 |
| METAL-185 | `SetSamplerFilter`/`SetSamplerAddressMode` — **already wired**, `METAL-1`/`METAL-2` | 🟨 |
| METAL-186 | Confirm `FillMode::WireFrame` has no meaning for 2D `SpriteBatch` quads in XNA either (verify-N/A task, not new code) | 🟨 (investigated, **not confirmed N/A as the task assumed** — found a real, unresolved divergence worth recording rather than a false "confirmed" claim: `EasyGLSpriteBatchBackend::FlushBatch()` never touches `wireframe_`/re-expands to `GL_LINES` at all — that emulation only exists inside the `Draw*Primitives*` 3D entry points — so EasyGL's own SpriteBatch draws never respect `RasterizerState.FillMode=WireFrame`, emulation-gap or by design, unclear which. Metal's `MetalSpriteBatch::Draw()` pipeline, by contrast, already applies the tracked `p.fill` via the shared `applyTrackedEncoderState()` uniformly for every encoder regardless of sprite-vs-3D, meaning `FillMode::WireFrame` *does* currently take real visual effect on Metal sprite draws — a genuine cross-backend behavioral difference this session could not resolve against real XNA/D3D9 ground truth (D3D's own rasterizer state is a genuine device-level setting applied to any draw, which argues Metal's approach may be the more faithful one, but this is inference, not verification) — left open rather than asserted either way) |
| METAL-187 | `CTest`: `Metal_SpriteBatch` — identity fast path, rotation, scale, flip, source-rect crop, transform matrix, custom effect (Phase 14), all 4 blend presets (`METAL-184`) | ⬜ |
| METAL-188 | `CTest`: `Metal_Blend` — `Opaque`/`AlphaBlend`/`Additive`/`NonPremultiplied` + a custom `BlendState`, mirroring `Dx3_Blend`'s exact-pixel methodology | ⬜ |

## Phase 20 — `SupportsCapability` accuracy (METAL-189 – METAL-198)

| ID | Task | Status |
|---|---|---|
| METAL-189 | `GraphicsCapability::ThreeD` — confirm the inherited default `true` is correct (verification task, not new code) | 🟨 |
| METAL-190 | `GraphicsCapability::DepthStencilBuffer` — same, confirm default `true` is correct | 🟨 |
| METAL-191 | `GraphicsCapability::MultiSampleAntiAliasing` — should become real/device-queried once Phase 10 lands (`MTLDevice.supportsTextureSampleCount:`), not a blanket `true` | 🟨 (found still a false positive on this same review pass — fixed to `false` until `METAL-104` lands) |
| METAL-192 | `GraphicsCapability::MultipleRenderTargets` — **fixed**: was a false-positive blanket `true` (real MRT doesn't exist, Phase 10), now correctly answers `false` until `METAL-112` lands | 🟨 |
| METAL-193 | `GraphicsCapability::AnisotropicFiltering` — should be `true` now that `METAL-1`'s sampler cache applies `maxAnisotropy`; confirm on real hardware once buildable | 🟨 (source-confirmed; real-hardware confirmation still needs macOS CI) |
| METAL-194 | `GraphicsCapability::WireFrame` — confirm the already-correct `FillMode::WireFrame`→`MTLTriangleFillModeLines` mapping makes the default `true` correct | 🟨 |
| METAL-195 | `GraphicsCapability::OcclusionQuery` — **fixed**: was a false-positive blanket `true` (`CreateOcclusionQuery()` still returns `nullptr`), now correctly answers `false` until Phase 13 lands | 🟨 |
| METAL-196 | `GraphicsCapability::CustomEffects` — **fixed**: was a false-positive blanket `true` (`CreateEffectBackend()` still returns `nullptr`), now correctly answers `false` until Phase 14 lands | 🟨 |
| METAL-197 | `SupportsCapability()` override added to `MetalGraphicsBackend`, covering the 3 known-wrong cases above and deferring to the (correct) base default otherwise | 🟨 |
| METAL-198 | `CTest`: `Metal_Capabilities` — one assertion per `GraphicsCapability`, meant to be extended incrementally as each phase's real behavior lands, not written once and left stale | ⬜ |

## Phase 21 — Argument buffers / bindless NOXNA (METAL-199 – METAL-204)

| ID | Task | Status |
|---|---|---|
| METAL-199 | Evaluate Metal argument buffers (`MTLArgumentEncoder`) as a NOXNA bindless texture/sampler path for many-material scenes — a genuine Metal-specific opportunity distinct from Vulkan's descriptor indexing | ⬜ |
| METAL-200 | Scope decision: recommend deferring until Phases 1–20 are real and hardware-verified; document explicitly rather than silently dropping the idea | ⬜ |
| METAL-201 | If pursued: NOXNA bindless material-index draw path (`GpuDrawParams` extension or a separate NOXNA entry point) | ⬜ |
| METAL-202 | Argument-buffer-backed texture array for `SpriteBatch` draws sharing one atlas — potential 2D-heavy perf win | ⬜ |
| METAL-203 | A/B benchmark vs. the existing per-draw bind path, to justify the added complexity before committing | ⬜ |
| METAL-204 | Document as `NOXNA` per `CNAHelper.hpp`'s macro convention | ⬜ |

## Phase 22 — Indirect command buffers / GPU-driven NOXNA (METAL-205 – METAL-208)

| ID | Task | Status |
|---|---|---|
| METAL-205 | Evaluate `MTLIndirectCommandBuffer` for GPU-driven instanced/batched draws, following on from Phase 9's CPU-issued instancing | ⬜ |
| METAL-206 | Scope decision — likely deferred well past v1 parity, same framing as `METAL-200` | ⬜ |
| METAL-207 | NOXNA API surface design — a new entry point, not a `GpuDrawParams` tweak (architecturally distinct from ordinary instancing) | ⬜ |
| METAL-208 | Document as `NOXNA`, explicitly gated behind Phase 9 being real and tested first | ⬜ |

## Phase 23 — MetalFX NOXNA upscaling (METAL-209 – METAL-214)

| ID | Task | Status |
|---|---|---|
| METAL-209 | Evaluate `MTLFXSpatialScaler`/`MTLFXTemporalScaler` as an optional NOXNA upscaling path — Apple-only, no FNA/XNA precedent | ⬜ |
| METAL-210 | Availability gating — macOS 13+/specific GPU families, must be a runtime-queried optional path, never a hard requirement | ⬜ |
| METAL-211 | Spatial upscaling integration point as a NOXNA alternative presentation path alongside Phase 15/16's letterbox/stretch modes | ⬜ |
| METAL-212 | Temporal upscaling — needs motion vectors this pipeline doesn't produce anywhere; scope as "not feasible until a motion-vector G-buffer pass exists," not a shallow API-wiring task | ⬜ |
| METAL-213 | NOXNA API surface design + docs | ⬜ |
| METAL-214 | Defer entirely until Phases 1–20 are hardware-verified, same priority framing as `METAL-200`/`METAL-206` | ⬜ |

## Phase 24 — GPU counters / signposts / Xcode frame capture (METAL-215 – METAL-219)

| ID | Task | Status |
|---|---|---|
| METAL-215 | Extend `SetStringMarkerEXT`'s existing `insertDebugSignpost:` to `pushDebugGroup:`/`popDebugGroup:` around logical draw batches for structured Xcode GPU Frame Capture grouping | ⬜ |
| METAL-216 | `MTLCounterSampleBuffer` — Apple GPU hardware performance counters for optional NOXNA profiling | ⬜ |
| METAL-217 | Document the Xcode GPU Frame Capture workflow for this backend (`MTL_CAPTURE_ENABLED`, `MTLCaptureManager`) — a real, physical-Mac-only workflow with no Linux/CI equivalent | ⬜ |
| METAL-218 | Document `MTL_SHADER_VALIDATION`/`MTL_DEBUG_LAYER` as the macOS-side equivalent of Vulkan's validation layers; enable in the macOS CI job (Phase 26) | ⬜ |
| METAL-219 | Explicitly note this phase has no meaningful CTest equivalent — interactive/visual tooling, not automatable | ⬜ |

## Phase 25 — Testing infrastructure (METAL-220 – METAL-226)

| ID | Task | Status |
|---|---|---|
| METAL-220 | Extend `cmake/Tests/MetalTests.cmake` from its current single `Metal_Smoke` registration to every `Metal_*` CTest named throughout this plan, one `cna_register_backend_test()` call per executable, as each phase's implementation actually lands | ⬜ |
| METAL-221 | Shared Metal test fixture/helper (extends `PixelTestGame.hpp`, already used by `metal_smoke_test.cpp`) with common probe-pixel/readback assertions once Phase 12 lands | ⬜ |
| METAL-222 | Audit `SDL_VIDEODRIVER`/`DISPLAY` requirements for every new Metal test — get this right from the start rather than repeating `plan_dx3.md`'s own hard-won issue #1 mistake (hardcoded `x11`/real display when a dummy driver would have worked) | ⬜ |
| METAL-223 | Confirm the real macOS CI runner's display capabilities (GitHub-hosted macOS runners provide a real virtual display, unlike this project's Linux Xvfb sandbox) rather than assuming Linux-style constraints apply | ⬜ |
| METAL-224 | Confirm whether the `WEBGPU-123`-style cross-backend pixel-parity harness (still open even for WebGPU) can extend to Metal once broad enough — folds into Phase 28 | ⬜ |
| METAL-225 | Add Metal to whatever full-`CnaTests`-suite regression-count tracking this project already performs per-backend once it has enough tests to matter | ⬜ |
| METAL-226 | Explicit "N/A, verified" note: a `ThrowNo3D`-style audit (DX3's own Phase X7) does not apply to Metal — it is a 3D-only backend, unlike DX3/SDL_Renderer/Canvas | ⬜ |

## Phase 26 — CI / tooling (METAL-227 – METAL-233)

| ID | Task | Status |
|---|---|---|
| METAL-227 | Keep `.github/workflows/metal-macos-ci.yml`'s `paths:` trigger list current as new files land (new `.mm`/`.hpp` splits, new example/test `.cpp`, new `docs/*.md`) — a living checklist, revisited at the end of each phase | ⬜ |
| METAL-228 | Consider a second macOS CI job variant (different macOS version / Apple Silicon vs. Intel runner) once GPU-family differences (e.g. BC compression, `METAL-17`) start mattering | ⬜ |
| METAL-229 | Add `MTL_SHADER_VALIDATION=1`/`MTL_DEBUG_LAYER=1` to the CI job (ties to `METAL-218`) | ⬜ |
| METAL-230 | Audit CI build-time budget as the backend grows toward EasyGL's scale — revisit `--parallel 3` once compile times actually grow | ⬜ |
| METAL-231 | Consider splitting the monolithic `.mm` into multiple translation units once file size approaches EasyGL's ~5,300-line mark (a concrete threshold, not a premature rule) | ⬜ |
| METAL-232 | Confirm `GraphicsBackendCompileDefinitionsTest` already knows about `CNA_BACKEND_METAL` (DX3's own external review found the equivalent gap for `CNA_BACKEND_DX3`/`D3D11`/`D3D12` until fixed) | ⬜ |
| METAL-233 | Keep `README.md`'s backend list/build instructions honest about Metal's real current capability boundary as phases land | ⬜ |

## Phase 27 — Documentation (METAL-234 – METAL-238)

| ID | Task | Status |
|---|---|---|
| METAL-234 | Create `docs/metal-backend.md` (does not exist today, unlike `docs/webgpu-backend.md`/`docs/dx3-backend.md`/`docs/d3d11-backend.md`) — the durable capability-boundary reference CLAUDE.md's WebGPU precedent points to | ⬜ |
| METAL-235 | Add a `Metal` column to `docs/graphics-backend-feature-matrix.md` only once the feature set is broad enough for a meaningful row-by-row comparison — the doc's own header excludes Headless/Software for the identical reason; do not add prematurely with a column full of ❌ | ⬜ |
| METAL-236 | Add Metal rows/columns to each relevant per-effect `docs/*-support.md` as its own phase lands (13 files identified: basiceffect/alphatesteffect/dualtextureeffect/environmentmapeffect/skinnedeffect/occlusionquery/rendertarget/texture3d-texturecube/sampler-state/depthstencilstate/rasterizerstate/surface-format/vertex-format-support.md) | ⬜ |
| METAL-237 | Update `docs/coverage.md`/`docs/xna-4-api-coverage.md` if either tracks per-backend Graphics coverage at a level Metal should appear in | ⬜ |
| METAL-238 | Hold this plan document itself to the same status-legend/correction-note discipline as `plan_dx3.md`/`plan_webgpu.md` — any future ✅ claim must cite the actual CTest name and check letters that proved it | ⬜ |

## Phase 28 — Cross-backend pixel parity (METAL-239 – METAL-242)

| ID | Task | Status |
|---|---|---|
| METAL-239 | Once Phases 1–20 are real and hardware-verified, add Metal to whatever cross-backend "same scene, compare pixels" harness exists (`WEBGPU-123` is the closest precedent, itself still open) | ⬜ |
| METAL-240 | Design as artifact-exchange (each backend's CI job uploads rendered output, a separate step diffs) since Metal can only build/run on macOS CI, not alongside Linux-built backends in one job | ⬜ |
| METAL-241 | Scope to a small, fixed scene set first, matching `tools/xna-oracle/`'s own checked-in-corpus precedent | ⬜ |
| METAL-242 | Explicitly out of scope until Phases 1–20 are substantially complete — this is deliberately the final phase | ⬜ |

## Phase 29 — iOS / tvOS platform scope (METAL-243 – METAL-251)

`cmake/BackendSelection.cmake`'s own gate message already claims iOS/tvOS as valid Metal targets
("METAL backend only builds when targeting macOS/iOS/tvOS"), but nothing in the previous revision of
this plan, the CI job, or the implementation has ever addressed that claim.

| ID | Task | Status |
|---|---|---|
| METAL-243 | Audit whether the claim is aspirational or already true — the current `.mm` uses only generic `SDL_Metal_CreateView`/`CAMetalLayer` calls (no macOS-only API spotted), so it may already be iOS/tvOS-buildable, but `metal-macos-ci.yml` only ever targets `macos-14` | 🟨 (audited from real vendored source — likely TRUE, not just aspirational, see narrative) |
| METAL-244 | Add an iOS-targeted CMake/CI configuration as a **build-only** smoke check (no simulator/device execution required) to catch iOS-incompatible API usage early | ⬜ |
| METAL-245 | tvOS-targeted build, same build-only scope | ⬜ |
| METAL-246 | Confirm touch input is already fully owned by CNA's existing input/device layer (`plan_input.md`/`plan_cna_devices.md`) and needs no Metal-specific work beyond Phase 15's generic window↔logical transform | 🟨 (confirmed: grepped every graphics backend, including Metal, for `SDL_EVENT_FINGER`/`TouchID`/`SDL_Finger` — zero matches anywhere; touch is architecturally independent of the graphics backend layer entirely, confirming the hypothesis) |
| METAL-247 | Audit iOS/tvOS `CAMetalLayer` differences from macOS (`UIView`-hosted, not `NSView`-hosted; different `backingScaleFactor`/@2x/@3x conventions) once a real iOS build exists | ⬜ |
| METAL-248 | Note `MTLGPUFamilyApple*` vs. `MTLGPUFamilyMac*` feature-set differences (tile-based deferred rendering specifics, programmable blending, imageblocks) as an optional-optimization concern for later NOXNA work, not a correctness blocker | ⬜ |
| METAL-249 | Confirm current App Store/TestFlight review guidelines still allow runtime shader compilation via `newLibraryWithSource:` (the mechanism `kMetalShaderSource` already relies on) on iOS, or plan a precompiled-`.metallib` fallback — a real distribution-risk item, not a code bug | ⬜ |
| METAL-250 | Document iOS Simulator Metal feature-gap caveats vs. real devices once iOS builds exist, same "physical device for final truth" framing already applied to macOS | ⬜ |
| METAL-251 | Document the real iOS/tvOS support boundary (build-only vs. run-verified vs. never-attempted) once `METAL-244`/`METAL-245` land — do not let the CMake gate's claim stay unverified indefinitely | ⬜ |

## Phase 30 — Additional NOXNA opportunities found during this audit (METAL-252 – METAL-255)

| ID | Task | Status |
|---|---|---|
| METAL-252 | Depth-buffer-as-shader-resource: the current backbuffer/depth texture is `MTLStorageModePrivate` with only `MTLTextureUsageRenderTarget`; add `MTLTextureUsageShaderRead` and a bind path for NOXNA post-process effects (SSAO-style, depth-based fog) that need to read scene depth — no FNA precedent, purely additive | ⬜ |
| METAL-253 | `MTLComputePipelineState` — a NOXNA compute-shader entry point (particle simulation, a precursor to Phase 22's GPU-driven culling), explicitly optional/deferred | ⬜ |
| METAL-254 | Audit whether `GraphicsAdapter`/`GraphicsDeviceManager` already has a NOXNA extension point for backend-specific device info (`MTLDevice.name`/`recommendedMaxWorkingSetSize`/`MTLGPUFamily`/`hasUnifiedMemory`), or whether a new accessor is needed | ⬜ |
| METAL-255 | Multi-GPU (Mac Pro/eGPU) `MTLCopyAllDevices()` enumeration — currently always `MTLCreateSystemDefaultDevice()`; confirm whether XNA's `GraphicsAdapter.Adapters` model already has a cross-backend enumeration contract to plug into, or defer until a concrete multi-GPU need exists | ⬜ |

---

## Testing strategy

Real Metal execution requires Apple hardware or an Apple GPU exposed to macOS. A normal macOS guest
under QEMU on a Linux PC does not provide a usable virtual Metal GPU, so it is not a meaningful
replacement for real hardware testing. This plan uses a 3-tier verification model, applied
consistently across every phase above:

1. **This Linux machine (Debian 13)** — source-level static work only: enum-mapping tables against
   the real CNA `.hpp` ordinal definitions, MSL shader text authored against Apple's public,
   stable Metal Shading Language reference, `IGraphicsBackend`-contract cross-referencing against
   already-tested EasyGL/Vulkan/WebGPU implementations, CMake/CI/doc changes, and any logic that can
   be extracted into a plain-C++, Apple-toolchain-free unit. Code produced this way is marked 🟨,
   never ✅ — it has not been compiled here, let alone run — **with exactly one earned exception**:
   `METAL-34` was actually extracted, built, and tested via the real `CnaTests` binary and `ctest`
   on this Linux machine on 2026-07-19 (8/8 tests, CTest #83–90) — the one task in this whole plan
   that genuinely reached ✅ from tier 1 alone, because unlike every other Metal task its logic has
   zero Objective-C/Apple-framework dependency to begin with.
2. **GitHub-hosted macOS runners** (`metal-macos-ci.yml`, currently `macos-14`) — real Apple Clang
   compilation, real `MTLCreateSystemDefaultDevice()` execution, and CTest pixel/behavior assertions
   this plan's `Metal_*` tests exercise. This is where a 🟨 task becomes a candidate for ✅, gated on
   the CTest actually passing.
3. **A physical Mac** — required for anything this plan explicitly flags as **physical-Mac-only**:
   HiDPI/Retina correctness (`METAL-166`), vsync/frame-pacing feel (`METAL-172`), Xcode GPU Frame
   Capture/counter workflows (`METAL-217`/`METAL-219`), and any visual-fidelity judgment call a CTest
   pixel-tolerance check cannot fully substitute for.

No task in this plan may be marked ✅ from tier 1 alone. A task is only ✅ once it has a named,
passing `Metal_*` CTest (tier 2) or an explicit physical-Mac verification note (tier 3) — matching
this project's established, hard-won discipline (`plan_dx3.md`'s multiple correction notices for
exactly this failure mode).
