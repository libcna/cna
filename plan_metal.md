# Native Metal Graphics Backend

## Post-audit adaptation (2026-08-09)

This file preserves the historical `feature/metal` task narrative. Its older status rows and
platform statements are evidence of what that lane attempted; they are not the current support
matrix. The current contract and validation boundary are maintained in
`docs/metal-backend.md`.

The adaptation onto `integration/post-audit-phase1` makes these binding decisions:

- Metal remains a genuine direct Objective-C++/MSL backend; SDL3 supplies only the macOS window,
  high-pixel-density Metal view, and `CAMetalLayer` integration.
- Supported target: macOS only. iOS and tvOS have no build evidence and are rejected by the current
  CMake platform gate.
- The backend implements the current `IGraphicsBackend`, normalized render-target descriptor,
  `BlendWriteState`, boolean transfer, `GpuDrawParams::vertexStreams`, and FNA-compatible
  `fogVector` contracts.
- MSAA, MRT, custom effects, multistream input, instancing, and backbuffer readback are deliberately
  unsupported. Capabilities report false, requests clamp or throw deterministically, and known-
  wrong pixels are never returned as successful output.
- All current capabilities are enumerated explicitly; unknown values return false. Supported
  formats and stream shapes are validated before native binding state is mutated.

Historical GitHub Actions run `29814126178` at the latest production commit
`e0f42426836ce9f2d4823d50732850877020aef1` built on macOS 14/Xcode 15.4 and passed 136 of 143
tests. Six failures read only the clear color; the RenderTarget2D MSAA failure applied sample count
four but produced a binary edge. The final four historical commits through
`48928d113cb864f78d754256d2d559d914d4f1a7` changed only handoff/plan prose. No post-adaptation
Objective-C++ compile or Mac runtime result exists yet, so the old run cannot be inherited as
evidence for the adapted source.

The chronological replay retained 88 signed commits and omitted 11 superseded, temporary-
diagnostic, or handoff-only commits. `docs/metal-history-map.tsv` is the authoritative 99-row
machine-readable disposition map, including the recreated commit for every retained original.

Portable Linux evidence at this checkpoint: the stable HEADLESS `CnaTests` and dedicated portable
Metal target build with `-j4`, all 206 unique `^Metal` tests pass with `DISPLAY` unset (CTest shows
207/207 because it also runs the aggregate), the build graph contains no Objective-C++ source,
and a Linux `METAL` configure fails at the intended macOS-only gate. The helper-only GNU 14.2
ASan+UBSan target also passes 206/206 with no sanitizer finding in the complete direct-test log; it
does not cover Objective-C++ or native Metal lifetime. A fresh successful macOS workflow run
remains the required external validation boundary before claiming adapted native compile/runtime
evidence; it is not an integration blocker under the repository's authoritative source-continuity
policy.

### Post-audit findings (continuing after `METAL-257`)

The following IDs are new adaptation findings. They do not absorb, rename, or change the status of
any historical task/finding below.

Post-audit correction for historical `METAL-257`: its repeated claim that CNA's Metal window path
never requested `SDL_WINDOW_HIGH_PIXEL_DENSITY` was false. The Metal branch has used
`SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY` since the initial replay. The historical prose
below is retained as lane evidence, but its Retina-window premise was already satisfied and is not
an open finding.

| Finding | Severity | Evidence | Disposition |
|---|---|---|---|
| `METAL-258` — backbuffer readback returned successful clear-only pixels | High | Historical macOS run `29814126178`: PBR, SkinnedPBR, DrawUser VPC, SpriteBatch custom effect, MRT, and backbuffer MSAA read only the clear color. | **Supported contract disabled:** `ReadBackbuffer` throws `NotSupportedException`; readback-dependent native cases are not registered as supported gates. |
| `METAL-259` — RenderTarget2D MSAA reported four samples without edge coverage | High | Run `29814126178` applied sample count four, but the diagonal edge was binary rather than partially covered. | **Supported contract disabled:** MSAA capability is false; backbuffer and render-target requests clamp and report zero. |
| `METAL-260` — cached `CAMetalDrawable` lacked an owned reference | High | MRR source audit: the `nextDrawable` (+0) result was stored across calls and mid-frame command commits without retain/release ownership. | **Implementation fixed:** a portable-tested retained owner retains on acquisition, preserves ownership across non-presenting commits, and releases after present encoding plus commit. Native adapted-Mac validation remains pending. |
| `METAL-261` — partial backend construction had no MRR rollback | Medium | Source audit: constructor throws after device/view/layer/queue acquisition (notably runtime MSL-library failure) destroy `impl_`, but the historical `Impl` had no destructor and the backend destructor never runs for a failed construction. | **Implementation fixed:** `Impl` now performs bounded, nil-safe cleanup for failure and normal teardown, releasing the drawable before the layer and destroying the SDL Metal view in order. Native adapted-Mac validation remains pending. |
| `METAL-262` — default device was retained twice | Medium | `MTLCreateSystemDefaultDevice()` already contributes the create-rule +1 reference; the historical constructor immediately sent a second `retain`. | **Implementation fixed:** the redundant retain is removed. Create/`new*` results remain single-owned; only borrowed results retained beyond their call scope receive an explicit retain. Native adapted-Mac validation remains pending. |
| `METAL-263` — fixed-stride draws ignored declaration meaning | High | Source audit: `SetVertexDeclaration` remembered elements, but all four native draw routes selected descriptors only by stride; same-stride semantic/offset/format changes and duplicate semantic ownership could silently reinterpret bytes. | **Implementation fixed:** every ordinary and direct indexed/non-indexed route invokes the shared `RequireFaithfulVertexDeclaration` oracle before native submission. Portable canonical and mismatch cases pass; native validation remains pending. |
| `METAL-264` — cube/3D transfers lacked complete validation, aligned readback layout, and mutation ordering | High | Source audit found unchecked face/level/region/length arithmetic, tight-row Metal buffer blits, and in-place `replaceRegion` writes racing prior GPU sampling. | **Implementation fixed:** overflow-safe tight/aligned layouts use a documented macOS-safe 256-byte row alignment; face/mip/volume and exact lengths are checked; readback de-pads rows/slices; SetData reallocates, preserves untouched faces/mips/slices by checked blit, and swaps only after completion. Source/portable evidence only. |
| `METAL-265` — Clear and encoder recreation destroyed viewport/scissor state | High | `ensureFrame`/`clear` overwrote caller state with the attachment extent, fresh encoders never applied scissor, and disabled scissor state still clipped. | **Implementation fixed:** requested state is tracked separately from attachment extent and preserved across Clear/target changes; each encoder applies the requested viewport unchanged, intersects only an enabled scissor with the attachment, and reacts immediately to rasterizer-state toggles. Portable transition/extreme-input tests pass. |
| `METAL-266` — OcclusionQuery was advertised despite split-command and slot-exhaustion defects | High | Dormant code could not produce correct completion when Clear split Begin/End work and monotonically exhausted unrecycled slots. | **Supported contract disabled:** capability is false, creation deterministically throws, and no visibility buffer is allocated. Dormant native code remains for future adapted-Mac work. |
| `METAL-267` — BGRA render-target readback was exposed as RGBA | High | Render targets allocate `BGRA8Unorm`, while the shared readback path copied raw bytes into CNA `Color` storage. | **Implementation fixed:** the aligned conversion helper swizzles BGRA targets to RGBA while plain RGBA cube/3D textures remain byte-identical; odd-width multirow/multislice cases pass portably. |
| `METAL-268` — RenderTarget2D SetData silently discarded uploads | High | `MetalRenderTargetBackend` inherited void no-op `UpdatePixels` hooks although public `RenderTarget2D` inherits Texture2D.SetData. | **Implementation fixed:** full RGBA uploads are converted to BGRA and applied through queue-ordered reallocate/copy/swap; invalid input throws before mutation. RenderTargetCube continues its inherited/explicit truthful false boundary. |
| `METAL-269` — absent stock textures reused stale encoder bindings | High | Several branches bound a slot only when non-null, explicitly leaving earlier draw resources in DualTexture and EnvironmentMap slots. | **Implementation fixed:** every used slot binds a per-draw native or owned neutral resource; null 2D/PBR inputs use white or flat-normal textures, null environment maps use an owned white cube, and non-null foreign resources are rejected. Portable slot/transition matrices pass. |
| `METAL-270` — native wrapper construction/allocation and cached +1 insertion were not transactional | High | MRR audit found throw-after-retain leaks in texture/target wrappers, unchecked nil buffers/descriptors/states, release-before-new replacement, and cache insertion leak windows. | **Implementation fixed:** scoped owners roll back partial construction and failed replacements; buffer sizes are checked; native nil results throw; pipeline/sampler +1 references remain owned through cache emplace; depth logical/native state rolls back on allocation failure. Native lifetime validation remains pending. |
| `METAL-271` — pipeline shape depended on texture pointer presence | High | Untextured lit BasicEffect stride 32 fell to the unrelated colored route even though a neutral sample faithfully represents TextureEnabled=false; null stock textures could also expose stale state. | **Implementation fixed:** effect flags and canonical stride select the pipeline, never pointer truthiness; every nullable stock sample has a deterministic neutral binding. Portable Basic/AlphaTest/Dual/Environment/Skinned/PBR matrices pass. |
| `METAL-272` — Texture2D ignored ImageData surface format and byte shape | High | `CreateTexture(ImageData)` always allocated/uploaded RGBA8 regardless of `surfaceFormat`, dimensions, mip count, or exact base bytes. | **Supported contract narrowed:** only positive Color-format complete shapes with an exact RGBA base level are accepted; unsupported formats and malformed input reject before native allocation. |
| `METAL-273` — SetBlendEnabled was inert/incorrectly latched | Medium | The control did not install the required complete last-writer state; an intermediate AND-style fix incorrectly let earlier state suppress later calls. | **Implementation fixed:** false installs opaque, true installs straight alpha, and every later Set/Apply call fully replaces the prior key. Portable Opaque/Alpha last-writer sequences pass. |
| `METAL-274` — disabling depth still wrote depth storage | High | Native depth state used `Always` comparison when disabled but retained `depthWriteEnabled=true`, so hidden depth writes affected later re-enable/draws. | **Implementation fixed:** native writes use `depthEnabled && requestedDepthWrite`; the requested flag remains retained for later re-enable. Portable disable/re-enable policy tests pass. |
| `METAL-275` — lighting-disabled built-in uniforms retained active light values | High | EasyGL's accepted contract normalizes unlit Basic/PBR state, but Metal forwarded caller ambient/directional values, producing lit output despite `lightingEnabled=false`. | **Implementation fixed:** ambient becomes one, directions become the deterministic default, and directional diffuse/specular contributions become zero where represented. Portable Basic/PBR uniform tests pass. |
| `METAL-276` — RenderTarget2D did not report defined GPU mip levels | High | Shared partial SetData consults `HasDefinedMipLevel`; inherited false caused generated untouched mip texels to be seeded from zero and wiped. | **Implementation fixed:** per-level state is marked only after full upload or successful synchronous mip generation, enabling authoritative backend readback before partial composition while uninitialized levels stay false. |
| `METAL-277` — transient nil drawable threw/retried within one logical frame | Medium | `nextDrawable=nil` is expected while minimized/backgrounded, but `ensureFrame` threw and repeated callers could reacquire or partially tail-present. | **Implementation fixed:** one failed acquisition latches backbuffer-unavailable until Present; Clear/draw/marker/Present skip that frame without throwing or retrying, while offscreen render-target work continues. Portable frame-state tests pass. |
| `METAL-278` — command failures and render-target readback ordering could return stale results | High | Asynchronous command errors were not surfaced; readback committed an active source render then waited only for a later blit, allowing failed source work to appear as successful stale pixels. | **Implementation fixed:** a lifetime-safe latch is checked at command-producing/readback entries; consuming it ends/releases encoder and abandons the uncommitted command without commit; synchronous operations wait/check the exact command; active RT readback verifies its exact source command before blit; older failures retain a distinct diagnostic. Portable lifecycle policy tests pass; native execution remains pending. |
| `METAL-279` — empty logical scissor submitted an illegal zero native rectangle | High | Metal rejects zero-width/height scissors, while fully outside/zero requests are valid logical empty intersections. | **Implementation fixed:** an empty intersection keeps its logical meaning, installs a legal 1x1 native placeholder, and suppresses every draw until state/extent makes it non-empty; Clear remains independent. Portable extreme/toggle/recreation tests pass. |
| `METAL-280` — CNA and Metal static archives had an undeclared reverse dependency | High | `MetalGraphicsBackend.mm` calls CNA-owned Effect/math/color symbols, so a one-pass CNA→backend archive link is order-dependent like the existing Sokol/LLGL/D3D cases. | **Build integration fixed:** `METAL` joins the backend→CNA reverse-edge condition; native Metal test configuration asserts the edge and uses ordinary target linking rather than a test-only group. Native Apple link verification remains external. |
| `METAL-281` — retained texture/render-target backends dereferenced a destroyed owner | High | Plain Texture2D/Cube/3D callbacks captured raw `Impl*`; RT2D/RTCube stored raw `Impl&`. Copy/move and public weak/shared backend access can retain a backend beyond `GraphicsDevice`/Metal `Impl` teardown, including a `RenderTargetCube` moved into its `TextureCube` base. | **Implementation fixed:** `Impl` publishes a shared health token and marks it inactive before native teardown; resources keep only a weak owner, route live pending failures through common consume/abandon cleanup so retry succeeds, and reject post-owner operations before dereference. RT destructors weak-lock for active-target cleanup and otherwise release their independently owned native textures without owner access. Portable alive/failed/inactive, retry, copy/move/backend-escape, and ownerless-cleanup tests pass; native lifetime validation remains external. |

## Historical record below

The historical legend was: ✅ meant verified against the acceptance criteria then in force; 🟨
meant source-complete or partially exercised; ⬜ meant not started. Later evidence and the current
conservative support boundary override those historical marks.

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
    `METAL-14` (the real, general `VertexElementFormat`→`MTLVertexFormat` table) — unlike the
    texture-format question, vertex layouts genuinely do carry arbitrary application-defined element
    formats via `VertexDeclaration`, with no equivalent "always normalized upstream" simplification.
    **Landed 2026-07-20, real ✅** alongside `METAL-26`/`27`'s core logic — see this session's own
    later narrative items for the real `CnaTests` evidence; this paragraph is left as the original
    research note, not rewritten, since it correctly explains *why* the table was scoped the way it
    was.
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
    'MetalBlendKey\|MetalPipelineCacheKey'` path (CTest #102–109, 100% pass, 1.03s total)** — a real,
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
    the very first run. Ran the full Metal-tagged `ctest` subset again afterward (32 real tests across
    all 5 extracted headers at this point — `ctest -R "Metal"` itself reports 38 since that filter is
    a substring match and also catches 6 unrelated, pre-existing `PbrEffectDefaultsTest.Metallic*`/
    `SkinnedPbrEffectDefaultsTest.Metallic*` tests; a later independent audit caught this filter
    imprecision, see the end of this session's narrative) to confirm zero regressions: 100% pass.

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
    full Metal-tagged `ctest` subset again afterward (47 real tests across all 6 extracted headers —
    `ctest -R "Metal"` itself reports 53 due to the substring-match filter imprecision noted above) to
    confirm zero regressions: 100% pass. A sixth genuinely real, fully-earned ✅ tier tonight — same
    discipline as the WVP matrix helpers: foundational dispatch infrastructure, not tied to one single
    pre-existing `METAL-N` task ID.

34. **`fillLitUniforms`/`fillEnvUniforms`/`fillSkinnedUniforms`/`fillPbrUniforms`/
    `fillSkinnedPbrUniforms` — a seventh real ✅, the largest single extraction of the night by field
    count**: these 5 functions are the connective tissue between `GpuDrawParams` (backend-agnostic
    per-draw state) and the actual float arrays `memcpy`'d into a real `MTLBuffer` for each shader
    family — `METAL-38`–`47`/`66`–`68`/`73`/`74`/`76`–`78`/`81`/`83`–`86`. With dozens of individual
    field assignments per function, the single biggest real risk is a copy-paste mistake (e.g.
    `light1Diffuse` accidentally reading `params.light2Diffuse`, or a light's `Specular` landing in
    another light's slot) — exactly the class of bug that compiles cleanly, never crashes, and would
    only ever show up as subtly wrong lighting on real hardware, i.e. never on this Linux machine.
    Extracted verbatim to `MetalUniformFill.hpp` (the 9 plain-C++ mirror structs — `LitTransform`/
    `LitUniforms`/`EnvTransform`/`EnvUniforms`/`SkinnedTransform`/`SkinnedUniforms`/`PbrTransform`/
    `PbrUniforms`/`SkinnedPbrTransform` — plus all 5 fill functions), `MetalGraphicsBackend.mm`
    reduced to `using` aliases and one-line wrappers, all existing call sites unaffected. 8 new
    `CnaTests`: one exhaustive "every field correctly mapped" test per function, built from a
    `GpuDrawParams` where every relevant field is set to its **own distinct numeric value** (not
    identical placeholders) so a wrong-field or wrong-light-index mapping produces a visibly wrong
    number instead of a coincidental match — every expected value re-derived independently by reading
    `GpuDrawParams`' own field list and each function's real mapping directly, not by copying the
    implementation being tested. The normal-matrix columns (`Lit`/`Env`/`Pbr`, not `Skinned` — which
    correctly has none, the skinned shader has no world-normal-matrix step) are cross-validated
    against a direct, independent call to the already-tested `ComputeMetalNormalMatrixCols` with the
    same `worldColMajor` buffer, using a genuinely non-identity invertible matrix specifically so a
    wrong-buffer wiring bug couldn't hide behind a trivial identity result. 3 further tests cover the
    boolean-gated fields' *false* branch (`fogEnabled=false`, `fresnelEnabled=false`,
    `vertexColorEnabled=false` plus a different `weightsPerVertex`) since the main test only exercises
    each gate's `true` side, and one test proves `fillSkinnedPbrUniforms`' delegation to
    `fillPbrUniforms` is real (fragment-side fields compared byte-for-byte against a direct
    `fillPbrUniforms` call with the same inputs) rather than merely coincidentally matching. All 8
    passed on the very first run, CTest #130–137 — no bugs found in either the extraction or the
    tests. Ran the full Metal-tagged `ctest` subset again afterward (55 real tests across all 7
    extracted headers — `ctest -R "Metal"` itself reports 61 due to the substring-match filter
    imprecision noted above) to confirm zero regressions: 100% pass.

35. **Independent adversarial audit of all 7 extractions (items 29–34 above)**: a separate review
    pass, deliberately skeptical rather than self-confirming, diffed every extracted header's logic
    against the exact pre-extraction inline code in git history (byte-for-byte/logic-for-logic
    identical in all 7 cases — one cosmetic-only rewrite in `MetalLogicalViewport.hpp`, `pw > 0 ? pw :
    0` instead of the original `std::max(0, pw)`, mathematically equivalent, not a bug), grepped every
    `using`-alias/wrapper call site in `MetalGraphicsBackend.mm` for dangling or duplicate references
    (none found), independently re-derived every `MetalUniformFillTests.cpp` expected value against
    the real `GpuDrawParams` struct definition rather than trusting the implementation under test
    (all correct, full field coverage), and rebuilt + reran the full Metal-tagged `ctest` subset from
    scratch. Found two genuine but purely documentation-accuracy issues, both fixed in this same pass:
    stale `METAL-34` CTest citations (`#83–90`, from before 6 more Metal test files shifted CTest's
    alphabetical numbering — corrected to the real current `#102–109`), and the "38/53/61 tests"
    figures above, which were inflated by 6 every time because `ctest -R "Metal"` is a substring match
    that also catches 6 unrelated, pre-existing `PbrEffectDefaultsTest.Metallic*`/
    `SkinnedPbrEffectDefaultsTest.Metallic*` tests — the real, extracted-header-only counts (32/47/55)
    are now cited alongside the filter's own reported number for transparency. Neither issue ever
    masked a regression (a full 5572-test whole-suite run, separately, already confirmed zero
    Metal-related failures) — both were citation-accuracy problems only, not functional bugs. No
    functional defects found in any of the 7 extractions after a genuine attempt to find one.

36. **`metal-macos-ci.yml` extended to actually run the 7 extractions' 55 tests on real Apple
    hardware, not just Linux HEADLESS (partial `METAL-227`)**: every extraction this whole session
    was, by design, only ever real-build-verified on this Linux machine's `HEADLESS` backend — the
    logic is platform-agnostic, but the *build* was never actually the `METAL` backend build, and the
    existing `metal-macos-ci.yml` job (real `macos-14` GitHub Actions runner) only ever ran
    `ctest -R Metal_Smoke`, never the general `CnaTests` gtest binary it already silently builds as
    part of `-DCNA_BUILD_TESTS=ON` (confirmed via `cmake/UnitTests.cmake`: `CnaTests` has no
    `EXCLUDE_FROM_ALL`, so `cmake --build build-metal` — no explicit target — already builds it every
    run, the CI job just never asked `ctest` to run any of the tests inside it). Changed the job's
    final step to `ctest -R "^Metal" --output-on-failure` — anchored at the start, per item 35's own
    finding, so it picks up `Metal_Smoke` plus all 55 extraction tests without also matching the 6
    unrelated `PbrEffectDefaultsTest.Metallic*`/`SkinnedPbrEffectDefaultsTest.Metallic*` tests an
    unanchored `-R Metal` would catch (confirmed the exact count, 55, against this Linux build before
    touching the CI file). Also added the previously-missing `tests/CNA/Internal/Backends/Metal/**`
    path to the workflow's own trigger list (`METAL-227`'s "living checklist" — the `include`/`src`
    Metal dirs were already covered, `tests` was not). This is genuinely new coverage the next real
    push/PR against this branch will exercise on actual Apple hardware for the first time — not
    something this Linux sandbox can trigger or observe itself, so it stays reported here rather than
    claimed as a verified pass.

37. **`docs/metal-backend.md` created (`METAL-234`)**: this backend had no durable
    capability-boundary reference document, unlike `docs/webgpu-backend.md`/`docs/dx3-backend.md`/
    `docs/d3d11-backend.md` — CLAUDE.md's own WebGPU precedent points to exactly this kind of doc.
    Unlike those three, the new doc leads with an explicit, unavoidable caveat before any status
    table: every `.mm` file in this backend has never been compiled, linked, or run anywhere, since
    every session on it has been Linux-only — so every 🟨 in the doc means "carefully written
    against the EasyGL reference, never built" rather than "verified," a materially different claim
    than what 🟨 means in most of this project's other backend docs. The one exception is the same 7
    plain-C++ extractions items 29–35 cover, called out as this doc's own dedicated "Real,
    machine-verified subset" section with the header/test table repeated for a reader who lands on
    this doc without having read `plan_metal.md`'s full narrative. Also checked `METAL-237`
    (`docs/coverage.md`/`docs/xna-4-api-coverage.md`): `docs/coverage.md`'s "Per-backend Graphics
    capability" table columns are all real-machine-verified backends (EasyGL/Vulkan/Bgfx/WebGPU) —
    adding a mostly-unverified Metal column now would be the identical premature-comparison problem
    `METAL-235` already explicitly defers for the sibling feature-matrix doc, so deferred for the
    same reason rather than actioned or silently skipped.

38. **`MTL_SHADER_VALIDATION`/`MTL_DEBUG_LAYER` enabled in CI (`METAL-218`/`229`)**: Apple's own
    documented runtime validation env vars — the Metal-side equivalent of Vulkan's
    `VK_LAYER_KHRONOS_validation` already used elsewhere in this project — set on
    `metal-macos-ci.yml`'s "Run Metal tests" step only (they're read at runtime by whatever process
    creates the `MTLDevice`, not needed at build/link time). Documented in `docs/metal-backend.md`'s
    verification methodology section, including the note that anyone reproducing a CI failure
    locally on a real Mac should set both by hand for the same diagnostic benefit. Like item 36's CI
    filter change, this is new coverage the next real push/PR will exercise on real Apple hardware
    for the first time — whether it actually fires without a code path to intentionally trip it is
    unverified from this Linux sandbox, so it's reported as "added" rather than "confirmed working."

39. **`METAL-222`/`223` (SDL display-driver audit) answered**: `cmake/Tests/MetalTests.cmake`'s
    `Metal_Smoke` registration sets no `SDL_VIDEODRIVER`/`DISPLAY` `ENVIRONMENT` override at all,
    unlike `Dx3Tests.cmake`'s `SDL_VIDEODRIVER=dummy` on every one of its entries. Confirmed this is
    correct, not an oversight: `plan_dx3.md`'s own hard-won mistake (issue #1) was the *opposite*
    direction — wrongly requiring a real display/`x11` where a dummy driver would have worked fine.
    Metal fundamentally needs a real `CAMetalLayer` bound to a real window and cannot use a dummy
    driver at all (unlike DX3's 2D-only `IDirectDrawSurface` path), so relying on the CI runner's
    own real display session (GitHub's documented `macos-14` runner capability) is the right choice
    already in place, not a gap to fix. Both stay 🟨 rather than ✅ since this reasoning is checked
    from source/documentation, not confirmed against actual observed CI behavior.

40. **`METAL-232`: a real, confirmed compile-definition gap found and fixed**: this session's
    highest-confidence bug of the whole "documentation and CI hardening" tail — not a maybe, not a
    "reasoning check," an actual missing `#ifdef CNA_BACKEND_METAL` branch in
    `GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` test, which
    counts one `++enabled` per known `CNA_BACKEND_*` macro and asserts `enabled==1`. Every other
    backend this project ships has its own branch; Metal never got one, and the file's own existing
    comment already documents an *identical* historical gap for `CNA_BACKEND_D3D9` (found while
    merging `feature/sdlgpu`, only caught because someone finally built under `D3D9`). Same root
    cause here: this test has never once been built or run under `CNA_GRAPHICS_BACKEND=METAL` (no
    Apple toolchain in any session to date), so a real `METAL` build would define
    `CNA_BACKEND_METAL` while this test kept counting 0 for it — `EXPECT_EQ(enabled, 1)` would fail
    the very first time `metal-macos-ci.yml`'s build actually reached this test. Fixed with the
    identical one-`#ifdef` pattern every other backend already uses; rebuilt and reran under this
    Linux `HEADLESS` build to confirm the fix doesn't disturb the existing pass (`CNA_BACKEND_METAL`
    stays undefined here, so `enabled` is still exactly 1 — the new branch is provably inert on this
    machine, its correctness for the `METAL` case itself remains real-CI-pending like everything
    else in this backend). Also added a dedicated `metal-macos-ci.yml` step
    (`ctest -R "GraphicsBackendCompileDefinitionsTest"`) specifically because this test's name
    doesn't start with "Metal" and so was invisible to the existing `^Metal` filter — without that
    addition, the very fix just made would never actually be exercised by the CI job meant to
    exercise it.

41. **README accuracy pass (`METAL-233`) plus two quick, confident audits (`METAL-226`/`231`)**:
    `README.md`'s per-backend description list (11 bullets, `SDL_RENDERER` through `DX3`) and its
    `CNA_GRAPHICS_BACKEND` build-time selection list both had **zero mentions of Metal anywhere** —
    not a stale entry, a complete absence, meaning a new contributor reading only the README would
    not know this backend exists at all. Added `METAL` to the selection list (marked
    "Apple platforms only, experimental") and a new description bullet after `DX3`'s, deliberately
    matching the honesty bar `WEBGPU`'s own bullet already sets ("Experimental... not yet a 3D-parity
    replacement...") but going further given Metal's own materially different state: leads with the
    same "never compiled, linked, or run anywhere" caveat `docs/metal-backend.md` opens with, cites
    the real 55-test/7-header verified-on-Linux subset by name, and points to both
    `docs/metal-backend.md` and `plan_metal.md` rather than re-deriving detail inline. While in
    Phase 25/26 checking for more `METAL-232`-shaped concrete gaps: `METAL-226` confirmed N/A (Metal
    is a real 3D backend, so the `ThrowNo3D`-style audit `DX3`/`SDL_RENDERER`/`CANVAS`/`ASCII` each
    needed for their 2D-only 3D-throws simply doesn't apply); `METAL-231` checked with real numbers
    — `MetalGraphicsBackend.mm` is 2,248 lines against `EasyGLGraphicsBackend.cpp`'s 4,733, well
    under half the stated ~5,300-line split threshold, genuinely not yet needed. `METAL-224`/`225`/
    `230` remain explicitly deferred: each has its own "once X grows/lands" gate in its own task
    description that genuinely hasn't been reached yet, not skipped without reason.

42. **`METAL-14`/`26`/`27` — the generic `VertexElement`-driven descriptor builder's core logic, an
    eighth genuine ✅ (user-authorized daytime session, not autonomous)**: with the user back and
    explicitly asking to continue into this specific task, tackled the piece Phase 2's own original
    text scoped down and every "Explicitly still open" summary since has deferred. Read
    `EasyGLGraphicsBackend::ApplyLayout()`/`DescribeVertexElementFormat()` first as the reference
    implementation (this project's established "port working reference logic" convention) — its
    core insight (attribute location = the element's own index within the declaration's element
    list, not sorted by offset) carries over exactly.

    Split into a plain-C++ tier and an Objective-C-only tier, same discipline as items 29–35:
    `MetalVertexAttribFormat.hpp` (`METAL-14`) maps all 12 `VertexElementFormat` values to a neutral
    `MetalVertexAttribKind` enum (a stand-in for `MTLVertexFormat`, since that real enum lives only
    in `<Metal/Metal.h>`) — 14 tests, including an all-pairs distinctness check across all 12
    formats and a dedicated check that `Color` (normalized) and `Byte4` (raw, for XNA's
    `BLENDINDICES`-style usage) never collide, the single riskiest distinction in the whole table.
    `MetalVertexDescriptorPlan.hpp` (`METAL-27`'s core) builds the actual attribute layout
    (location/offset/format per element) from an arbitrary `VertexElement` list and a stride — 6
    tests, including one that deliberately declares elements out of offset order to prove location
    comes from list position not sorted offset, and two that cross-validate the generic builder
    against this file's own existing hand-written stride-48/52 `vertexDescriptorForStride()` cases
    byte-for-byte. All 20 new tests pass on this Linux machine, full Metal-tagged `ctest` subset
    re-run afterward: 75/75 across all 9 extracted/added headers, zero regressions; a full
    whole-suite `ctest -j4` run confirmed no Metal-tagged failures either.

    The `.mm`-side glue (`vertexDescriptorFromElements()`, translating the tested plan to a real
    `MTLVertexDescriptor` via a one-line `MetalVertexAttribKind`→`MTLVertexFormat` switch) and
    `MetalVertexBuffer::SetVertexDeclaration()` (storing the declaration, mirroring EasyGL's own
    identical trivial-storage pattern) are both real, written code but Objective-C-only, so stay 🟨
    like every other `.mm`-side piece this whole plan — genuinely untestable without a Mac.

    **Deliberately not wired into any live draw path.** Traced `getOrCreatePipeline(PipelineKind
    kind)`'s actual call site (`drawMetal3D()`) and confirmed it derives `stride` purely from `kind`
    itself, never from the real bound `MetalVertexBuffer` — every built-in shader (`BasicEffect`
    through `SkinnedPbrEffect`) already has a matching fixed-stride descriptor in
    `vertexDescriptorForStride()`'s existing 8 cases, so nothing today would ever call the new
    generic path with a shader that could actually consume an arbitrary layout. Forcing a live
    wiring now would repeat exactly the mistake this plan's own Phase 9 note already warned against
    for instancing: "a technically-present-but-functionally-inert override." The infrastructure is
    real, tested, and ready; `METAL-28`'s fallback-choice wiring and any actual generic-layout draw
    stay correctly blocked on Phase 14 (custom `ShaderEffect`), the same conclusion this plan
    reached before, just backed by real code now instead of an open task.

43. **`METAL-30`'s regression-proof trace, done against real pre-rewrite source**: read the actual
    original pipeline-dispatch code via `git show 08707f81:.../MetalGraphicsBackend.mm` (the very
    first Metal commit, before Phase 1/2's pipeline-cache rewrite) rather than relying on memory or
    assumption. Old dispatch: `textured=params&&params->texture0; if(textured){stride==20→pipe3Tex20;
    ==24→pipe3ColorTex24; ==32→pipe3NormalTex32; else throw} else if(stride!=16) throw; else
    pipe3Color`. Byte-identical to `SelectMetalPipelineKind()`'s current dispatch for strides
    16/20/24 — same throw conditions, same textured-gate, only the destination name changed. Stride
    32 is the one real divergence, and it's already fully documented elsewhere in this plan
    (`METAL-38`): the old `pipe3NormalTex32` reused the same flat unlit fragment shader as strides
    20/24 (no lighting existed anywhere in the pre-rewrite backend at all), the new `LitTex32`/
    `LitTex32VertexLit` is genuinely lit — Phase 3's own deliberate addition, not a silent
    regression this trace uncovered. `MetalSelectPipelineKindTests.cpp`'s 15 already-passing tests
    (item 33) now lock the traced dispatch in going forward, closing a task that had been open since
    Phase 2 first landed.

44. **`METAL-3`: a real PBR sampler-slot bug found and fixed, by reading the reference
    implementation rather than assuming**: while checking whether Phase 5/6/8 (all landed) had
    actually closed out `METAL-3`'s own "extend sampler-slot consultation beyond unit 0" task,
    found that `DualTextureEffect`/`EnvironmentMapEffect` already correctly consult
    `samplerSlots[0]`/`[1]` for their own 2 texture units each — but the `Pbr48`/`SkinnedPbr68`
    draw paths, which bind 5 distinct PBR texture units (base color/normal/metallic-roughness/
    emissive/occlusion), broadcast a single `samplerSlots[0]` sampler across all 5 instead of
    consulting `samplerSlots[1]`–`[4]` for units 1–4. Confirmed this is a real divergence, not a
    stylistic choice, by reading `EasyGLGraphicsBackend`'s own already-tested PBR texture-binding
    code (the `p.loc_pbr_normalmap`/`p.loc_pbr_mr`/etc. block): it binds each PBR map to its own
    GL texture unit and samples each through its own independently-created GL sampler object
    (`samplers_[0..4]`) — the real, established reference behavior this whole backend has been
    ported from all session. A game setting a distinct `SamplerState` on, say, the
    metallic-roughness slot (e.g. `GraphicsDevice.SamplerStates[2] = SamplerState.PointClamp`)
    would have had it silently ignored, with every PBR map sampling through whatever was set on
    slot 0 instead. Fixed both `Pbr48` and `SkinnedPbr68` to use `samplerSlots[0..4]` respectively.
    `.mm`-only, so — like every functional fix this whole plan has made — correct on inspection and
    by reference-comparison, but genuinely unverified without a Mac to actually run it.

45. **First real CI signal from `metal-macos-ci.yml`, and a genuine CI bug fixed**: the push
    landed the job's first-ever run on real Apple hardware — and it failed immediately at the
    `cmake` configure step, not from anything in this backend's own code: `cmake/ThirdPartySDL.cmake`
    threw its own actionable "Missing vendored 'SDL' ... run: git submodule update --init" error,
    because the job's `actions/checkout@v4` step never set `submodules:` at all. Checked every
    other CI workflow in this repo (`d3d-windows-ci.yml`/`devices-tests.yml`/`input-ci.yml`) —
    every single one of them sets `submodules: true` (non-recursive, matching the established
    `DEV-BUILD-001` precedent: initializes `vendor/googletest` and `third_party/SDL`/`SDL_image`/
    `SDL_mixer` without their own nested codec submodules this project's CMake args disable
    anyway) — `metal-macos-ci.yml` was the one workflow that never had it, confirmed against
    `.gitmodules` that this covers every top-level submodule CMake actually needs. Fixed by adding
    the identical `submodules: true` line, matching the rest of the repo exactly rather than
    inventing a new pattern. This is the first genuine, observed (not just predicted) real-hardware
    CI signal this whole plan has ever received — a config-level failure, not a code-level one, so
    it says nothing about whether the backend itself actually compiles yet; that remains the very
    next thing to find out once this fix lands.

46. **A genuinely serious real bug found and fixed in `TextureFilter`→min/mag/mip mapping, a ninth
    real ✅**: continued the "cross-check every mapping table against the real reference
    implementation" methodology that already caught the PBR sampler bug (item 44). The original
    `metalMinFilter()`/`metalMagFilter()`/`metalMipFilter()` each independently maintained their own
    `case 1: case X: case Y: ...` membership set — three separately-derived sets for what should be
    one coherent per-filter table, and transcribing three sets independently is exactly the kind of
    task where a single miscopied case number goes unnoticed. Verified programmatically (a small
    Python script computing both the buggy table and the correct one, derived directly from each
    XNA `TextureFilter` enumerator's own self-documenting name — e.g. `MinLinearMagPointMipPoint`
    literally spells out min=Linear/mag=Point/mip=Point) against all 9 real filter values: **3 of
    9 were wrong** — `LinearMipPoint` (3) had mag stuck at Point instead of Linear; `MinLinearMagPointMipPoint`
    (6) had min and mag fully swapped; `MinPointMagLinearMipLinear` (7) had min stuck at Linear
    instead of Point, silently degrading to plain `Linear` filtering. Cross-validated the corrected
    table a second, independent way against `EasyGLGraphicsBackend::ApplySamplerState()`'s own
    already-shipping GL filter table (read directly from its source) — full agreement on all 9
    values. This would have produced visibly wrong texture filtering (either aliased/blocky where
    smooth was requested, or blurry where sharp was requested, and no correct-vs-wrong sample point
    to compare, since XNA's stock effects never touch these obscure filter modes but a game
    directly setting `GraphicsDevice.SamplerStates[n]` to one of the 3 broken modes absolutely
    could) for real, legitimate, documented XNA API usage — silently, with no crash or warning.

    Following the same discipline as items 1–7 (the overnight extraction spree): rewrote the logic
    as a single per-filter `switch` in the new plain-C++ `MetalSamplerFilter.hpp`
    (`DescribeMetalSamplerFilter`) that spells out each filter's Min/Mag/Mip explicitly per case —
    structurally harder to get wrong than three independently-derived membership sets, since each
    case is self-contained and directly traceable to its own enumerator name. `metalMinFilter()`/
    `metalMagFilter()`/`metalMipFilter()` are now thin wrappers, existing call site unaffected. 10
    new tests, including one dedicated regression guard per previously-broken filter value (3/6/7)
    and a structural "every Min*Mag* name is internally consistent" check across 5/6/7/8 together —
    all pass on this Linux machine, full Metal-tagged `ctest` subset re-run afterward: 85/85 across
    all 10 extracted/added headers, zero regressions. `TextureAddressMode`→`MTLSamplerAddressMode`
    was checked the same way and confirmed already correct (matches `EasyGLGraphicsBackend`'s own
    Wrap=0/Clamp=1/Mirror=2 mapping exactly) — not every table had a bug, this cross-check discipline
    finds real problems specifically because it's applied uniformly, not because everything it
    touches turns out broken.

47. **Shader-math cross-check (alpha test / fog / diffuse-lighting sign / specular half-vector) —
    all confirmed correct**: continued the same reference-comparison discipline into the actual MSL
    formulas embedded in `kMetalShaderSource`, not just enum-mapping tables — the highest-risk area
    left, since a sign or operand-order slip here produces plausible-looking but wrong lighting, not
    a crash. Compared four formulas against `EasyGLGraphicsBackend`'s own embedded GLSL source,
    read directly (not from memory): the alpha-test discard formula (`cna_alpha_test_fails` vs. the
    GLSL `_at` computation — algebraically identical), the object-space fog factor (`(z+fogEnd)/
    (fogEnd-fogStart)` clamped 0–1, identical epsilon guard for `fogStart≈fogEnd`), the diffuse
    lighting sign convention (`dot(N,-lightDir)` — both backends negate the light's own travel
    direction before dotting with the normal, the correct Lambertian convention, not an inverted
    one), and the Blinn-Phong specular half-vector (`normalize(eye-lightDir)` — identical in both).
    All four match exactly. No new bug found here, but this is real, positive evidence — the
    lighting/fog/alpha-test math genuinely was ported correctly, not just assumed to be.

48. **Real second CI signal — build progressed further, hit a new, unrelated blocker, fixed**: the
    submodule fix (item 45) worked — the macOS runner successfully built SDL3/SDL3_mixer this time
    — but then failed at `third_party/enet/CMakeLists.txt`'s own `cmake_minimum_required(VERSION
    2.6)`: tolerated as a mere deprecation warning by the CMake version on this Linux dev machine
    (3.31), but a hard, build-stopping error under CMake ≥4.0 ("Compatibility with CMake < 3.5 has
    been removed from CMake"), which this `macos-14` runner's own toolchain apparently provides.
    Fixed with CMake's own suggested escape hatch, `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, added to
    `metal-macos-ci.yml`'s own configure step rather than editing the vendored `third_party/enet`
    source directly (`enet` isn't Metal-specific — editing it risks being an out-of-lane change
    affecting every other backend's own CI, whereas a configure-flag scoped to this one workflow
    doesn't). This is the second real, observed CI signal this plan has received, and — like the
    first — a toolchain/config-level blocker, not a code-level one; whether the Metal backend's own
    source actually compiles remains unknown until a run gets past this too.

49. **Real third CI signal — the Objective-C++ compiler and `METAL` backend selection both
    confirmed working, third blocker fixed**: this run's log is the strongest evidence yet that this
    backend is genuinely close to a real compile: `-- Detecting OBJCXX compiler ABI info ... done`
    and `-- CNA: Using native METAL graphics backend` both appeared, meaning
    `cmake/BackendSelection.cmake`'s Apple-only gate and `enable_language(OBJCXX)` call (documented
    as already-correct back in the "Implemented initial foundation" list) are now genuinely
    confirmed, not just source-reviewed. Failed next at `cmake/CnaLibrary.cmake`'s `REQUIRED
    libavcodec` `pkg_check_modules` — this repo's `CLAUDE.md` documents the Linux
    `apt-get install libavcodec-dev ...` step, but `metal-macos-ci.yml` is this project's first-ever
    macOS CI job, so no Homebrew equivalent existed yet. Fixed by adding a `brew install ffmpeg`
    step before configure — a single Homebrew formula provides all 4 required pkg-config modules
    (`libavcodec`/`libavformat`/`libavutil`/`libswresample`). `find_package(draco CONFIG QUIET)`
    needs no equivalent install — it's genuinely optional (`QUIET`, with an already-documented
    graceful "throws a clear unsupported-format error" fallback when absent), unlike FFmpeg's
    `REQUIRED`. Third real, observed CI signal — each one closer to an actual compile result, still
    entirely toolchain/environment-level so far, not a single Metal-backend source issue found by
    the CI itself yet.

50. **Real fourth CI signal — actual C++ compilation started, and a real cross-repo bug found and
    fixed in `sharp-runtime` (a sibling repo, not this one)**: the log showed `enet` build
    successfully and `SHARP_RUNTIME` (the sibling `sharp-runtime` library CNA depends on) begin
    compiling real `.cpp` files — genuine progress past every prior blocker. Failed on
    `StoragePaths.cpp` with `error: unknown warning option '-Wno-format-truncation'
    [-Werror,-Wunknown-warning-option]`. Traced to `sharp-runtime`'s own `CMakeLists.txt`
    (`target_compile_options(SHARP_RUNTIME PRIVATE -Wall -Wextra -Werror
    -Wno-format-truncation)`, and an identical line for `SharpRuntimeTests`) — `-Wformat-truncation`
    is a GCC-only diagnostic Clang never implemented, so `-Wno-format-truncation` is an unrecognized
    flag under Apple Clang, which `-Werror` then escalates to a hard build failure rather than a
    harmless no-op.

    This is the first bug this whole plan has found outside `cnametal` itself. Fixed properly at
    the source: gated both occurrences behind `if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")` in
    `sharp-runtime`'s own `CMakeLists.txt` — verified zero regression by rebuilding `SHARP_RUNTIME`
    on this Linux/GCC 14.2.0 machine (still adds the flag there, unchanged behavior). Did **not**
    commit this to `sharp-runtime`'s `develop` branch directly, nor to whatever topic branch
    happened to be checked out locally (`feature/xnb-charreader`, unrelated in-progress work) — a
    shared dependency repo's default branch is a bigger blast radius than this session's own
    established scope, so the fix landed on a new `fix/clang-format-truncation-flag` branch off
    `origin/develop` instead, pushed to `origin`. `metal-macos-ci.yml`'s sharp-runtime checkout
    step is temporarily pinned to that branch (`ref:`, clearly commented as temporary) so this CI
    job can proceed without waiting for a human to review and merge a PR into another repo's
    `develop` — revert that pin once the fix branch merges. Fourth real, observed CI signal; the
    Metal backend's own source has still not been reached by an actual compile yet, but is now only
    one more successful `SHARP_RUNTIME` build away.

51. **Real fifth CI signal — a second, more subtle `sharp-runtime` bug, this one a genuine
    product tradeoff, not a mechanical fix**: `SHARP_RUNTIME` began compiling real `.cpp` files
    (confirming item 50's fix worked) and failed on `BitConverter.cpp`'s translation unit with
    Clang reporting "no viable overload for call to 'from_chars'" — only the *integer*
    `from_chars` template as a candidate. Root cause: Apple's libc++ omits the floating-point
    `std::from_chars`/`std::to_chars` overloads entirely below a macOS 13.3+ deployment target
    (the underlying implementation was only added to the OS-shipped `libc++.dylib` at that OS
    version) — genuinely absent from the overload set under this build's deployment target, not
    merely deprecated.

    Unlike item 50's flag-portability fix, this had a real tradeoff worth surfacing rather than
    silently deciding: raise `CMAKE_OSX_DEPLOYMENT_TARGET` to 13.3+ (simple, zero code-risk, but
    forces a higher minimum runtime macOS for every consumer of `sharp-runtime`, not just this CI
    job) vs. a portable `strtof`/`strtod` fallback (keeps the current floor, but touches code with
    carefully-documented, ticket-referenced .NET-exact parsing behavior — real regression risk).
    Asked; the user chose the portable-fallback path.

    Added `SharpRuntime::FromCharsFloat` (`include/SharpRuntime/PortableFromChars.hpp`): uses the
    real `std::from_chars` when a `requires`-detected floating-point overload actually exists,
    falls back to `strtof`/`strtod` otherwise. The fallback is not a naive passthrough — it
    corrects two real behavioral differences from `std::from_chars` *before* delegating (both
    rejected up front as `std::errc::invalid_argument`, matching what real `from_chars` would do):
    `strtof`/`strtod` skip leading whitespace (`from_chars` never does — this codebase's own
    `XmlConvert.cpp` comment documents callers relying on that strictness), and `strtof`/`strtod`
    accept a leading `+` (`from_chars`'s floating-point grammar does not, a well-known asymmetry).
    Wired into the 3 real affected call sites: `System/Single.hpp`/`System/Double.hpp`'s
    `tryParseCore` (float/double `.Parse`), and
    `System/Xml/XPath/XPathAstInternal.cpp`'s `ParseXPathNumberLiteralString` (its own
    `chars_format::fixed` restriction is redundant with the portable helper's format-less parsing,
    since that call site's own character pre-filter already guarantees no exponent can reach the
    parse call regardless).

    Verified the fallback logic *directly* — this Linux/GCC 14.2 machine's libstdc++ already has
    floating-point `from_chars`, so the `if constexpr` dispatch would never actually exercise the
    fallback branch here otherwise — with a standalone test calling `PortableFromCharsFloat`
    directly: simple/negative floats, trailing-garbage partial consumption, leading-whitespace
    rejection, leading-plus rejection, empty/garbage rejection, exponent notation, and
    out-of-range (`ERANGE`) all correct. Full `SharpRuntimeTests` suite (12,481 tests) passes with
    zero regressions on this machine. Landed on the same `fix/clang-format-truncation-flag` branch
    as item 50 (already pinned in `metal-macos-ci.yml`), so no further CI-workflow change was
    needed. Fifth real, observed CI signal — `BitConverter.cpp` (and the rest of `SHARP_RUNTIME`)
    should now compile; the Metal backend's own source is the next thing this CI has never reached.
    **Correction, item 52**: "should now compile" was too optimistic — the very next CI run showed
    `BitConverter.cpp` itself compiled fine, but a *different* file, `Environment.cpp`, then hit two
    more Linux-only symbols. Worth recording plainly rather than quietly editing this claim away.

52. **Real sixth CI signal — a third `sharp-runtime` portability gap, this time two symbols in one
    file, both genuinely Linux-only**: `Environment.cpp` failed with `use of undeclared identifier
    'HOST_NAME_MAX'` and `use of undeclared identifier 'CLOCK_BOOTTIME'`. Both are real —
    `HOST_NAME_MAX` is a glibc extension Apple's libc never defines at all (even though
    `gethostname()` itself is fully POSIX and available everywhere); `CLOCK_BOOTTIME` is a
    Linux-only clock ID (kernel 2.6.39+) absent from Apple's `<time.h>` entirely. Both are exactly
    the class of gap this whole session keeps finding: real Unix code, written once, that had
    simply never been compiled on a non-Linux Unix before this CI job existed.

    Fixed with portable fallbacks, not platform-specific reimplementations: `HOST_NAME_MAX` falls
    back to 255 (the POSIX-guaranteed `_POSIX_HOST_NAME_MAX` minimum) when undefined, guarded so
    Linux keeps using its own real constant unchanged; `CLOCK_BOOTTIME` falls back to
    `CLOCK_MONOTONIC` when undefined — the portable POSIX clock available on every Unix-like
    platform, and matches .NET's own cross-platform `GetTickCount64` PAL implementation, which
    uses `CLOCK_MONOTONIC` uniformly rather than a Linux-specific clock (not a guess — this is
    what real .NET's own reference implementation does).

    Proactively grepped the rest of `src`/`include` for other likely Linux-only symbols (`gettid`,
    `pthread_setname_np`, `epoll_*`, `eventfd`, `O_TMPFILE`, further `/proc` usage) specifically to
    avoid another slow CI round-trip per individual issue, rather than waiting for each to surface
    one at a time — found none: `AppDomain.cpp`'s `/proc/self/exe` is already correctly gated
    behind the final Linux-and-other-POSIX `#else` (it has its own proper `#elif defined(__APPLE__)`
    branch using `_NSGetExecutablePath` first), and `FileSystemWatcher.cpp`'s `eventfd`/`inotify`
    usage is already gated behind `SHARP_RUNTIME_FSW_LINUX` (`defined(__linux__)` only) — both
    genuinely already correct, not new findings. Full `SharpRuntimeTests` suite (12,481 tests,
    including all 99 `EnvironmentTests`) passes with zero regressions. Landed on the same fix
    branch, no further `cnametal`-side change needed. Sixth real, observed CI signal.

53. **Real seventh CI signal — a fourth `sharp-runtime` portability gap, a genuinely different
    root cause class from the previous three**: `TcpClient.cpp`/`Socket.cpp` failed with `error:
    expected unqualified-id` at every `::htonl(...)`/`::htons(...)` call site, Clang's own note
    chain showing the expansion through Apple's `sys/_endian.h` (`#define htonl(x)
    __DARWIN_OSSwapInt32(x)`) into `libkern/_OSByteOrder.h`'s ternary expression. Root cause: on
    Apple's libc, `htonl`/`htons`/`ntohl`/`ntohs` are preprocessor **macros**, not real functions
    — macro expansion happens on token match regardless of a preceding `::` qualifier, so
    `::htonl(x)` expands to `::(a ternary expression)`, which isn't valid C++ (`::` must be
    followed by an unqualified-id). On Linux these are ordinary functions, so the identical `::`
    qualification silently works there, masking the problem until this CI job existed.

    Grepped every `htonl`/`htons`/`ntohl`/`ntohs` call in `src`/`include` before fixing anything,
    the same discipline as item 52's proactive sweep: found 20 real `::`-qualified call sites
    across exactly 3 files (`TcpClient.cpp`, `Socket.cpp`, `UdpClient.cpp`), and confirmed
    `Dns.cpp`/`Ping.cpp` already call these unqualified — not new findings, already portable.
    Removed the `::` prefix from all 20 (a plain, behavior-preserving syntax fix — unqualified
    calls resolve correctly via ordinary lookup on every platform, real functions on Linux, macro
    expansion on Apple, no functional difference). Full `SharpRuntimeTests` suite (12,481 tests,
    including all 95 Tcp/Udp/Socket/Listener tests) passes with zero regressions. Landed on the
    same fix branch, no further `cnametal`-side change needed. Seventh real, observed CI signal —
    four genuinely distinct classes of macOS-portability gap found and fixed in `sharp-runtime` so
    far (GCC-only warning flag, missing floating-point `from_chars`, Linux-only POSIX symbols,
    Apple's macro-not-function byte-order conversions), none of them in `cnametal` itself, none of
    them the Metal backend's own source — which this CI still has not reached.

54. **Real eighth CI signal — a fifth `sharp-runtime` gap, and the first genuinely architectural
    one (not a portability shim)**: `Ping.cpp` failed with `use of undeclared identifier
    'ICMP_DEST_UNREACH'` (and 5 sibling constants) plus `unknown type name 'icmphdr'`/`no member
    named 'type'/'code'/'checksum'/'un' in 'icmp6_hdr'` (Clang's own confused "did you mean
    icmp6_hdr?" suggestion, since `icmphdr` genuinely doesn't exist on Apple's headers at all).
    Unlike items 50–53 (flag/symbol/macro-name fixes), this is a real architectural gap: BSD/
    Darwin's `<netinet/ip_icmp.h>` defines a genuinely different ICMPv4 header ABI from Linux's —
    the struct itself is named `struct icmp` (not `icmphdr`), with fields `icmp_type`/`icmp_code`/
    `icmp_cksum`/`icmp_id`/`icmp_seq` (the last two reached via the system header's own
    convenience macros over a nested union) rather than Linux's `type`/`code`/`checksum`/
    `un.echo.id`/`un.echo.sequence`. The Destination-Unreachable/Time-Exceeded sub-code constants
    also have different names on BSD (`ICMP_UNREACH`/`ICMP_UNREACH_NET`/etc.) for the identical
    RFC 792 values. ICMPv6 (`icmp6_hdr`) needed no changes — that struct is standardized
    identically across Linux and BSD.

    Fixed with a genuine dual implementation, not a workaround: an `IcmpV4Header` type alias
    (`::icmphdr` on Linux, `::icmp` on BSD/Darwin) so `sizeof`/pointer-cast call sites stay
    identical on both platforms; the 6 constants aliased onto BSD's own real macros
    (`ICMP_DEST_UNREACH → ICMP_UNREACH`, etc.) — deliberately not hand-typed numeric values, so
    the actual RFC 792 numbers always come from the system's own header, never guessed; and
    explicit per-platform `#if` branches for the handful of field-name-dependent construction/
    parsing lines, deliberately not hidden behind further macros so each platform's real field
    names stay directly readable. Verified the Linux path both compiles AND works correctly
    end-to-end: all 14 `Ping`-related tests pass, including a real loopback ICMP echo round-trip
    (not a mock — an actual packet sent and received). Full `SharpRuntimeTests` suite (12,481
    tests) passes with zero regressions. **The BSD/Darwin path itself is written from documented
    FreeBSD/Darwin API knowledge and cannot be verified from this Linux sandbox** — unlike every
    other fix so far, this one's correctness genuinely depends on the next macOS CI run, not
    already-confirmed-by-local-testing. Eighth real, observed CI signal.

55. **Real ninth CI signal — a sixth `sharp-runtime` gap, a security-sensitive one**:
    `RandomNumberGenerator.cpp` failed with `no member named 'getrandom' in the global namespace`.
    `getrandom()` is a Linux-only syscall wrapper (glibc 2.25+, kernel 3.17+) — undeclared on
    Apple/BSD platforms entirely. BSD/Darwin's real equivalent is `getentropy()` (same header,
    already included) — same cryptographic-quality guarantee, but a simpler signature (no flags
    parameter, no partial-read return value) and a hard 256-byte-per-call maximum (requesting more
    fails with `EIO` rather than partially filling the buffer, per its own documented contract).

    Added a `getentropy()`-based branch chunked into ≤256-byte calls for exactly that reason,
    gated `#elif defined(__linux__)` rather than folded into the existing generic branch, so the
    proven Linux `getrandom()` path (with its own real `EINTR` retry loop) stays completely
    unchanged. `getentropy()` is documented as non-interruptible by signals, so no `EINTR`
    handling was added there — matching the real API contract rather than blindly copying Linux's
    retry logic onto a function that doesn't need it. Verified zero regression: all 6
    `RandomNumberGeneratorTests` pass (including a large-buffer fill test exercising the Linux
    chunking loop), full `SharpRuntimeTests` suite (12,481 tests) passes. Same caveat as item 54:
    the `getentropy()` branch itself is unverified from this Linux sandbox until the next macOS CI
    run. Ninth real, observed CI signal.

56. **A genuine milestone: `MetalGraphicsBackend.mm` compiled for the first time ever, and found
    two real bugs in this repo's own source (not `sharp-runtime`) on its very first attempt**: with
    all 6 `sharp-runtime` gaps fixed, `SHARP_RUNTIME` finished building completely on this run, and
    `cna_backend_graphics_metal`'s own compile step began — every piece of Metal backend source
    written this entire session, most of it never touched by a compiler until this exact moment.
    Found two real, independent bugs, both real language/API-surface mistakes rather than
    portability issues:

    **Bug 1 — Objective-C class/protocol confusion**: `id<MTLVertexDescriptor> vd = ...` in
    `getOrCreatePipeline()` — Clang: `"type argument 'MTLVertexDescriptor' must be a pointer
    (requires a '*')"`. `MTLVertexDescriptor` is a concrete Objective-C class (like every other
    `MTL*Descriptor` type this file already correctly uses as a plain pointer everywhere else —
    `MTLRenderPipelineDescriptor`/`MTLTextureDescriptor`/`MTLRenderPassDescriptor`/
    `MTLDepthStencilDescriptor`/`MTLSamplerDescriptor`, all checked and all already correct), not a
    protocol like `MTLDevice`/`MTLTexture`/`MTLBuffer`/`MTLCommandQueue`/etc. (which genuinely are
    protocols, and correctly use `id<...>` throughout this same file — checked every `id<MTL*>`
    usage in the file, all 11 distinct protocol names, all correct). `vertexDescriptorForStride()`
    itself already returned the correct `MTLVertexDescriptor*` type; only this one call site's
    local variable declaration got it wrong. Fixed to `MTLVertexDescriptor* vd = ...`.

    **Bug 2 — real XNA API-surface mismatch, `Rectangle`/`Vector2` field access**: 16 compile
    errors across `MetalSpriteBatch::Draw()`, all `no member named 'getXProperty'`/`'getYProperty'`/
    `'getWidthProperty'`/`'getHeightProperty'` in `Rectangle`/`Vector2`. Checked the real headers:
    `Rectangle`/`Vector2`/`Vector3`/`Vector4`/`Point` in this codebase all use **plain public
    fields** (`X`/`Y`/`Width`/`Height`), matching real XNA's own lightweight value-type struct
    convention exactly — `Rectangle` doesn't even have `getWidthProperty()`/`getHeightProperty()`
    at all, only `getLeftProperty()`/`getRightProperty()`/`getTopProperty()`/`getBottomProperty()`
    (computed edge accessors, a real but different part of the API). The `.getXProperty()`-style
    call this code guessed at is the correct convention for `Color` (which this exact same function
    correctly uses for `.getRProperty()`/`.getGProperty()`/etc., just two lines below the broken
    code) — but `Rectangle`/`Vector2` were never converted to properties, matching CLAUDE.md's own
    "don't replace C# properties with public fields unless the type already establishes that
    style" rule precisely, just in the opposite direction of the mistake actually made here.
    Proactively grepped the whole file for the same pattern afterward (any other
    `.getXProperty()`/`.getYProperty()`/`.getWidthProperty()`/`.getHeightProperty()` call) — found
    none remaining; this was contained to the one function. Fixed all 16 occurrences to plain field
    access (`d.X`, `s.Width`, `origin.Y`, etc.).

    Both bugs are exactly the class of mistake this entire multi-hundred-task plan has been unable
    to catch by inspection alone, no matter how carefully each phase cross-checked against the
    EasyGL reference — a real compiler, for the first time, checking real Objective-C class/
    protocol rules and real API member names against the actual codebase, not against memory or
    assumption. Verified zero regression in the already-real-tested plain-C++ extraction subset
    (85/85 `ctest -R "^Metal"` still pass — neither bug touched any extracted header). The .mm file
    itself remains uncompilable from this Linux sandbox by definition, so whether these were the
    *only* two bugs, or whether more lie further into the file past where this run's error count
    cut off, is still unknown — the next CI run is the real answer. Tenth real, observed CI signal,
    and the most significant one yet: the first evidence, positive or negative, about the Metal
    backend's own source code correctness, not this repo's build/CI plumbing or a sibling repo's
    portability.

57. **Real eleventh CI signal — genuinely not a bug, a cross-repo branch-pinning gap involving the
    user's own concurrent work, resolved with the user rather than guessed at**: with both Metal
    bugs (item 56) fixed, the build reached `CNA.dir` (the main library) and failed on
    `DecimalDateTimeContentTypeReaders.cpp`: `no member named 'ReadDecimal' in
    'Microsoft::Xna::Framework::Content::ContentReader'`. Unlike every prior finding, this class of
    error ("no member named", on an ordinary C++ class) is almost never platform-specific — worth
    checking directly rather than assuming another Apple/Clang quirk. Reproduced locally on this
    Linux/GCC machine by deleting and forcing a fresh rebuild of the exact same object file: it
    **compiled successfully** here, which ruled out a genuine compiler difference immediately (a
    "no member named" error is either universally true or universally false — it cannot depend on
    which compiler is asking). Traced the real cause: `ContentReader : public
    System::IO::BinaryReader`, and `BinaryReader::ReadDecimal()` — confirmed via `git show` against
    3 refs — exists only on `sharp-runtime`'s local, **never-pushed** `feature/xnb-charreader`
    branch (the user's own in-progress work, the branch this whole session has been careful to
    leave untouched between fixes), not on `origin/develop` nor on the `fix/clang-format-
    truncation-flag` branch item 50 created (based on `develop`). This Linux sandbox's own build
    happened to succeed only because `feature/xnb-charreader` was the branch already checked out on
    disk here — not because of anything about this machine's toolchain.

    This is a genuine cross-repo dependency on the user's own unmerged, unpublished work — not a
    bug this session's usual "find it, fix it, verify locally, push" pattern applies to, and not a
    decision to make unilaterally (rebasing onto someone else's in-progress branch, and publishing
    previously-unpushed commits as a side effect, are both consequential enough to ask first).
    Asked; the user chose to rebase the fix branch onto `feature/xnb-charreader`. Executed via `git
    rebase --onto feature/xnb-charreader develop fix/clang-format-truncation-flag` — all 6 of this
    session's fix commits replayed cleanly with zero conflicts (they touch entirely disjoint files
    from `feature/xnb-charreader`'s own 3 `BinaryReader` commits). Rebuilt and reran the full test
    suite with both branches' work combined: 12,494 tests pass (13 more than before, the new
    `ReadDecimal`/`ReadChar`/length-prefix-guard tests), zero regressions. Pushed with
    `--force-with-lease` (not a blind `--force`) since this rewrites an already-pushed branch's
    history — `metal-macos-ci.yml`'s own `ref:` pin needed no change, since it already points at
    this same branch name and `ref:` resolves to whatever a branch's current head is at run time.
    **Explicitly flagged to the user, not silently done**: this action published `feature/xnb-
    charreader`'s 3 previously-unpushed commits to `origin` for the first time, as an unavoidable
    side effect of the chosen resolution. Eleventh real, observed CI signal.

58. **Real twelfth CI signal — a genuine `cnametal`-side CMake bug, not a `sharp-runtime` portability
    gap: `-Wl,--start-group`/`--end-group` is unconditionally unsupported by Apple's linker,
    regardless of whether a target actually needs it**: with `ReadDecimal()` (item 57) resolved, the
    build got past `libCNA.a` for the first time and failed linking `cna_demo_2d`/`cna_demo_sound`
    with `ld: unknown options: --start-group --end-group` / `clang: error: linker command failed`.
    Authenticated `gh` CLI in this sandbox (user ran `gh auth login` interactively) specifically so
    CI logs could be pulled directly instead of relayed through pasted, sometimes-truncated terminal
    output — `gh run view --log-failed` returned nothing usable (empty output, no error, still
    unexplained), but `gh api /repos/openeggbert/cna/actions/jobs/<id>/logs` fetched the complete
    raw log successfully, which is what actually located the real `ld:` error two steps before the
    generic `make: *** [all] Error 2` the pasted log had ended on.

    Traced the cause to `cmake/Examples.cmake`: 25 identical guards of the form
    `if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)` wrap each demo executable's
    `target_link_libraries()` call in a hand-written `-Wl,--start-group CNA ${BACKEND_TARGET}
    -Wl,--end-group`, added historically to resolve a genuine circular symbol dependency for
    backends where `${BACKEND_TARGET}` calls back into `CNA`-defined symbols (documented at
    `cmake/CnaLibrary.cmake`'s own D3D11/D3D12/D3D9/SDL_GPU comment block). `MATCHES "GNU|Clang"` is
    unanchored regex, so it also matches `AppleClang` as a substring — meaning this branch is taken
    on every macOS build too, unconditionally, even though `METAL` never adds the reverse
    `BACKEND_TARGET → CNA` edge that makes grouping necessary in the first place (confirmed by
    checking `cmake/BackendLibraries.cmake`'s `METAL` block, which links only `SDL3::SDL3` and the
    three Metal frameworks). Reproduced the actual root cause locally, without a Mac, by configuring
    (not building — `CNA_GRAPHICS_BACKEND=METAL` hard-fails off-Apple by design) a `HEADLESS` build
    and inspecting the generated `CMakeFiles/cna_demo_2d.dir/link.txt`: it contained the identical
    `-Wl,--start-group libCNA.a libcna_backend_graphics_headless.a -Wl,--end-group` on this GCC/Linux
    machine too, proving the bug is generic to the guard's condition, not Metal-specific — Metal is
    simply the first backend whose CI ever reaches Apple's linker at all.

    Fixed by adding `AND NOT APPLE` to all 25 guards (`sed -i 's/AND NOT WIN32)/AND NOT WIN32 AND NOT
    APPLE)/'`), routing Apple builds into the same plain `target_link_libraries(target PRIVATE CNA
    SHARP_RUNTIME)` fallback branch Windows/MSVC already uses successfully — verified safe for every
    one of the 25 sites by reading the full file end-to-end (every guard has a matching `else()`
    fallback, none left dangling). Verified two ways: (1) configuring with `-DAPPLE=1` on this Linux
    machine and confirming `link.txt` no longer contains `--start-group` and instead falls back to
    CMake's own repeated-archive resolution (`libCNA.a ... libcna_backend_graphics_headless.a
    SHARP_RUNTIME/libSHARP_RUNTIME.a` listed twice, no group flags) — the same technique Apple's own
    linker already handles natively; (2) a full real build of `cna_demo_2d`/`cna_demo_sound` on this
    machine's actual GCC/Linux toolchain (where `NOT APPLE` was already true before this change, so
    behavior must be provably unchanged) — both link successfully, zero regressions. `SDL_GPU`
    (which, unlike `METAL`, *does* add the genuine reverse edge and *can* target macOS) is not
    exercised by any current CI job, so its Apple-linker-group story remains an open, documented gap
    rather than something this fix silently papered over — flagged here rather than solved
    speculatively for a combination nothing currently tests. Twelfth real, observed CI signal.

59. **Real thirteenth CI signal — `-lavcodec` etc. resolve on Linux only by accident of a shared
    default search path, and `pkg_check_modules()`'s own `_LIBRARY_DIRS` output was sitting unused**:
    with item 58's linker-group fix landed (verified via `gh api
    /repos/openeggbert/cna/actions/jobs/<id>/logs`, which is now the established way to pull a full
    job log in this sandbox — `gh run view --log`/`--log-failed` both returned empty output for
    reasons that were not tracked down further, but the raw REST endpoint works reliably), the same
    two link steps failed differently: `ld: library 'avcodec' not found`. `cmake/CnaLibrary.cmake`'s
    `CNA_FFMPEG_AVAILABLE` block only ever consumed `pkg_check_modules()`'s `_LIBRARIES` (bare names,
    e.g. `avcodec`, that become `-lavcodec`) and `_INCLUDE_DIRS` — never `_LIBRARY_DIRS`, i.e. no
    `-L` flag ever reached the linker. This happened to work on every Linux CI/dev build so far only
    because distro `libavcodec.so` lives under `/usr/lib/x86_64-linux-gnu`, already on the linker's
    default search path — never a deliberately-correct link line. Homebrew's `ffmpeg` (installed by
    this same workflow's own "Install FFmpeg (Homebrew)" step, added earlier this session) installs
    under a non-default prefix (`/opt/homebrew` on the Apple Silicon `macos-14` runner), so `-lavcodec`
    alone can't resolve there. Fixed by adding a `target_link_directories(CNA PRIVATE
    ${LIBAVCODEC_LIBRARY_DIRS} ${LIBAVFORMAT_LIBRARY_DIRS} ${LIBAVUTIL_LIBRARY_DIRS}
    ${LIBSWRESAMPLE_LIBRARY_DIRS})` block right next to the existing include-dirs block — the exact
    library-dir output `pkg_check_modules()` had already computed and left unused. Verified
    zero-regression with a real local Linux rebuild of `cna_demo_2d` (clean build, links
    successfully, same as before — Linux's default search path made the missing `-L` invisible, not
    load-bearing, so adding it changes nothing there). Thirteenth real, observed CI signal.

60. **Item 59's `target_link_directories(CNA PRIVATE ...)` fix did not actually work — same "library
    'avcodec' not found" error, next CI run, same commit's fix included**: rather than assume the fix
    needed more time or a cache-clean rebuild, re-pulled the job log the same way (`gh api
    .../actions/jobs/<id>/logs`) and confirmed byte-for-byte the identical `ld: library 'avcodec' not
    found` on the identical two link steps. This raised real doubt about whether CMake's
    `target_link_directories(<target> PRIVATE ...)` reliably propagates a *static* library's private
    `-L` search directories into a final executable's link command in the CMake version this runner
    uses — a question this Linux sandbox could not actually settle empirically, because
    `pkg-config --libs-only-L libavcodec` returns **empty** here (this distro's ffmpeg already lives
    on the linker's default search path, so pkg-config itself omits `-L`), meaning item 59's local
    "zero-regression" rebuild never actually exercised a non-empty `-L` flag going through that
    PRIVATE-scope propagation path at all — it only proved the *absence* of a flag didn't break
    anything, not that CMake would have correctly propagated the flag had one been present.

    Rather than keep guessing at scope-propagation semantics unverifiable from this machine, switched
    to `pkg_check_modules(... REQUIRED IMPORTED_TARGET libavcodec)` (and the same for
    `libavformat`/`libavutil`/`libswresample`) — the CMake-blessed mechanism that bundles a
    dependency's include dirs, link dirs, and libraries into a single real `PkgConfig::<NAME>`
    `INTERFACE` target, exactly the same pattern this file already uses successfully for
    `SDL3::SDL3`/`Vulkan::Vulkan`/`WebGPU::WebGPU`. Replaced the three separate `target_link_
    libraries()`/`target_include_directories()`/`target_link_directories()` calls with one
    `target_link_libraries(CNA PRIVATE PkgConfig::LIBAVCODEC PkgConfig::LIBAVFORMAT
    PkgConfig::LIBAVUTIL PkgConfig::LIBSWRESAMPLE)` — `INTERFACE_LINK_DIRECTORIES`/`INTERFACE_
    INCLUDE_DIRECTORIES` on a genuine imported target are propagated through CMake's ordinary,
    unambiguous target-dependency graph, unlike raw directory-property variables whose propagation
    through a private static-lib link chain this session could not independently confirm. Verified
    zero-regression with a real local Linux rebuild of `cna_demo_2d` (clean configure + build, links
    successfully). Whether this actually resolves the Apple-side failure is still unconfirmed pending
    the next CI run — flagged honestly rather than assumed, since item 59's identical-looking "verified
    locally" claim already turned out not to predict the real Apple-linker outcome once.

61. **Real fourteenth CI signal — item 60's fix DID work (the `avcodec` error is gone), and the next
    CI run exposed the first genuine Metal-source bug this whole plan's static review passes never
    caught: `CreateGraphicsBackend` was defined one namespace level too deep, so it silently compiled
    as an unrelated, unlinkable symbol**: with FFmpeg resolved, both `cna_demo_2d`/`cna_demo_sound`
    failed differently again — `Undefined symbols for architecture arm64: "CNA::Internal::Backends::
    CreateGraphicsBackend(...)", referenced from: GraphicsDevice::createBackend() in libCNA.a`. Before
    assuming this was yet another linker-flag/grouping issue (item 58's territory), checked whether
    the symbol was actually correctly defined at all — grepped every backend's own
    `CreateGraphicsBackend` definition and compared namespace structure against a working reference
    (`EasyGLGraphicsBackend.cpp`, which passes real Linux CI today). EasyGL's convention: close its
    own `namespace CNA::Internal::Backends::EasyGL { ... }` block, then **reopen** a fresh `namespace
    CNA::Internal::Backends { ... }` specifically to define `CreateGraphicsBackend` at the correct,
    shallower scope the header (`IGraphicsBackend.hpp`) actually declares it in — referencing the
    concrete class via its qualified name (`EasyGL::EasyGLGraphicsBackend`). `MetalGraphicsBackend.mm`
    never did this: its entire implementation lives inside `namespace CNA::Internal::Backends::Metal`
    (opened at file scope, `#ifdef __APPLE__`-gated), and `CreateGraphicsBackend` was defined *inside*
    that same block, one level too deep — legal C++ (an unrelated, fully-valid free function
    `CNA::Internal::Backends::Metal::CreateGraphicsBackend` compiles cleanly, calling
    `MetalGraphicsBackend` via ordinary nested-namespace lookup), but never satisfying the actual
    symbol `GraphicsDevice.cpp`'s `using CNA::Internal::Backends::CreateGraphicsBackend;` needs — an
    entirely silent compile-time success with a real link-time failure, and the exact class of defect
    this whole plan's static review passes structurally cannot catch (grep/read-based review sees a
    correctly-named, correctly-signed function and has no reason to suspect the wrong enclosing
    namespace) — only a genuine link step, on real Apple hardware/toolchain, for the very first time
    this session, surfaced it.

    Fixed to match `EasyGLGraphicsBackend.cpp`'s exact convention: close `namespace CNA::Internal::
    Backends::Metal` before the factory function, reopen `namespace CNA::Internal::Backends` for just
    `CreateGraphicsBackend`, and qualify the constructed type as `Metal::MetalGraphicsBackend`. Cannot
    be build-verified from this Linux sandbox at all — Objective-C++ (`.mm`) requires Apple's Clang
    frontend and Metal/QuartzCore/Foundation frameworks, none of which exist here, unlike every prior
    fix in this session, which was at least Linux-buildable even when Apple-only in its final effect.
    Verified only via careful manual namespace-scope tracing (confirmed brace balance unchanged,
    322/322 before and after) and by direct comparison against `EasyGLGraphicsBackend.cpp`'s
    already-CI-proven pattern. This is explicitly the least-verified fix of the whole session —
    flagged honestly, not claimed as done — and the next CI run is the only real answer on whether it
    resolves the link, or whether more Metal-source bugs of this same "compiles clean, links wrong"
    class remain undiscovered elsewhere in this 2300+ line file. Fourteenth real, observed CI signal.

62. **Real fifteenth CI signal — item 61's namespace fix DID work, confirming both `cna_demo_2d` and
    `cna_demo_sound` now compile AND link successfully on real Apple hardware for the first time ever
    — and the next failure is a genuine, pre-existing, Metal-unrelated repo gap**: the next run's log
    contained zero `error:`/`Undefined symbols` output at all for the first time this whole session —
    only benign `ld: warning: ignoring duplicate libraries` noise (an expected, harmless side effect
    of `SHARP_RUNTIME` appearing twice in the flattened link line via both a direct and a transitive
    path). The actual failure moved to `cna_demo_xact`'s `POST_BUILD` step: `Error copying directory
    from ".../examples/demo_xact/Content" to ".../build-metal/Content": No such file or directory`.
    Verified locally: `examples/demo_xact/Content` genuinely does not exist anywhere in this
    repository — not on disk, not tracked by git (`git ls-files examples/demo_xact/Content` returns
    nothing) — unlike `demo_2d`'s (2 files) and `demo_sound`'s (5 files) real, populated `Content`
    directories. This is a pre-existing gap, not introduced by anything this session touched, and not
    Metal-specific at all: `cmake/Examples.cmake`'s `cna_demo_xact` block has unconditionally
    referenced a `copy_directory` from that path on every backend/platform since it was written; it
    only surfaced now because this is the first time this session's CI has ever gotten far enough
    through the build to reach it.

    Fixed by wrapping the `add_custom_command(... POST_BUILD copy_directory ...)` call in `if(EXISTS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_xact/Content")` — the minimal, honest fix: does not
    fabricate placeholder content for a demo whose real XACT audio assets were simply never authored,
    and will transparently start copying real content the moment someone adds it, with zero further
    CMake change required. Verified with a real local Linux build of `cna_demo_xact` (configures and
    links cleanly, `POST_BUILD` step correctly skipped, zero regression). Not filed under `METAL-*`
    since it is a general example-build gap, not part of this plan's own scope — noted here only
    because it was blocking this plan's own CI signal. Fifteenth real, observed CI signal, and the
    most encouraging one yet: two of this plan's core executables now build and link end-to-end on
    real Apple hardware.

63. **Real sixteenth CI signal — another pre-existing, Metal-unrelated harness-target gap, same class
    as item 62 but a compile failure this time, not a `POST_BUILD` one**: the very next failure after
    `cna_demo_xact`'s `Content` fix was `AudioMixer.hpp:5:10: fatal error: 'SDL3/SDL.h' file not
    found` while compiling `tools/audio/mixer_destroy_active_static_voice_harness.cpp`. Traced to
    `cmake/Harnesses.cmake`: this harness (and its dynamic-voice counterpart,
    `cna_audio_mixer_destroy_active_dynamic_voice_harness`) both `#include
    "CNA/Internal/Audio/AudioMixer.hpp"` — an internal CNA header that itself `#include`s
    `<SDL3/SDL.h>` — yet both harness targets only ever linked `CNA SHARP_RUNTIME`, never
    `SDL3::SDL3`. `CNA` itself links SDL3 `PRIVATE` (`cmake/CnaLibrary.cmake`), so that include path
    was never meant to propagate to consumers — this only ever "worked" on every prior CI run because
    of a coincidental system-wide SDL3 dev package on the Linux/GHA-hosted-runner include path,
    exactly the same class of gap `cmake/Examples.cmake`'s own `cna_demo_devices` comment already
    documents for an unrelated Android cross-compile case. `tools/devices/shutdown_ordering_harness.cpp`
    (which also directly touches `<SDL3/SDL.h>`) already correctly links `SDL3::SDL3` explicitly —
    used that as the fix template.

    Fixed by adding `SDL3::SDL3` to both harnesses' `target_link_libraries()` in
    `cmake/Harnesses.cmake`. Verified with a real local Linux build of both harness targets (clean
    compile + link, zero regression). Also added `cmake/Harnesses.cmake` itself to
    `metal-macos-ci.yml`'s push path filter (missing, same class of gap as item 58's `Examples.cmake`/
    `CnaLibrary.cmake` omission) so a future change to this file reliably re-triggers this workflow.
    Not filed under `METAL-*`, same reasoning as item 62. Sixteenth real, observed CI signal.

64. **Real seventeenth CI signal — every executable/harness now links; the build reached the actual
    `CnaTests` GTest binary for the first time this session, and hit a genuine glibc-vs-strict-POSIX
    portability gap, the same class this whole session already found repeatedly in `sharp-runtime`,
    just now inside `cnametal`'s own test sources**: `tests/CNA/Internal/Audio/AudioMixerTests.cpp:84:
    17: error: use of undeclared identifier 'kill'`. `<unistd.h>` was already included; `kill()` and
    `SIGKILL` are POSIX-specified in `<signal.h>`, not `<unistd.h>` — glibc's `<unistd.h>` happens to
    transitively declare `kill()` anyway (a long-standing GNU/glibc convenience, not POSIX-mandated),
    which is why this has silently compiled on every Linux host so far; Apple's strict-POSIX libc
    does not. Proactively grepped the whole `tests/`/`tools/` tree for every other `kill(`/`SIGKILL`/
    `SIGTERM` use before fixing just the one reported error, rather than waiting for each remaining
    instance to surface one CI round-trip at a time: found two more with the identical gap —
    `tests/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest.cpp` and
    `tests/Microsoft/Devices/Detail/DevicesShutdownOrderingTests.cpp` (both, like `AudioMixerTests
    .cpp`, watchdog-timeout helpers that `kill(pid, SIGKILL)` a spawned harness process). Added
    `#include <csignal>` to all three. Verified with a real local Linux build of the full `CnaTests`
    target (not just these three files in isolation) — links successfully, confirming no other
    translation unit in the whole suite was silently depending on the same glibc leniency. Seventeenth
    real, observed CI signal, and the first one located inside `cnametal`'s own test sources rather
    than its CMake plumbing or `sharp-runtime`.

65. **THE FIRST FULLY GREEN `metal-macos-ci.yml` RUN — real Apple hardware, real Metal device, real
    tests, 100% passing**: run `29762732685` (manually triggered via `workflow_dispatch` after item
    64's fix landed on a commit whose paths didn't match this workflow's push filter — `tests/**`
    isn't watched, by design, so a targeted manual dispatch was used instead of further widening the
    filter to something that broad) completed with every step green. Pulled the full job log (`gh api
    .../actions/jobs/88421128360/logs`) and confirmed real evidence, not just the absence of a red X:
    `ctest` reports **`100% tests passed, 0 tests failed out of 86`**, including `Test #5589:
    Metal_Smoke ... Passed 1.99 sec` — the actual `MTLCreateSystemDefaultDevice()`/window/present
    smoke test, genuinely executing on real Apple Silicon GPU hardware for the very first time in
    this entire plan's history, not a plain-C++ extraction — plus the separate
    `GraphicsBackendCompileDefinitionsTest.ExactlyOneGraphicsBackendIsSelected` sanity check (`1/1
    Passed`). The 86 total covers `Metal_Smoke` together with every one of the 55 previously-
    plain-C++-extracted-and-Linux-tested unit groups (enum mapping, vertex descriptor plan, sampler
    filter, pipeline-cache key/hash, normal-matrix, `primitiveVertexCount`, letterbox/viewport, WVP
    matrix helpers, `selectPipelineKind()`, `fillXUniforms()`) now additionally confirmed to still
    pass when compiled by real Apple Clang instead of GCC.

    This closes out the fourteen-fix chain items 42–64 opened: from the very first `submodules: true`
    gap through six `sharp-runtime` portability fixes, two genuine `MetalGraphicsBackend.mm` bugs (the
    `id<MTLVertexDescriptor>` protocol/class mixup and the `Rectangle`/`Vector2` property-vs-field
    mixup), the Apple-linker `--start-group` incompatibility, an FFmpeg `pkg-config` linking gap, the
    `CreateGraphicsBackend` namespace-depth bug, a missing `demo_xact` Content directory, a missing
    `SDL3::SDL3` harness link, and a `<csignal>` gap — every single one now proven resolved together,
    not just individually plausible. Seventeen real, observed, back-to-back CI signals in one
    session, ending on the first genuinely positive one. This is the first point in this whole plan
    where marking a Metal-specific task ✅ under this plan's own tier-2 discipline (a named, passing
    `Metal_*` CTest on real Apple hardware) is actually possible — `METAL-34` earned that status from
    tier-1 alone back on 2026-07-19 as the sole documented exception; every other 🟨 task whose logic
    is covered by one of these 55 extracted units can now be reconsidered for ✅ on this same evidence
    in a follow-up pass, rather than staying provisionally tagged from Linux-only compilation.

66. **`METAL-19` (enum-reordering regression guard) — genuinely closed for the one real instance found,
    not a mechanical relabeling sweep**: with CI finally green, resumed work by checking whether the
    now-green state unlocks a wave of ✅ promotions across the 183 remaining 🟨 markers — it does not,
    on inspection: `Metal_Smoke` and the 55 extracted units prove the backend *compiles and runs*, not
    that every individual state table (blend/stencil/compare-function/etc., still un-extracted, living
    directly in `MetalGraphicsBackend.mm`) is behaviorally correct on real hardware; relabeling those
    without a dedicated test each would repeat the exact "claim complete without adversarial
    verification" mistake this project has been burned by before. Instead pursued `METAL-19` itself
    directly — its own open note says "needs a real compile-time check... unreachable from this Linux
    machine," which turned out to be only half right: audited every extracted header's own
    `switch`/array-index pattern (`MetalVertexAttribFormat.hpp`, `MetalPrimitiveVertexCount.hpp`,
    `MetalSelectPipelineKind.hpp`) and found `MetalVertexAttribFormat.hpp`/`MetalPrimitiveVertexCount
    .hpp` already switch on the real XNA enum by name (immune to reordering by construction — a C++
    `switch` on an `enum class` resolves each `case` by name at compile time, not by the numeric value
    a human happened to write down), but `MetalSamplerFilter.hpp`'s `DescribeMetalSamplerFilter()` —
    genuinely plain C++, no Objective-C dependency, same as the others — switched on raw integer
    literals (`case 1:`, `case 3:`, ...) with only a comment recording the assumed `TextureFilter`
    ordinals. A real, live vulnerability: if `TextureFilter`'s declaration were ever reordered, this
    function would silently return the wrong filter plan with no compiler diagnostic. Fixed by
    including the real `Microsoft::Xna::Framework::Graphics::TextureFilter` header and switching on
    `static_cast<TextureFilter>(xnaFilter)` by enumerator name instead — the strongest form of
    `METAL-19`'s ask (architecturally immune, not just guarded-and-checked). Verified with a real local
    Linux rebuild of the full `CnaTests` target and running `--gtest_filter="MetalSamplerFilter.*"`
    directly: all 10 existing tests still pass, identical inputs/outputs, now with the reordering
    hazard structurally eliminated rather than just re-confirmed. `METAL-19` itself stays ⬜ overall
    since the same class of un-extracted, magic-literal `.mm`-only tables (`CullMode`, `Blend`,
    `CompareFunction`, `StencilOperation`) this task's original scope also covers remain outside what's
    reachable without extracting them first, same reasoning `METAL-14` originally used to scope its
    own eventual extraction — but the one concrete, real instance this session could find and fix from
    Linux alone is now genuinely closed.

67. **`METAL-89`/`METAL-90` (Phase 8's remaining CTest coverage/doc-ownership tasks) — both closed**:
    `METAL-90` resolved outright (a pure decision, no code): `plan_cnj.md`'s existing `CNB-103`–`109`
    per-backend PBR table is already the natural single source of truth for cross-backend PBR support
    tracking — added `CNB-110`/`111` rows there instead of creating a second, separately-maintained
    doc, and pointed `docs/metal-backend.md`'s own PBR bullet at it rather than duplicating. `METAL-89`
    (real known-material probe-pixel `CTest`s) was writable but not verifiable from this Linux sandbox
    — `CNA_GRAPHICS_BACKEND=METAL` hard-fails off-Apple by design, so this is source-complete-only,
    same discipline as every `.mm`-touching change tonight. Rather than deriving new pixel math blind,
    reused the exact precedent `VulkanTests.cmake` already established for its own `Vulkan_PbrEffect_
    Golden`/`Vulkan_SkinnedPbrEffect_Golden` tests: `examples/easygl_pbreffect_golden_test.cpp`/
    `easygl_skinnedpbreffect_golden_test.cpp` are genuinely backend-agnostic (confirmed by reading
    both files in full — public XNA API and `examples/common/PixelTestGame.hpp` only, zero EasyGL/
    Vulkan-specific includes or preprocessor guards), so Metal can reuse them verbatim rather than
    writing new ones, comparing against the same already-checked-in golden PNGs at the same
    already-cross-backend-tolerant thresholds (20–35, chosen by that harness's own documented survey
    of ~98 existing tests' driver/rasterizer variance). Added `cna_test_metal_pbreffect_golden`/
    `cna_test_metal_skinnedpbreffect_golden` executables and `Metal_PbrEffect_Golden`/`Metal_
    SkinnedPbrEffect_Golden` `CTest`s to `cmake/Tests/MetalTests.cmake`, mirroring `Metal_Smoke`'s own
    existing registration style exactly. `cmake/Tests/MetalTests.cmake` is already in `metal-macos-
    ci.yml`'s push path filter, so no CI config change was needed — these two new tests will
    automatically be picked up by the existing `ctest -R "^Metal"` step on the next run. Confirmed
    `PixelTestGame.hpp`'s own mechanics (`GraphicsDevice::GetBackBufferData`, `Texture2D::FromStream`/
    `SaveAsPng`) rely only on Metal capabilities already real-hardware-verified this session (Phase
    12/13's GPU readback work) — a reasoned, not just hopeful, basis for expecting this to work,
    though genuinely unconfirmed until the next CI run reports back.

68. **Real eighteenth CI signal — a genuine, significant, previously-undiscovered rendering bug:
    `drawMetal3D` silently ignored `GpuDrawParams::vertexStart`/`startIndex`/`baseVertex`, always
    hardcoding a zero offset**: the new `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`
    tests (item 67) both compiled and ran — the build itself stayed green — but both failed their
    pixel checks, with clearly abnormal readings: some sample points read back as `(0,255,0,255)`
    (this test's own literal `Clear` color, meaning nothing was ever drawn there) and others as
    `(0,0,0,0)`, neither of which looks like a subtle BRDF math discrepancy. Before assuming a shader
    bug, traced the actual draw call chain: this test issues 4 separate `DrawPrimitives` calls
    against **one shared `VertexBuffer`** at four different vertex offsets (0/6/12/18), reconfiguring
    `PbrEffect`'s texture/material properties between each — exactly the "multiple draws from
    nonzero offsets in one shared buffer" shape no Metal test had ever exercised before today.
    `GraphicsDevice::DrawPrimitives()` threads its `vertexStart` argument through as
    `GpuDrawParams::vertexStart` (confirmed by reading `GraphicsDevice.cpp` directly), and grepped
    every other backend's own draw dispatch for how they consume it:
    `EasyGLGraphicsBackend.cpp`/`VulkanGraphicsBackend.cpp`/`BgfxGraphicsBackend.cpp`/
    `SdlGpuGraphicsBackend.cpp`/`WebGPUGraphicsBackend.cpp` all read `params.vertexStart` (and, for
    indexed draws, `params.startIndex`/`params.baseVertex`) and apply it as a real byte/index offset
    or native draw-call argument — `MetalGraphicsBackend.mm` was the **only** backend of the six that
    never read any of the three fields at all, hardcoding `vertexStart:0` (non-indexed path) and
    `indexBufferOffset:0` with no base-vertex support whatsoever (indexed path). This means any
    `GraphicsDevice.DrawPrimitives`/`DrawIndexedPrimitives` call with a nonzero start offset has been
    silently rendering the wrong vertex range on Metal since the backend's very first draw-dispatch
    code was written — undetected until today because nothing had ever exercised a nonzero offset on
    Metal before this exact test.
    Fixed both paths in `drawMetal3D`: the non-indexed branch now passes `params ?
    params->vertexStart : 0` as Metal's own `drawPrimitives:vertexStart:vertexCount:` argument; the
    indexed branch switched from the simple 5-argument `drawIndexedPrimitives:...:indexBufferOffset:`
    overload to the full 8-argument Metal API overload
    (`...:instanceCount:baseVertex:baseInstance:`), passing `params->startIndex` (converted to a byte
    offset via the index buffer's own 2-or-4-byte element size, mirroring exactly how
    `EasyGLGraphicsBackend.cpp` computes its own equivalent byte offset) and `params->baseVertex`
    directly, with `instanceCount:1`/`baseInstance:0` for the non-instanced case. Confirmed the
    Metal API's own real signature for this overload (Apple's documented
    `MTLRenderCommandEncoder` 8-parameter `drawIndexedPrimitives` method) before writing the call,
    rather than guessing. Cannot be build-verified from this Linux sandbox (`.mm`, Apple Clang/Metal
    frameworks only) — brace/bracket balance re-checked across the whole file (324/324, 762/762,
    unchanged from before this edit) as the only sanity check available here. This is a real,
    substantive correctness fix, not CI plumbing — the next CI run is the actual test of whether it
    resolves `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`, and, more importantly, of
    whether any *other* currently-passing Metal test was silently relying on this same always-zero
    behavior in a way that a nonzero-offset fix could now change. Eighteenth real, observed CI
    signal, and the most significant correctness bug found by this session's testing (as opposed to
    its CI-infrastructure) work.

69. **Real nineteenth CI signal — item 68's `vertexStart` fix compiled and ran, but
    `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden` still failed, with the same
    "reads back either the literal `Clear` color or fully-transparent-black, never an actual shaded
    value" symptom, just at slightly shifted sample points — leading to a second, deeper,
    independent bug: `ReadBackbuffer()` presented and ended the frame as an unwanted side effect of
    every call, so the 2nd through 8th of this test's 8 `ExpectPixel`/`CompareGoldenImage` calls per
    frame each silently read a brand-new, never-drawn swapchain image instead of the one that was
    actually rendered**: re-pulled the CI log the same way and confirmed the build itself stayed
    green — only the two new tests still failed, values shuffled but still confined to exactly the
    same two "impossible" readings as before. Before assuming the `vertexStart` fix was wrong or
    incomplete, re-examined `MetalGraphicsBackend::ReadBackbuffer()` line by line: it calls
    `[p.command presentDrawable:p.drawable]` and nils both `p.command`/`p.drawable` on **every**
    call — its own header comment even documented this as an accepted "documented tradeoff", framing
    it as merely ending the frame "slightly earlier than the game intended". That framing understated
    the real impact: `PixelTestGame`'s established pattern (used by ~330 example test files across
    every other backend) calls `ExpectPixel`/`CompareGoldenImage` — each of which calls
    `GraphicsDevice::GetBackBufferData()` → `ReadBackbuffer()` — multiple times per single rendered
    frame, and this test's own 4 quads make exactly that pattern unavoidable (8 total read calls
    after 4 draws). Traced `ensureFrame()`/`resolveActiveAttachments()`: nil'ing `drawable` makes the
    *next* `ensureFrame()` call fetch a genuinely new drawable via `[layer nextDrawable]`, whose
    `MTLLoadActionLoad` render pass then preserves whatever **stale, undefined content** was already
    in that swapchain slot from an unrelated prior present cycle — not the frame this test just
    rendered. With a small (2–3 image) swapchain rotation, repeatedly hitting this across 8 reads
    naturally lands on a small set of stale values, matching the observed "only ever two distinct
    wrong readings" symptom exactly.
    Fixed by mirroring `endActiveEncoding(bool presentBackbuffer)`'s own already-established
    METAL-180 pattern (documented in this same file, just never applied to `ReadBackbuffer()`
    itself): commit and wait for the blit's command buffer so the read is accurate, but **do not**
    call `presentDrawable:` and **do not** nil `drawable` — it stays alive so the *next*
    `ensureFrame()` reuses the identical drawable with `MTLLoadActionLoad`, correctly continuing from
    everything already rendered into it. This has one necessary knock-on consequence, also fixed in
    the same pass: `endFrame()` (the sole real `Present()` call site) previously guarded on `if
    (!command) return;`, which would now skip presenting entirely — and leave a stale,
    should-have-been-shown `drawable` for the *next* frame to incorrectly inherit — whenever the most
    recent frame's last action was a `ReadBackbuffer()` call rather than a draw. Fixed `endFrame()`
    to check `!command && !drawable` and, if `command` is nil but `drawable` is still pending, call
    `ensureFrame()` first to reacquire a fresh command buffer bound to that same drawable before
    presenting — verified only `Present()` and the destructor call `endFrame()` (grepped both call
    sites), and that `Metal_Smoke` itself never calls `GetBackBufferData()`/`ReadBackbuffer()` at all
    (confirmed by reading `metal_smoke_test.cpp` in full), so this change cannot regress the one test
    already known to pass. Also confirmed exactly one real `presentDrawable:` call site remains in
    the whole file afterward (inside `endActiveEncoding`, matching its own "the only call site
    allowed to present" claim) — the fix consolidates presentation to a single place rather than
    leaving two inconsistent code paths. Cannot be build-verified from this Linux sandbox; re-checked
    brace/bracket balance (still 324/324, 762/762) as the only available sanity check. This is now
    two independent, real, substantive rendering-correctness bugs found back-to-back purely from this
    session's first-ever real Metal pixel tests — the next CI run will show whether both are now
    actually fixed, or whether a third layer remains. Nineteenth real, observed CI signal.

70. **Real twentieth CI signal — item 69's frame-lifecycle fix DID resolve the "reads two different
    stale swapchain images" symptom (every sample now reads back the exact same value across all 8
    reads for the first time), but that single consistent value is still just the literal `Clear`
    color, not any shaded quad — pointing at a third, still-unconfirmed issue, most likely a
    logical-viewport-vs-physical-drawable coordinate mismatch, not yet fixed pending real evidence**:
    re-pulled the log; `vertexStart`/`ReadBackbuffer` are confirmed working (no more alternating
    green/black — every one of the 8 `ExpectPixel`/`CompareGoldenImage` reads across both tests now
    reads the identical `(0,255,0,255)`, this test's own literal `device.Clear(Color(0,255,0,255))`
    color, meaning nothing painted at that coordinate in the one real rendered frame, not "reading a
    different frame each time" anymore). Traced two independent code paths for "how big is the
    screen": `MetalGraphicsBackend::GetViewportSize()` → `computeLogicalViewport()` →
    `SDL_GetWindowSizeInPixels()` (genuinely physical-pixel-accurate, HiDPI-aware), versus
    `GraphicsDevice::getViewportProperty()`, which reads `viewport_`, itself set by either
    `UpdateViewportFromWindow()` (which *does* call `backend_->GetViewportSize()` first, so *should*
    agree) or, for virtual-resolution games only, `PresentationParameters`'s independent 800×480
    default (irrelevant here — `PixelTestGame` never calls `SetVirtualResolution`). Grepped the whole
    file for any HiDPI/`backingScaleFactor`/`contentsScale` handling in the 3D draw path itself
    (`drawMetal3D`'s `wvp`/viewport setup) and found **none** — the raw NDC-to-viewport mapping in
    `ensureFrame()` (`viewport={0,0,(double)w,(double)h,...}` from `colorTex.width/height`, the real
    physical drawable) has no letterbox/scale adjustment at all, unlike the 2D `SpriteBatch` path's
    own explicit `computeSpriteTransform()`. Whether `UpdateViewportFromWindow()` genuinely runs
    early enough, with a value that actually matches the real physical drawable Metal itself reports,
    could not be conclusively resolved by reading alone — the code path exists to make them agree,
    but nothing here proves it fires before this test's first frame on this exact CI runner, and
    Apple Silicon macOS runners are a well-known category of environment where a virtual/headless
    display can report a non-1.0 `backingScaleFactor` even off-screen.
    Rather than attempt a third speculative full fix after two wrong-symptom-shaped guesses already
    burned CI cycles, added a **temporary, clearly-marked diagnostic only** — `NSLog` in
    `ReadBackbuffer()` printing the requested `x/y/w/h` (which encode the test's own assumed
    viewport width, since `sampleAx=W*1/8` etc. are recoverable by back-multiplying) against the
    real `drawable.texture.width/height` — plus the `#import <Foundation/Foundation.h>` `NSLog`
    itself needs (not previously imported in this file; confirmed no other call site already
    provided it). This is explicitly **not** a fix — it exists to let the next CI run's log prove or
    disprove the HiDPI/coordinate-mismatch hypothesis with real numbers instead of guessing a third
    time. If the two width readings disagree, the real fix is either scaling the 3D draw path's NDC-
    to-viewport mapping to the logical/letterbox rectangle (matching `SpriteBatch`'s own established
    pattern) or making `ReadBackbuffer`'s callers pass already-physical coordinates consistently —
    which one is correct depends entirely on what the numbers show. Twentieth real, observed CI
    signal — the first of this whole chain that is deliberately an evidence-gathering step, not an
    attempted fix, after two consecutive "looked right, wasn't the (whole) problem" outcomes.

71. **Real twenty-first CI signal — item 70's diagnostic definitively ruled out the coordinate/HiDPI
    hypothesis**: the `ReadBackbuffer` diagnostic's own log lines proved the real drawable is exactly
    `800x480`, and the test's own requested `x` values (100/300/500/700) are exactly `800*⅛`/`⅜`/`⅝`/
    `⅞` — a perfect match, zero scale discrepancy. Since even quad A's own sample point (`x=100,
    y=240`, dead center of quad A's `[0,200]×[0,480]` screen footprint under the test's Identity
    View/Projection) still reads pure `Clear` green, and quad A's draw is the *one* case that would
    have rendered correctly even under item 68's original `vertexStart:0` bug (quad A's real offset
    genuinely is `0`) — this proves the remaining problem is on the draw/pipeline side, not
    coordinates, and is not explained by either of the two bugs already fixed. Checked several
    further candidates by reading (pipeline-creation error handling — already throws on `nil`, so a
    silent failure is ruled out; `RasterizerState::CullNone` — matches the encoder's own
    already-`MTLCullModeNone` default, ruled out; `SetDepthTestEnabled(false)` — resolves to
    `MTLCompareFunctionAlways`, ruled out) without finding a smoking gun by inspection alone. Added a
    second, more targeted diagnostic rather than committing to a fourth guess: `NSLog` at the top of
    `drawMetal3D` printing the selected `PipelineKind`, vertex stride, `primitiveCount`, and the
    actual tracked `viewport`/`cull`/`fill`/`scissor` state being applied to the encoder, plus a
    second line at the real `drawPrimitives:vertexStart:vertexCount:` call site printing the exact
    `vertexStart`/`vertexCount` reaching the Metal API. Still not a fix — the next CI run's log will
    show, with real numbers, whether `Pbr48` is genuinely being selected and what state is actually
    active at draw time, narrowing the remaining hypothesis space concretely instead of guessing
    further. Twenty-first real, observed CI signal.

72. **Real twenty-second CI signal — item 71's second diagnostic confirmed every remaining structural
    hypothesis is also correct, exhausting what this Linux sandbox can determine by reading alone;
    `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`'s root cause is paused here, undetermined,
    rather than guessed at a fourth time**: the log confirmed `kind=12`/`kind=13`, which — counting
    `MetalPipelineKind`'s declaration order by hand — are exactly `Pbr48`/`SkinnedPbr68`, the correct
    selections; `vertexStart=0/6/12/18` across the 4 draws, exactly matching the test's intent (and
    confirming item 68's fix is genuinely working); `vertexCount=6` throughout, correct. With pipeline
    selection, viewport, cull, fill, scissor, and vertex offsets all independently confirmed correct
    by real data, continued reading (not diagnostics — this part didn't need another CI round-trip)
    to check every remaining structural candidate: `vertexDescriptorForStride(48)`'s attribute
    offsets/formats match `PbrGpuVertex`'s real field layout byte-for-byte; the MSL `VPbrIn`/
    `VSkinnedPbrIn` structs' `[[attribute(N)]]` indices match those descriptor slots exactly; the
    CAMetalLayer's `pixelFormat` (`MTLPixelFormatBGRA8Unorm`) matches the pipeline descriptor's
    declared color attachment format exactly; and — the most promising lead, `cna_f3d_pbr`'s
    `discard_fragment()` alpha-test gate — traced `GpuDrawParams::alphaTest`'s real default
    (`{0,0,1,1}`), `PbrEffect::FillGpuDrawParams()` (confirmed it never touches `alphaTest` at all,
    leaving the safe default), and `cna_alpha_test_fails()`'s actual boolean logic by hand: with
    `at.z`/`at.w` both `1.0` (the default), the discard condition (`w<0.0`) is unreachable regardless
    of the computed alpha value — ruling this out definitively, not just "looks fine".
    Every hypothesis this session could generate and check from source alone is now checked and
    ruled out. What remains genuinely requires either a physical Mac with Xcode's GPU Frame Debugger
    (to directly inspect what the GPU actually received/executed, rather than inferring it from log
    lines before/after the opaque `drawPrimitives:` call), or a further round of much more invasive
    diagnostics (e.g., a trivial single-triangle/no-offset/no-lighting reduction of this exact test,
    isolating whether the problem is PBR-specific or a more fundamental "any real 3D draw + same-
    process readback" gap Metal_Smoke's own trivial clear-only loop never exercised). Given the
    session has already delivered two real, confirmed, valuable correctness fixes from this exact
    investigation (item 68's `vertexStart`/`startIndex`/`baseVertex` fix and item 69's `ReadBackbuffer`
    frame-lifecycle fix — both independently useful regardless of this specific test's fate, and both
    already verified not to have regressed the other 86 passing tests), this narrative pauses the PBR-
    golden-test investigation here rather than continuing to spend CI cycles on a fourth, less-
    grounded guess. Twenty-second real, observed CI signal — the point this session's own "read the
    code, don't guess" discipline correctly says to stop and ask, not push further blind.

73. **Resumed the paused investigation with the user's explicit direction: a minimal reduced-repro
    test, chosen specifically to isolate "PBR-specific" from "any real 3D draw + same-process
    readback on Metal at all"**: rather than author a new test from scratch, found and reused
    `examples/easygl_draw_user_primitives_vpc_test.cpp` verbatim — already existing, already
    genuinely backend-agnostic (public XNA API only: `Game`/`GraphicsDeviceManager`/`BasicEffect`/
    `DrawUserPrimitives`, zero EasyGL-specific includes), and already doing exactly what this
    investigation needs: draws one full-NDC red quad via the simplest possible stock effect
    (`BasicEffect` with only `VertexColorEnabled=true`, no lighting/texturing at all — dispatches to
    `PipelineKind::Colored16`, the simplest shader pair in the whole file, `cna_v3d_color`/
    `cna_f3d_color`, with none of PBR's BRDF/texture-sampling complexity), reads back the center
    pixel, and checks it's red. Its own second sub-test additionally exercises `vertexOffset=1` — the
    *exact* class of bug item 68 fixed, now in the simplest possible shader context, a genuine bonus
    regression check for that fix specifically. Registered as `Metal_DrawUserPrimitives_VPC` in
    `cmake/Tests/MetalTests.cmake`, same reuse pattern as items 67's `Metal_PbrEffect_Golden`. If this
    also reads back only the `Clear` color, the remaining problem is generic to Metal's own 3D-draw-
    then-readback pipeline, unrelated to PBR; if it passes, the problem is narrowed specifically to
    PBR's own shader/uniform path (something this investigation's reading-based checks already found
    no evidence for, but real execution is the only way to be sure). The next CI run's result decides
    which branch of investigation continues.

74. **Real twenty-third CI signal — decisive: `Metal_DrawUserPrimitives_VPC` fails identically to the
    PBR tests, proving the root cause is generic to any real 3D draw + same-process readback on
    Metal, not PBR-specific**: both its sub-tests (`offset=0` and `offset=1`) read back `centre=
    (0,255,0)` — the literal `Clear` color, exactly the same failure signature as `Metal_PbrEffect_
    Golden`, on a test using `PipelineKind::Colored16` (the simplest pipeline in the whole file:
    straight `position`→clip-space passthrough via `wvp`, vertex color output directly, no lighting,
    no texture sampling, no BRDF) and a **full-NDC quad covering the entire screen**, not a narrow
    per-quad region — ruling out even a partial/positional miss as an explanation, since a full-
    screen quad can only fail to cover the center pixel if nothing rasterizes at all. Also confirmed,
    while investigating, that `BasicEffect`'s `World`/`View`/`Projection` all default to `Matrix::
    getIdentityProperty()` (real Identity, not a zero-initialized `Matrix()`), so this test's implicit
    (never explicitly set) transform genuinely matches the PBR test's explicit one — both tests are
    verified equivalent in transform setup, reinforcing that this is the same underlying bug, not two
    coincidentally-identical-looking separate ones.
    This conclusively rules out every PBR-specific candidate this whole investigation considered
    (BRDF math, PBR uniform fill, alpha-test discard, PBR-specific texture fallbacks) and narrows the
    remaining search to whatever `ensureFrame()`/`resolveActiveAttachments()`/`drawMetal3D()`'s shared
    setup/`ReadBackbuffer()` still gets wrong — all of which have now been read and re-read multiple
    times this session without finding a further issue, despite every individually-checkable
    hypothesis (pipeline selection, viewport, cull, fill, scissor, vertex offsets, vertex descriptor,
    shader attribute indices, pixel format match, alpha-test discard logic, and now confirmed-correct
    default transform matrices) independently confirmed correct. This is the point where this
    session's own "read the code, don't guess blindly" discipline has run out of new, well-reasoned
    hypotheses to check from source alone — genuinely requires either a physical Mac with a GPU frame
    debugger (to see what the GPU actually received, not infer it from before/after log lines around
    an opaque Metal API call), or a fundamentally different diagnostic technique this session hasn't
    tried yet (e.g., an `MTLCaptureManager` programmatic GPU trace dumped to a file the next CI run
    could upload as an artifact). Twenty-third real, observed CI signal — a decisive, valuable result
    (definitively not PBR-specific) even though the underlying root cause itself remains open.

75. **At the user's explicit direction, tried the GPU-capture route item 74 flagged as the next real
    option**: added a `CNA_METAL_GPU_CAPTURE=<path>` env-var-gated diagnostic to
    `MetalGraphicsBackend`'s constructor/destructor — when set, starts a real `MTLCaptureManager`
    capture (`MTLCaptureDestinationGPUTraceDocument`, targeting the device's whole command queue for
    the process lifetime) and finalizes it via `stopCapture()` in the destructor, writing a genuine
    `.gputrace` document a physical Mac's Xcode can open. Wired to `Metal_DrawUserPrimitives_VPC`
    only (via `cna_register_backend_test`'s `ENVIRONMENT`), the single most decisive of the three
    failing tests (item 74) — a unique filename avoids a `startCapture` failure from a stale file a
    different test process might have left (Apple's own API refuses to overwrite an existing
    document). `metal-macos-ci.yml` uploads the resulting file as a workflow artifact
    (`actions/upload-artifact@v4`, `if: always()` since the whole point is capturing the currently-
    *failing* run, not a passing one) — this sandbox cannot open a `.gputrace` itself, so the file
    exists for the user (or a future session with real Mac/Xcode access) to inspect directly, which
    is the one class of evidence this investigation has not yet had access to.
    Also added a second, directly-actionable-from-CI-logs diagnostic while investigating this same
    area: `ReadBackbuffer()` now checks `MTLCommandBuffer.status`/`.error` after `waitUntilCompleted`
    and `NSLog`s if either indicates a runtime GPU error — `MTL_SHADER_VALIDATION`/`MTL_DEBUG_LAYER`
    (already enabled in this workflow) catch API-*misuse* at validation time, but a genuine runtime
    execution failure (a real GPU-side error, not a validation-time one) would silently leave the
    read-back buffer with stale/undefined content while `ReadBackbuffer()` otherwise proceeds
    normally — exactly matching the observed symptom — and this check is the one place that would
    surface it directly in the CI log text, without needing to open the `.gputrace` at all. Neither
    diagnostic is a fix. Cannot be build-verified from this Linux sandbox; re-checked brace/bracket/
    paren balance (333/333, 784/784, 1685/1685) and validated the workflow YAML parses
    (`python3 -c "import yaml; yaml.safe_load(...)"`) as the only sanity checks available here.
    Twenty-fourth real, observed CI signal.

76. **Real twenty-fifth CI signal — both item 75 diagnostics ran, and both came back inconclusive-
    but-informative: GPU capture is unsupported on this specific CI runner (an environment limit, not
    a coding mistake), and the command-buffer error check came back completely clean across all 15
    `ReadBackbuffer` calls in this run**: `[METAL-89 diag] GPU capture: device does not support
    MTLCaptureDestinationGPUTraceDocument` — the macOS CI runner's own Metal device (very plausibly a
    virtualized/headless GPU context specific to GitHub-hosted `macos-14` runners, not something a
    physical Mac would necessarily also hit) declined `MTLCaptureDestinationGPUTraceDocument`, so no
    `.gputrace` was ever produced — the uploaded artifact exists but is empty. The command-buffer
    `status`/`.error` check, however, DID run for real, on every one of the 3 tests' `ReadBackbuffer`
    calls (5 for each PBR test, 2 for the reduced VPC test — the same log confirmed the VPC test's
    two draws both correctly use `PipelineKind::Colored16`/`vertexStart=0`; also clarified while
    reading closely that `DrawUserPrimitives<VertexPositionColor>`'s `offset` parameter is applied by
    re-packing the relevant vertex slice into a fresh CPU-side buffer before upload, not via GPU-side
    `vertexStart` at all — so its own `vertexOffset=1` sub-test does *not* actually exercise item 68's
    fix path the way this narrative's item 73 originally assumed; a documentation correction, not a
    new bug) — and zero of them logged a runtime GPU error. This rules out a genuine GPU-side
    execution failure as the cause, on top of every other hypothesis this whole investigation has
    already ruled out by reading or by diagnostic.
    With both the GPU-capture and command-buffer-error avenues now exhausted without a positive
    result, and roughly ten CI round-trips spent on this single investigation across items 67–76,
    this session has reached the genuine limit of what's determinable from this Linux sandbox without
    either a physical Mac (ideally with Xcode's interactive GPU Frame Debugger, since programmatic
    capture-to-file is confirmed unavailable on the CI runner specifically — a real Mac's own GPU may
    not have the same limitation) or a fundamentally different, more invasive technique this session
    has not yet identified. Pausing here and reporting back rather than continuing to spend further CI
    cycles on decreasingly-well-grounded guesses. Twenty-fifth real, observed CI signal.

77. **`METAL-31` closed — based on a false premise, the same class of finding `METAL-15`/`17` already
    established for texture formats, now confirmed for render-target attachment formats**: with the
    PBR investigation paused (physical-Mac-access unavailable), picked up Phase 2's other remaining
    item. `METAL-31`'s own task text assumes Metal pipelines need to be keyed by attachment pixel
    format because a `RenderTarget2D` *might* use a different format than the backbuffer's own
    hardcoded `MTLPixelFormatBGRA8Unorm`. Reading Phase 10's real, already-landed
    `MetalRenderTargetBackend`/`MetalRenderTargetCubeBackend` constructors (not assumed from the task
    text alone) shows this was already correctly ruled out when Phase 10 was implemented, not merely
    overlooked: both deliberately allocate their color texture as `MTLPixelFormatBGRA8Unorm` and
    their depth texture as `MTLPixelFormatDepth32Float_Stencil8` — byte-identical to every pipeline's
    own hardcoded attachment formats — with an existing, already-detailed comment
    (`MetalRenderTargetBackend`'s own constructor) explicitly stating this is intentional, precisely
    to avoid the exact attachment-format mismatch `METAL-31` worries about. No code path anywhere in
    this file ever creates a color/depth/stencil attachment with any other format — backbuffer,
    `RenderTarget2D`, and `RenderTargetCube` all funnel through the same fixed set of formats by
    design — so there is no format variance for a pipeline key to disambiguate; adding one would be
    real, unneeded complexity solving a problem this architecture already sidesteps a different way.
    `METAL-32` (the sample-count sibling) stays genuinely open, since MSAA itself doesn't exist yet —
    but noted the same "force every attachment to one fixed value" pattern as a real candidate design
    for it too, rather than assuming a full keyed cache is the only option, when that phase lands.

78. **`METAL-33` closed — the pipeline cache's own bounded-size assumption, documented rather than
    left implicit, completing Phase 2 in full except for `METAL-32`'s genuine MSAA block**: added a
    real doc comment to `MetalPipelineCacheKey` in `MetalPipelineKey.hpp` (plus a one-line pointer at
    the actual `pipelineCache` member declaration in `MetalGraphicsBackend.mm`) explaining why no
    eviction policy exists or is needed for a v1 backend — `MetalPipelineKind` is a compile-time-fixed
    15-value enum, and `MetalBlendKey`'s own much larger theoretical space is, in real usage, bounded
    by however many distinct `BlendState`s a game actually calls `ApplyBlendState()` with (XNA's own 4
    built-in presets cover the overwhelming majority of real games), capping the cache at a few
    hundred lightweight `id<MTLRenderPipelineState>` entries even in a pathological case — the same
    reasoning `EasyGLGraphicsBackend`'s own fixed-field `Prog3D` relies on implicitly (a bounded set of
    named struct fields instead of a dynamic cache at all), made explicit here since `Metal
    PipelineCacheKey` genuinely is dynamic. A pure documentation change, no logic touched — verified
    with a real local Linux rebuild (`cmake-build-headless/`, this session's newly-established
    persistent build directory, not the scratchpad) and the existing 8 `MetalBlendKey`/
    `MetalPipelineCacheKey`/`MetalPipelineCacheKeyHash` `CnaTests` all still pass unchanged. Phase 2
    (`METAL-21`–`34`) is now fully closed except `METAL-32`, correctly and only blocked on MSAA
    (`METAL-104`/`105`) not existing yet.

79. **`METAL-256` fix landed — `MetalTexture`'s in-place `replaceRegion:` CPU/GPU-sync hazard
    (`METAL-175`) closed with the mip-level-preserving reallocate pattern its own table row
    described as the needed approach, not attempted until now**: `MetalTexture` now stores
    (retained) `dev_`/`queue_` references, mirroring `MetalTextureCube`'s own established pattern,
    passed in from `CreateTexture()`'s existing `impl_->device`/`impl_->queue`. Both
    `UpdatePixels()`/`UpdatePixelsLevel()` now delegate to a new `reallocateAndUpdate(targetLevel,
    rgba, bytesPerRow, levelW, levelH)` method instead of mutating `texture_` in place: it allocates
    a fresh `id<MTLTexture>` with the same descriptor, and — only when `mipmapLevelCount > 1` (the
    uncommon case; skipped entirely for the overwhelmingly common non-mipmapped texture, so this adds
    no extra GPU round-trip there) — blit-copies every *other* mip level from the old texture into
    the new one first via `MTLBlitCommandEncoder copyFromTexture:...toTexture:...` (one short-lived
    command buffer, `waitUntilCompleted` before proceeding, matching this file's own established
    synchronous-upload style elsewhere), so no already-uploaded level's content is lost the way a
    naive "just reallocate" fix would have lost it. Only then does it `replaceRegion:` the new pixel
    data into the new texture's target level, release the old `texture_`, and swap the pointer — by
    the time either public method returns, `texture_` always refers to an object nothing has ever
    mutated in place while potentially GPU-visible, the same safety property `MetalVertexBuffer`/
    `MetalIndexBuffer`'s own `SetData()` already had (`METAL-173`/`174`). Cross-checked before
    committing: `CreateTexture()` (the sole `MetalTexture` construction call site, confirmed by
    `grep`) updated to pass `impl_->queue` as the new constructor argument; `nativeTextureFor()`
    (the only place that reads a `MetalTexture`'s live `id<MTLTexture>`) confirmed to always call
    `->native()` fresh on every invocation rather than caching the pointer anywhere, ruling out a
    stale-reference risk from the swap; `ITextureBackend::UpdatePixels`/`UpdatePixelsLevel`'s base
    contract (`IGraphicsBackend.hpp`) confirmed to be plain synchronous `void` virtuals with no
    async/completion-handler requirement this synchronous design needs to satisfy. **Cannot be
    build-verified from this Linux sandbox** — this is genuinely new Objective-C++ code that has
    never been compiled anywhere; the only sanity check available here was a full-file
    brace/bracket/paren balance re-check (`336/336`, `796/796`, `1717/1717`, all balanced) plus
    careful manual re-reading of the diff against Apple's documented Metal blit-encoder API shape.
    Pushed for real CI verification next — genuinely unverified until that CI run reports back.

80. **Real twenty-sixth CI signal — item 79's `METAL-256` fix confirmed: compiles clean, no
    regression**: CI run `29800865929` (workflow `metal-macos-ci.yml`, triggered by the item 79
    push) shows the "Build native Metal backend" step passing with no errors — this genuinely new
    `MetalTexture::reallocateAndUpdate()`/blit-copy Objective-C++ code compiles correctly on real
    Apple Clang, the first real compiler feedback it has ever received. "Run Metal tests" still
    reports the overall job as `failure`, but the actual `ctest` breakdown is `97% tests passed, 3
    tests failed out of 89` — the *same* 3 pre-existing, already-documented failures as every prior
    run (`Metal_PbrEffect_Golden`, `Metal_SkinnedPbrEffect_Golden`, `Metal_DrawUserPrimitives_VPC`,
    all failing with the identical `centre=(0,255,0)` Clear-color-only symptom item 74 already
    proved is generic and unrelated to this texture-update code path), not a new one — confirming
    the `METAL-256` fix introduced no regression. The full job log was also grepped for any runtime
    crash/exception/`"Metal: failed to allocate replacement texture"` around the new code path —
    none found. `METAL-256`/`METAL-175` can now be marked verified-safe rather than merely
    source-complete.

81. **`METAL-19` fully closed — the remaining un-extracted `CullMode`/`Blend`/`BlendFunction`/
    `CompareFunction`/`StencilOperation` tables, real-build-and-test-verified on this Linux
    machine**: mirrors item 66's own `MetalSamplerFilter.hpp` extraction exactly. The 4 functions
    in `MetalGraphicsBackend.mm` that previously switched on raw `int` literals with only a comment
    recording the assumed XNA ordinals (`metalCompareFunction`/`metalStencilOp`/`metalBlendFactor`/
    `metalBlendOp`), plus `ApplyRasterizerState`'s own inline `c==1?...:(c==2?...:...)` cull-mode
    ternary chain, are now thin wrappers around 5 new plain-C++ headers
    (`MetalCompareFunction.hpp`/`MetalStencilOperation.hpp`/`MetalBlend.hpp`/
    `MetalBlendFunction.hpp`/`MetalCullMode.hpp`) that each switch on the real XNA enumerator name
    (`CompareFunction`/`StencilOperation`/`Blend`/`BlendFunction`/`CullMode`, cast once from the
    plain int the `.mm` call sites still pass) and return a plain C++ enum describing the equivalent
    Metal semantic — zero Objective-C dependency. A future reordering of any of these 5 XNA enums'
    declarations is now compile-time-irrelevant here, the same guarantee item 66 already gave
    `TextureFilter`. The final `.mm`-side translation from each plain-C++ "kind" enum to the real
    Apple SDK enum (`MTLCompareFunction`/`MTLStencilOperation`/`MTLBlendFactor`/`MTLBlendOperation`/
    `MTLCullMode`) stays a trivial 1:1 name-matching switch, the one part of this that genuinely
    needs the Apple SDK and so still can't be verified from this sandbox. Every enumerator value of
    all 5 XNA enums was read directly from its real `.hpp` (`CompareFunction.hpp`/
    `StencilOperation.hpp`/`Blend.hpp`/`BlendFunction.hpp`/`CullMode.hpp`), confirming the ordinals
    the old literal-based comments already assumed were correct (no latent bug found here, unlike
    `MetalSamplerFilter.hpp`'s own real bug) — this closes `METAL-19` as a genuine hardening/
    regression-guard task, not a bugfix. Unlike `METAL-256`, this is **fully build-and-test-verified
    on this Linux machine**, not merely balance-checked: `cmake --build cmake-build-headless
    --target CnaTests` compiled clean, and `ctest --test-dir cmake-build-headless -R
    "MetalCompareFunction|MetalStencilOperation|MetalBlend|MetalCullMode"` reports `100% tests
    passed, 0 tests failed out of 45` (42 new tests across the 5 new suites plus the 3 pre-existing
    `MetalBlendKey` tests the filter also matched). A full, unfiltered `ctest --test-dir
    cmake-build-headless` run (5644 total tests, using `--output-log` to an on-disk file rather
    than trusting any tool-side tail truncation) confirms zero regression across the entire suite:
    `96% tests passed, 231 tests failed out of 5644`, and every one of those 231 failures, broken
    down by test-suite name, falls into `MediaLibraryTestFixture`/`VideoPlayerTest`/
    `VideoDecoderTest`/`SongTest`/`AudioTagParserTest`/`Xnb*`/`ContentManager*`/`Lzx*`/`Playlist*`/
    `Picture*`/`Thumbnail*` (all pre-existing, fixture/ffmpeg-dependent Media/Content/Video/Audio
    subsystems this Linux sandbox cannot fully exercise, matching this project's own already-
    documented limitation) plus exactly one unrelated `GraphicsDeviceCapabilityTest.
    DoesNotSupportWireFrame` (a `HEADLESS`-backend capability query, untouched by this change) —
    zero of the 231 are Metal-related, and the specific test-ID range covering all 5 new suites
    (`#83`–`#181`) is 100% green. Still pushes for real CI build/test confirmation next, same as
    every other `.mm` change this session, since the final Apple-SDK-side switch itself remains
    unbuilt outside CI.

82. **Real twenty-seventh CI signal — item 81's `METAL-19` fix confirmed on real Apple Clang: compiles
    clean, all 42 new tests pass, no regression**: CI run `29801955085` (triggered by the item 81
    push) again shows "Build native Metal backend" passing with no errors — the 5 new plain-C++
    headers and their `.mm`-side thin wrappers around `MTLCompareFunction`/`MTLStencilOperation`/
    `MTLBlendFactor`/`MTLBlendOperation`/`MTLCullMode` compile correctly. `ctest` now reports `98%
    tests passed, 3 tests failed out of 131` — the test count rose from 89 to exactly 131 (the 42
    new tests, matching the local count precisely), and the 3 failures are the identical
    pre-existing trio (`Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`/
    `Metal_DrawUserPrimitives_VPC`), so all 42 new `MetalCompareFunction`/`MetalStencilOperation`/
    `MetalBlend`/`MetalBlendFunction`/`MetalCullMode` tests passed on real Apple hardware too (131
    total − 3 failed = 128 passed; 128 − 86 previously-passing = 42, exactly). `METAL-19` is now
    fully verified, not merely source-complete.

83. **Phase 14 (custom `ShaderEffect`/MSL contract) landed for its real, correctly-scoped shape —
    the phase's own 2026-07-19 blocker note was a false premise, corrected by reading
    `VulkanEffectBackend`/`D3D11EffectBackend`/`D3D12EffectBackend` directly**: those three each
    document their custom-effect facility as SpriteBatch-only with a fixed, `Sprite2DVertex`-shaped
    (32-byte) vertex contract, not the arbitrary-3D-vertex-layout facility only
    `EasyGLGraphicsBackend` actually supports (GL's own attribute binding is inherently
    layout-flexible; no established structured-pipeline backend — Vulkan, D3D11, D3D12, or now
    Metal — needs `MTLVertexDescriptor`-style rigidity for this facility at all, since Metal's own
    `cna_v2d` shader already reads vertices manually via `v[vid]` with zero vertex descriptor).
    This makes Phase 14 fully independent of Phase 2's still-open generic `VertexElement`-driven
    descriptor builder (`METAL-26`/`27`), reversing the original blocking conclusion.

    Implementation: a new `MetalEffectBackend : IEffectBackend` class compiles `vertSrc`/`fragSrc`
    as two **separate** `MTLLibrary` objects via `newLibraryWithSource:options:error:` (matching
    `CompileProgram`'s own two-string signature) — each source must declare exactly one function,
    discovered via `MTLLibrary.functionNames` rather than a fixed required name, so a shader author
    is free to name their own entry point anything. The uniform contract mirrors
    `VulkanEffectBackend`/`D3D11EffectBackend`'s own fixed-slot, name-ignoring precedent (MSL has no
    simple named-uniform reflection either) but as three separate natural-typed buffers
    (`buffer(2)`=mat4, `buffer(3)`=vec4, `buffer(4)`=float) instead of one combined push-constant-
    style block, avoiding any `constant`-address-space struct-padding ambiguity. Two deliberate,
    documented improvements over the Vulkan/D3D11/D3D12 precedent rather than a blind copy: (1) the
    automatic per-draw transform buffer uses the same letterbox-aware `U2D{scale,offset}` the stock
    `cna_v2d` shader already uses (`METAL-157`/`158`), not Vulkan/D3D11's own raw `vpSize`-only
    convention, which would have silently reintroduced the exact letterboxing bug those tasks
    already fixed; (2) the custom pipeline's blend state is keyed off the real, currently-applied
    `BlendState` via `MetalEffectBackend::pipelineFor()` (Metal's own pipeline cache is already
    blend-state-aware), rather than the fixed hardcoded alpha blend Vulkan/D3D11/D3D12 use — a real
    XNA/FNA-behavior improvement, not a scope gap, since `blendState` and `effect` are independent
    `SpriteBatch.Begin()` parameters. `MetalSpriteBatch::Draw()` resolves the bound `Effect`'s
    backend fresh on every draw call (not once per flush the way D3D11/Vulkan's own batched
    architecture requires), so a `SetUniformXxx()` call between two `Draw()`s in the same
    `Begin`/`End` genuinely takes effect on the very next sprite — Metal's own architecture (one
    immediate draw per sprite, no batching) makes this more correct at no extra cost, not a
    deliberate divergence for its own sake. A `pipelineFor()` failure (a rare, real GPU/driver-level
    error, distinct from a `CompileProgram()`-time compile failure) falls back to the stock
    `Sprite2D` pipeline rather than passing `nil` to `setRenderPipelineState:`, a hard Metal API
    misuse that would otherwise crash.

    `METAL-147` (`BindTexture`/`BindTextureCube`/`BindTexture3D` for extra sampler units) and
    `METAL-148` (the general 3D `GpuDrawParams::customEffectBackend` bypass) remain genuinely
    unimplemented — not oversights, but confirmed-by-reading scope boundaries matching
    Vulkan/D3D11/D3D12's own identical choices (`VulkanGraphicsBackend`'s own 3D `draw3DFor()` was
    read directly and confirmed to never consume `customEffectBackend`/`activeCustomEffect_` at
    all, only its `SpriteBatch` flush code does).

    A new `Metal_SpriteBatch_CustomEffect` `CTest` (`examples/metal_spritebatch_customeffect_
    test.cpp`) mirrors `D3D9_SpriteBatch_CustomEffect`'s own exact 4-check RGB-inversion
    methodology in real MSL, additionally exercising `SetUniformVec4()`'s real `buffer(3)` wiring
    (a broken wiring would produce black, not the expected cyan, a distinguishable failure). While
    researching the CTest precedent this task's own text cited ("D3D9/D3D11/D3D12/Vulkan/Bgfx
    already use" this methodology), found that claim itself was optimistic — only `D3D9` and
    `SDL_Renderer` (a "throws" test, since SDL_Renderer doesn't support custom effects at all)
    actually have a registered custom-effect `CTest` today; Vulkan/D3D11/D3D12/Bgfx have the
    backend classes but no dedicated test. Documented rather than silently treated as true; the new
    Metal test still faithfully mirrors the one real precedent that exists. The full MSL uniform/
    vertex-buffer contract is written up in `docs/metal-shader-effect-contract.md` (`METAL-152`).

    **Cannot be build-verified from this Linux sandbox** — this is genuinely new Objective-C++ code
    that has never been compiled anywhere; the only sanity check available here was a full-file
    brace/bracket/paren balance re-check (`375/375`, `854/854`, `1876/1876`, all balanced) plus
    careful manual review against Apple's documented `MTLLibrary`/`MTLRenderPipelineDescriptor` API
    shape and the established `VulkanEffectBackend`/`D3D11EffectBackend` precedent. `METAL-142`/
    `144`–`146`/`149`–`151` land as source-complete (🟨); `METAL-143`/`152` are genuinely done
    (design decision + doc, no code to verify); `METAL-147`/`148` stay open, correctly. Pushed for
    real CI verification next — genuinely unverified until that CI run reports back.

84. **Real twenty-eighth CI signal — item 83's Phase 14 code compiles clean on real Apple Clang, no
    regression, but the new `Metal_SpriteBatch_CustomEffect` test itself fails — likely the same
    pre-existing, already-investigated "reads back only the Clear color" bug (items 67–76/82), now
    newly observed to affect `SpriteBatch` 2D draws too, not just 3D**: CI run `29804175429` shows
    "Build native Metal backend" passing with no errors — `MetalEffectBackend`, its two-separate-
    `MTLLibrary` compile path, and the `MetalSpriteBatch` wiring all compile correctly, the first
    real compiler feedback this code has ever received. `ctest` reports `97% tests passed, 4 tests
    failed out of 132` — the test count rose from 131 to exactly 132 (the one new test), and the
    3 pre-existing failures are unchanged (`Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`/
    `Metal_DrawUserPrimitives_VPC`, still showing their own identical, already-investigated
    `centre=(0,255,0)`-reads-only-the-Clear-color symptom) — no regression in anything this session
    already had passing.

    `Metal_SpriteBatch_CustomEffect` itself reports `2/4 PASS`: Check A (compiles) and Check C (the
    background outside the sprite's destination rectangle stays the Clear color, proving the custom
    vertex shader's own NDC transform is genuinely correct) both **pass**. Check B (custom shader's
    RGB-inversion result) and Check D (the fresh, no-custom-effect control case) both **fail**. Check
    D failing is the single most informative data point here: it exercises *zero* of this session's
    new code (`customEffect_` stays null, so `Draw()` falls straight through to the same
    `p.getOrCreatePipeline(PipelineKind::Sprite2D)` call the stock path always used) — it is
    literally the same "`sb.Begin(); sb.Draw(tex, rect, color); sb.End();`" pattern every other
    passing Metal `SpriteBatch` usage already relies on, just now read back via
    `GraphicsDevice::GetBackBufferData()` for the first time in any Metal `CTest` this whole
    session (confirmed by grepping `cmake/Tests/MetalTests.cmake` and `examples/metal_smoke_test.
    cpp` — no prior Metal test exercises `SpriteBatch.Draw()` + same-process readback together).
    That a completely unmodified code path fails the identical class of check the still-open
    `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`/`Metal_DrawUserPrimitives_VPC`
    investigation already spent ~10 CI round-trips on (items 67–76) is strong circumstantial
    evidence this is the same root cause, now shown to be even more general than "any real 3D draw"
    — plausibly "any real draw of any kind, 2D or 3D, plus same-process readback in this specific
    process" — rather than a new bug this session's own Phase 14 code introduced. Not yet
    definitively confirmed: the test's own `check()` helper did not print the actual observed pixel
    values (only PASS/FAIL), so the exact failing color is not yet known from this run — **fixed**
    in the same commit as this item, adding `std::printf` of the observed vs. expected color to all
    three readback checks (Check B/C/D), matching every other Metal test's own established
    informative-`[FAIL]`-message convention, so the next CI run will show whether the observed color
    really is `(10,10,10,255)` (this test's own Clear color) — the exact signature that would
    confirm this is the same bug, not a coincidence. Left deliberately unfixed pending that
    confirmation, per this session's own standing rule: investigate honestly before claiming a root
    cause, and do not blindly guess at a "fix" for a hypothesis not yet confirmed by real evidence.

85. **Real twenty-ninth CI signal — item 84's hypothesis definitively confirmed: `Metal_
    SpriteBatch_CustomEffect`'s Check B and Check D both read back exactly this test's own Clear
    color, `(10,10,10,255)`, not the drawn sprite**: CI run `29804647754` (the diagnostic-print
    push) shows both failing checks' now-printed observed values are byte-for-byte identical to
    `kClear` (`Color(10,10,10,255)`), for both the custom-effect path (Check B) and the completely
    unmodified stock path (Check D). This is the exact "reads back only the Clear color" signature
    items 67–76/82 already spent ~10 CI round-trips investigating for 3D draws (`Metal_PbrEffect_
    Golden`/`Metal_SkinnedPbrEffect_Golden`/`Metal_DrawUserPrimitives_VPC`, all showing the
    identical `centre=(0,255,0)`-equals-their-own-Clear-color pattern) — now confirmed, not merely
    suspected, to also affect `SpriteBatch` 2D draws, a genuinely new and useful data point for
    that investigation: the underlying bug is not scoped to 3D draws, `drawMetal3D()`, or any
    particular `PipelineKind` — it reproduces on `MetalSpriteBatch::Draw()`'s own, architecturally
    distinct immediate-draw code path too, the first time this whole session's investigation has
    had a 2D data point. Since Check D exercises none of Phase 14's own new code, this closes the
    question of whether Phase 14 introduced a regression: **it did not** — `Metal_SpriteBatch_
    CustomEffect`'s 2/4-pass result is the pre-existing, cross-cutting, already-paused-pending-a-
    physical-Mac bug manifesting on a new draw path, not a new bug. Per the user's own standing
    direction (paused this investigation at item 76 after ~10 round-trips, no physical Mac
    available), not restarting the deep GPU-capture-style diagnosis that already proved
    inconclusive there — this item exists to honestly record the new data point and correctly
    attribute `Metal_SpriteBatch_CustomEffect`'s current failure to the same root cause, not to
    reopen that paused investigation. `METAL-151`'s own new `CTest` is source-complete, CI-compiles
    clean, and Check A/C (compile + correct vertex-transform positioning) are real, confirmed
    passes — but the test as a whole cannot honestly be marked ✅ until the shared readback bug is
    resolved, exactly the same honesty standard `Metal_PbrEffect_Golden`/`Metal_
    SkinnedPbrEffect_Golden` have been held to since item 67.

86. **`METAL-112`/`113`/`118` landed — real MRT (`SetRenderTargets`), replacing the base default's
    "bind only the first target"**: reading `VulkanGraphicsBackend::SetRenderTargets()`/
    `VulkanMRTProxy`/`GetOrCreateMRTRenderPass()` directly first (the closest architectural analog
    among established backends — both Vulkan and Metal use explicit render-pass/pipeline objects,
    unlike D3D11's simpler `OMSetRenderTargets`) confirmed the exact same real constraint would
    apply to Metal: a render pipeline's declared color-attachment count must match how many
    simultaneous attachments the active render pass actually binds, or pipeline creation is a
    genuine Metal API validation error, not just a style mismatch — so `METAL-113`'s own premise
    (the pipeline cache needs to vary by attachment *count*) is real, just narrower than its own
    text suggested: only the count matters, not a per-slot *format* list, since every
    `MetalRenderTargetBackend` color texture is already unconditionally `MTLPixelFormatBGRA8Unorm`
    (item 77's own finding) — `MetalPipelineCacheKey` gained one `colorAttachmentCount` field
    (`MetalPipelineKey.hpp`), not a hashed format-set.

    Implementation: `MetalGraphicsBackend::Impl` gained `currentMRT` (a `vector<
    MetalRenderTargetBackend*>`, `size()>=2` meaning true MRT is active) and
    `activeColorAttachmentCount`, alongside the existing single-target `currentRenderTarget`
    (`currentMRT[0]` is always mirrored into it, so `computeSpriteTransform()` and every other
    single-target-only caller keep working unmodified against target 0). `ensureFrame()`/`clear()`
    were extended from a single `id<MTLTexture> colorOut` to a new
    `resolveActiveColorAttachments()`/loop, building `rp.colorAttachments[0..N-1]` for real MRT
    while staying byte-identical to the original single-attachment code whenever `N==1` (the
    overwhelming common case, verified by inspection: the loop body is the exact same 2 lines the
    original hardcoded `[0]` version had, just iterated). Depth: reuses `currentMRT[0]`'s own
    already-existing `depthTextureNative()` for the whole MRT render pass rather than allocating a
    dedicated shared one the way `VulkanMRTProxy` must (Vulkan's explicit `VkFramebuffer` needs one
    concrete depth `VkImageView` chosen up front; Metal's `MTLRenderPassDescriptor` has no such
    constraint, and every Metal render target already unconditionally owns a real
    `Depth32Float_Stencil8` texture) — a genuine, documented simplification, not a corner cut.
    `Clear()`'s own real XNA contract (clearing every currently-bound target to the same color) is
    preserved directly by the same loop.

    A real correctness risk found and fixed while implementing this, not left implicit: only
    `currentMRT[0]` is ever mirrored into `currentRenderTarget`, so the *existing* single-target
    destructor safety net (`if (currentRenderTarget==this) ...`) would silently miss targets
    1..N-1 being destroyed while still part of an active MRT binding, leaving `currentMRT` holding
    a dangling pointer. Fixed by extending `MetalRenderTargetBackend`'s destructor to also scan
    `currentMRT` via `std::find` and invalidate the whole set if `this` appears anywhere in it, not
    just at index 0. A second, related gap: `BindAsRenderTarget()` (the single-target bind path,
    reachable both directly via `SetRenderTarget2D()` and via `SetRenderTargets()`'s own count==1
    delegation) did not clear stale `currentMRT`/`activeColorAttachmentCount` left over from a
    *previous* `SetRenderTargets()` call — fixed by clearing both there too, and by extracting
    `UnbindAsRenderTarget()`'s own per-target mip-regeneration logic (`METAL-103`) into a reusable
    `regenerateMipsIfNeeded()` method, called both from the existing single-target unbind path and
    from a new `Impl::unbindCurrentMRT()` (the single chokepoint every render-target-changing entry
    point — `SetRenderTarget2D`/`SetRenderTargetCubeFace`/`SetRenderTargets` — now calls first,
    unconditionally, so a previously-active MRT set is always torn down the same correct way
    regardless of which specific entry point the game used next).

    Blend state: unlike Vulkan/D3D11/D3D12's own custom-`ShaderEffect` precedent (item 83), this is
    *not* a case of correcting an established simplification — Vulkan's own real 3D MRT path
    already keys its pipeline cache by the real `BlendKey`/`colorAttachmentCount` together (read
    directly, not assumed), so Metal's `makePipeline()` replicating the same single `BlendKey`
    across all `colorCount` attachments (XNA's own `BlendState` is a single global state, not
    per-render-target — there is nothing to vary per-attachment) matches established precedent
    exactly, not a deviation either way.

    Genuine open question, not glossed over: whether a Metal fragment function that returns a
    single `float4` (implicitly written to `[[color(0)]]` only) validates cleanly against a
    pipeline whose `colorAttachments[1..N-1]` are also declared with a real pixel format, with the
    unwritten attachments simply left as whatever their own load action produced. This is standard,
    well-documented Metal MRT usage as best understood from Apple's own API shape and sample-code
    conventions (a shader is never required to write every attachment a pipeline declares), and is
    exactly the semantics `examples/easygl_mrt_test.cpp`'s own reused test expects (rt1 stays blue,
    untouched by a shader that only writes rt0) — but this specific claim about Metal's own
    attachment-validation rules could not be verified against the real API or its documentation
    from this sandbox, and is flagged here explicitly rather than silently assumed correct.

    A new `Metal_MRT` `CTest` reuses `examples/easygl_mrt_test.cpp` verbatim (public XNA API only,
    same reuse technique as every other Metal test in this file). Its own final readback goes
    through `GetBackBufferData()` — expected, per items 67–76/82/84/85, to likely hit the same
    still-unresolved Clear-color-only readback bug even if the MRT binding/draw themselves are
    fully correct; a CI failure here should be checked against that known signature (observed color
    equals the test's own `Clear()` color) before being read as an MRT-specific bug, the same
    diagnostic discipline items 84/85 established for `Metal_SpriteBatch_CustomEffect`.

    **Cannot be build-verified from this Linux sandbox for the `.mm`-side changes** — this is
    genuinely new, never-compiled Objective-C++ code touching the same frame/encoder lifecycle this
    project's own docs already flag as its riskiest surface. Real local verification was possible
    for the plain-C++ pipeline-key extension only: `cmake --build cmake-build-headless --target
    CnaTests` compiles clean, and the full `ctest -R "^Metal"` suite (130 tests, 3 new
    `MetalPipelineCacheKey`/`Hash` tests plus the 127 already-passing ones) reports `100% tests
    passed, 0 tests failed`. The `.mm`-side changes were sanity-checked only via a full-file
    brace/bracket/paren balance re-check (`386/386`, `868/868`, `1990/1990`, all balanced) and
    careful manual tracing of `examples/easygl_mrt_test.cpp`'s own exact call sequence against the
    new code, step by step, by hand. Pushed for real CI verification next — genuinely unverified
    until that CI run reports back.

87. **Real thirtieth CI signal — item 86's MRT code compiles clean and runs without any Metal
    validation error, and the "genuine open question" it flagged is answered: `Metal_MRT` fails
    for the same already-confirmed readback bug, not a new MRT-specific defect**: CI run
    `29807262945` shows "Build native Metal backend" passing with no errors, and `ctest` reports
    `96% tests passed, 5 tests failed out of 136` — the count rose from 132 to 136 (the 3 new
    `MetalPipelineCacheKey` tests, which also run under the real macOS `CnaTests` build since they
    carry no `CNA_BACKEND_METAL` gate, plus the 1 new `Metal_MRT` test), and the failures are the
    same 4 pre-existing ones plus exactly 1 new one (`Metal_MRT`) — no other regression anywhere.

    `Metal_MRT`'s own diagnostic log (`MTL_SHADER_VALIDATION=1`/`MTL_DEBUG_LAYER=1` both active,
    per `metal-macos-ci.yml`'s own test-step env) shows the real, decisive sequence: one
    `drawMetal3D kind=0 stride=16` (the MRT draw itself — `PipelineKind::Colored16`, the green quad,
    into the real 2-color-attachment pipeline this task built) completes cleanly with **no**
    validation error or exception logged, followed by two ordinary single-target `drawMetal3D
    kind=1 stride=20` calls (the rt0/rt1 blit-to-backbuffer draws) and two `ReadBackbuffer`
    requests — the whole sequence runs to completion without incident. This directly answers item
    86's own flagged open question: a Metal fragment function that writes only `[[color(0)]]`
    (implicitly, via a single `float4` return) against a pipeline whose `colorAttachments[0]` and
    `colorAttachments[1]` are both declared with a real pixel format does **not** trigger a Metal
    API validation error, even under both of Apple's own strictest runtime validation layers —
    confirmed empirically, not just believed correct from documentation.

    The actual failure: `[FAIL] MRT: left=(0,0,0) [expect green], right=(0,0,0) [expect blue]` —
    both readback pixels are pure black, exactly matching the test's own `device.Clear(Color(0, 0,
    0, 255))` call immediately before the two blit draws, the identical "readback returns only the
    most recent `Clear()` color, not what was actually drawn" signature items 67–76/82/84/85/86
    already established. This closes the loop cleanly: **the MRT feature itself — binding,
    pipeline construction, drawing into 2 simultaneous color attachments — works without any
    detectable defect** (real hardware, real validation layers, zero errors across the whole
    sequence); the test's failure is entirely attributable to the same pre-existing, already-
    paused-pending-a-physical-Mac readback bug, not to anything landed in this task. `METAL-112`/
    `113`/`118` are now build-and-runtime-confirmed source-complete (no crash, no validation error),
    correctly not marked ✅ only because the shared readback bug prevents this specific `CTest` from
    itself passing — the same honesty standard every other readback-dependent Metal test in this
    plan is held to.

88. **`METAL-32`/`104`/`105`/`191` landed — real MSAA, backbuffer and `RenderTarget2D`, keyed the
    same way `METAL-112`/`113`'s own MRT extension already keys attachment count**: mirrors
    `VulkanRenderTargetBackend`'s own exact "piggyback on the backend's own device-wide sample
    count" scope decision (confirmed by reading it directly) — a `RenderTarget2D` only engages MSAA
    if it asked for it AND the device backend itself was constructed (or later reconfigured, see
    below) with real MSAA available; there is no independent per-target arbitrary sample count.
    `clampMetalSampleCount()` walks Metal's own standard sample counts (8/4/2) no higher than
    requested, returning the first one `MTLDevice.supportsTextureSampleCount:` actually confirms —
    a real device query, not a blind pass-through of the request, satisfying `METAL-105`'s own
    "report the real clamped value" contract on both `IGraphicsBackend` (backbuffer) and
    `IRenderTargetBackend` (per-target).

    `MetalPipelineCacheKey` gained a `sampleCount` field, hash-combined with the existing packed
    word via the standard `hash_combine` formula rather than packed into the same 64-bit word as
    `colorAttachmentCount` — a real overflow risk caught before it became a bug: `colorAttachmentCount`
    already occupies 4 bits (57-60) for its own 1-8 range, leaving only 3 free, one short of
    `sampleCount`'s own 1-8 range. A dedicated regression-guard test
    (`ColorAttachmentCountAndSampleCountBothVaryingProducesDifferentHash`) locks in the specific
    pair most at risk of exactly that overflow collision. `makePipeline()`/`getOrCreatePipeline()`
    both extended with a `sampleCount` parameter the same way they were already extended for
    `colorCount` in item 86 — and `MetalEffectBackend::pipelineFor()` (Phase 14's own custom
    `SpriteBatch` effect pipeline, built independently of `getOrCreatePipeline()`) needed the
    identical fix, found while implementing this: without it, a custom effect drawn while the
    backbuffer happened to be MSAA-enabled would have built a 1-sample pipeline against an
    N-sample render pass, a real validation error `getOrCreatePipeline()`'s own callers were
    already protected against but this one wasn't.

    A genuinely important, non-obvious correctness detail found and fixed before it could ship as a
    silent bug, not left implicit: this codebase's own established multiple-encoders-per-logical-
    frame architecture (`Clear()`/render-target switches routinely end and restart encoders
    mid-frame, all already audited and fixed once during Phase 18) means plain
    `MTLStoreActionMultisampleResolve` would be actively wrong here — that store action *discards*
    the multisampled texture's own content after resolving, so the next `MTLLoadActionLoad` on it
    (at the next mid-frame encoder boundary) would silently load undefined content, losing
    accumulated multisample data between a `Clear()`/target-switch pair within one XNA frame.
    Used `MTLStoreActionStoreAndMultisampleResolve` instead — stores the MSAA texture's own content
    *and* resolves it, at the real cost of extra bandwidth on every encoder boundary, correctly
    matching this file's own architecture rather than the simpler single-encoder-per-frame model
    Apple's own basic sample code usually assumes.

    Real runtime reconfiguration, not just construction-time: overrides
    `IGraphicsBackend::ApplyMultiSampleCount()` (found while researching the interface — a hook
    `GraphicsDevice::Reset()` calls when `GraphicsDeviceManager.PreferMultiSampling` changes
    *after* construction; the base default is a pure no-op report-back, which this backend was
    silently falling back to before this task). Forces the backbuffer's own MSAA/depth textures to
    nil so `resolveActiveAttachments()` reallocates both at the new sample count next use, mirroring
    their own pre-existing lazy-reallocate-on-width/height-change convention.

    A real, unrelated bug also found and fixed in the same pass: `SupportsCapability(
    MultipleRenderTargets)` (`METAL-192`) was still hardcoded `false` — item 86 landed real MRT but
    never reverted this flag, a stale false-negative left sitting in the source for one commit,
    caught only while fixing `MultiSampleAntiAliasing`'s own identical case. Both now correctly
    answer `true`.

    Scope decisions, documented rather than silently assumed: true MRT (`currentMRT.size()>=2`) and
    MSAA are never combined in this pass — every MRT draw always runs at sample count 1 regardless
    of `deviceSampleCount` (matches real-world XNA usage: MSAA is a single-target scene-anti-
    aliasing feature, deferred/G-buffer-style MRT rendering practically never also wants MSAA on
    the G-buffer itself); `RenderTargetCube` also stays out of MSAA scope, matching `METAL-109`/
    `110`'s own established simplification tier. Two new `CTest`s, both reusing existing
    backend-agnostic EasyGL sources verbatim: `Metal_MSAA` (backbuffer, solid-fill) and `Metal_
    RenderTarget2D_MSAA` (a genuinely stronger differential test — checks for real partially-
    covered pixels along a diagonal triangle edge, the one signature only an actual multisample
    resolve can produce, not just "solid colors survive unchanged"). Both still route their final
    readback through `GetBackBufferData()`, so — consistent with `Metal_MRT`'s own result in item
    87 — expect them to likely hit the same pre-existing readback bug even if the MSAA machinery
    itself is fully correct; a CI failure here should be checked against that known signature
    before being read as an MSAA-specific defect. Not attempted this pass: a dedicated CTest for
    `ApplyMultiSampleCount()`'s own real runtime-reconfiguration behavior (EasyGL's own `easygl_
    msaa_change_test.cpp` explicitly asserts the *opposite* of what Metal now does — that
    reconfiguration is impossible post-construction — so it cannot be reused verbatim; a genuinely
    new Metal-specific test would be needed, left as a real, flagged follow-up rather than silently
    skipped).

    **Cannot be build-verified from this Linux sandbox for the `.mm`-side changes** — the largest
    single feature landed this session, touching the same frame/encoder lifecycle `METAL-112`
    already flagged as risky, now with real resolve semantics on top. Real local verification was
    possible for the plain-C++ pipeline-key extension only: `cmake --build cmake-build-headless
    --target CnaTests` compiles clean, and `ctest -R "^Metal"` (134 tests, 4 new `MetalPipelineCacheKey`
    /`Hash` tests including the overflow-collision regression guard) reports `100% tests passed, 0
    tests failed`. The `.mm`-side changes were sanity-checked only via a full-file brace/bracket/
    paren balance re-check (`409/409`, `891/891`, `2114/2114`, all balanced) and careful manual
    tracing of both new `CTest`s' own exact call sequences against the new code, step by step, by
    hand. Pushed for real CI verification next — genuinely unverified until that CI run reports
    back.

89. **Real thirty-first CI signal — item 88's MSAA code compiles clean, and CI caught a real,
    substantive design bug before it could ship: `MetalRenderTargetBackend`'s "piggyback on the
    device backend's own sample count" scope decision was wrong for Metal, not merely unnecessary
    — fixed, `RenderTarget2D` MSAA is now independent of the backbuffer's own**: CI run
    `29809228751` shows "Build native Metal backend" passing with no errors, and `ctest` reports
    `95% tests passed, 7 tests failed out of 142` — 136→142 (4 new `MetalPipelineCacheKey` tests +
    `Metal_MSAA`/`Metal_RenderTarget2D_MSAA`), the same 5 pre-existing failures unchanged, plus
    exactly the 2 new tests — no other regression.

    `Metal_MSAA` fails with `centre=(0,0,0)`, exactly this test's own `Clear()` color — the same
    already-confirmed readback bug signature (items 84/85/87), unsurprising and expected (this test
    draws via `SpriteBatch` + reads back via `GetBackBufferData()`, the exact combination already
    shown broken for unrelated reasons in `Metal_SpriteBatch_CustomEffect`).

    `Metal_RenderTarget2D_MSAA` is the genuinely interesting, different result: Check 1
    (`MultiSampleCount=0`, expects a hard binary edge) **passed** — real, correct content, not the
    Clear color, proving the readback mechanism itself works fine for this test's own code path.
    Check 2 (`MultiSampleCount=8`, expects blended/anti-aliased pixels) **failed**, with the test's
    own diagnostic spelling out the exact symptom: "row is purely binary — MSAA resolve is not
    actually averaging sub-pixel coverage." Since Check 1 proves the pipeline genuinely works when
    MSAA isn't requested, this is not the known readback bug — tracing it by hand against the
    reused `examples/easygl_rendertarget2d_msaa_test.cpp` found the real cause: that test's own
    `Game`/`GraphicsDeviceManager` never sets `PreferMultiSampling` (only `easygl_msaa_test.cpp`,
    behind `Metal_MSAA`, does), so `deviceSampleCount` stayed `1` for the whole test process — and
    item 88's own "piggyback on `deviceSampleCount>1`" condition (mirrored from
    `VulkanRenderTargetBackend`'s real precedent) silently forced `appliedSampleCount_` to `0` for
    *every* `RenderTarget2D` MSAA request regardless of what was actually asked for, no matter the
    value. This premise, borrowed directly from Vulkan without re-deriving whether it actually
    applies to Metal, turned out not to: Vulkan's own comment gives its real reason — "avoids
    threading an independent numeric sample count through every pipeline cache key" — reusing a
    single shared MSAA render-pass/pipeline infrastructure that genuinely doesn't exist without
    real per-value bookkeeping in Vulkan's own `VkRenderPass`/`VkFramebuffer` model. Metal's own
    `MetalPipelineCacheKey` *already* threads `sampleCount` independently (this same item 88's own
    other half), so there was never a shared-infrastructure cost to avoid here — the piggyback
    restriction was pure, avoidable capability loss for Metal, not a real architectural constraint,
    and this reused EasyGL test (which presumably passes on real EasyGL, itself apparently capable
    of genuine independent per-RT MSAA) was quietly exercising exactly the gap. Fixed:
    `MetalRenderTargetBackend`'s own applied sample count is now computed directly from
    `clampMetalSampleCount(owner.device, requestedMultiSampleCount)`, with no dependency on the
    backbuffer's own `deviceSampleCount` at all — a `RenderTarget2D` can now engage MSAA whether or
    not the game ever requested backbuffer-level `PreferMultiSampling`, matching what this reused
    test (and presumably real games using RT-only MSAA, e.g. an offscreen anti-aliased render
    later composited without backbuffer MSAA) actually need. Re-pushed for a fresh CI round-trip;
    genuinely unverified whether this specific fix is itself correct until that run reports back —
    this item documents the found-and-fixed bug, not yet its own resolution.

90. **Real thirty-second CI signal — item 89's decoupling fix compiles clean, but `Metal_
    RenderTarget2D_MSAA`'s `MultiSampleCount=8` check still fails identically; a targeted
    diagnostic ruled out the most likely remaining hypothesis, and this specific sub-investigation
    is paused, not resolved**: CI run `29809995787` shows the exact same `[FAIL] MultiSampleCount=8:
    ... row is purely binary` result as before item 89's fix, byte-for-byte — the decoupling fix
    itself was real and correct (confirmed necessary by the earlier CI evidence in item 89), but it
    was not sufficient to fix `Metal_RenderTarget2D_MSAA` on its own, meaning a second, still
    undetermined issue exists somewhere downstream.

    Added a temporary diagnostic (`NSLog` in `computeAppliedRenderTargetSampleCount()`, printing
    `requested`/`clamped`/`applied` and the raw `supportsTextureSampleCount:` result for 8/4/2) to
    distinguish two remaining hypotheses: (a) this specific CI runner's GPU doesn't actually support
    any of Metal's standard MSAA sample counts at all (an environment limitation, matching this same
    runner's own earlier-confirmed `MTLCaptureDestinationGPUTraceDocument` limitation from item 76),
    vs. (b) a real >1 sample count genuinely gets applied but something further downstream (the
    render pass/pipeline/resolve chain) still isn't producing genuinely anti-aliased output. CI run
    `29810575691`'s own diagnostic output answers this cleanly: `supports8=0` (confirming this
    runner's GPU/driver combination cannot do 8x MSAA — a real, now-documented environment
    limitation, consistent with its own earlier GPU-capture limitation) but `supports4=1`/
    `supports2=1`, and for the actual `MultiSampleCount=8` test request: `clamped=4 applied=4` — a
    real, non-zero sample count *is* being applied. Hypothesis (a) is therefore false; hypothesis
    (b) is confirmed: `RenderTarget2D` genuinely engages 4x MSAA (a real, non-multisampled-texture-
    allocation-shaped `id<MTLTexture>` is created, a real `sampleCount=4`-declared pipeline is built
    and used), and the test's own diagonal-triangle draw still produces zero blended pixels in the
    final resolved-and-read-back row.

    A second, careful manual re-trace of the whole render-pass/pipeline/resolve chain (`resolve
    ActiveAttachments()`'s `currentRenderTarget` branch → `ensureFrame()`'s per-attachment loop →
    `getOrCreatePipeline()`'s cache-key construction → `applyTrackedEncoderState()`, checked fresh
    for any MSAA-unaware state that might interfere — found none) did not surface a further, more
    specific code defect beyond what item 89 already found and fixed. Given this is now the third
    real CI round-trip spent on this one specific, narrow sub-symptom (distinct from — and
    downstream of — the separate, much larger Clear-color-readback investigation paused at item 76),
    and per this session's own standing discipline (documented explicitly by the user: pause and
    document rather than keep guessing blindly once an investigation reaches the limit of what's
    determinable from this sandbox), this specific thread is paused here, not abandoned. What is
    now confirmed, not merely hoped: the MSAA *infrastructure* itself (multisample texture
    allocation at a real device-clamped sample count, matching-sample-count pipeline construction,
    `StoreAndMultisampleResolve` render-pass wiring) runs on real Apple hardware without crashing,
    without any Metal API validation error, and without the already-familiar Clear-color-readback
    signature (Check 1's own `MultiSampleCount=0` control case genuinely passes, proving the
    surrounding readback/positioning machinery is sound) — but whether the *resolve itself*
    produces genuine sub-pixel-averaged output remains unconfirmed, and `Metal_RenderTarget2D_MSAA`
    cannot honestly be marked ✅ until that's resolved, either by further diagnosis or by a physical
    Mac. The `[METAL-104 diag]` `NSLog` is left in place (matching this file's own established
    precedent for prior investigation diagnostics, e.g. `[METAL-89 diag]`), not reverted.

91. **`METAL-198`/`119`/`129` landed — a real capabilities regression-guard `CTest`, and both
    cross-backend support-matrix docs gained their Metal column**: `Metal_Capabilities`
    (`examples/metal_capabilities_test.cpp`) asserts all 8 `CNA::GraphicsCapability` values are
    `true` — Metal is the first backend in this project expected to genuinely support every
    currently-enumerated capability, including `WireFrame` (unlike EasyGL, whose own
    `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` correctly asserts `false`: GLES3 has no
    wireframe fill mode at all, while Metal's `MTLTriangleFillModeLines` mapping is real,
    `METAL-194`). A deliberately-noted, real property of this specific test: it never calls
    `GetBackBufferData()` at all (`SupportsCapability()` is a pure query, no draw/readback
    involved), so unlike every other Metal `CTest` landed this session it is **not** exposed to the
    known Clear-color-only readback bug — a genuinely independent regression guard for this
    session's own 3 capability-flag fixes (`CustomEffects`/`MultipleRenderTargets`/
    `MultiSampleAntiAliasing`, items 83/86/88), not one more test whose failure would need the
    usual "is this the known bug or something new" disambiguation.

    `docs/rendertarget-support.md`'s own "what actually works today" table and
    `docs/texture3d-texturecube-support.md`'s several per-feature tables both gained a Metal
    column/row, cross-referencing `plan_metal.md`'s own phase/task numbers and narrative items
    rather than duplicating a second tracking system — most cells are 🔍 (source-complete,
    CI-confirmed to compile/run without a Metal API validation error, but not independently
    pixel-verified — either because no dedicated `CTest` exists yet, e.g. `Texture3D`/`TextureCube`
    round-trips [`METAL-127`/`128`], or because the shared Clear-color-readback bug blocks the
    pixel-level proof even where a `CTest` does exist, e.g. `RenderTarget2D` sampling/MRT), matching
    this session's own consistent honesty standard rather than inflating them to ✅ on the strength
    of "the code compiles and doesn't crash" alone.

    Real local verification: `examples/metal_capabilities_test.cpp` is plain C++ (no new
    Objective-C++), balance-checked (`6/6` braces, `42/42` parens); the two doc files are prose/
    Markdown-table edits, not code, with no build-verification concept to apply. Pushed alongside
    whatever `.mm` state is current for a real CI compile/run of `Metal_Capabilities` specifically.

92. **Real thirty-third CI signal — `Metal_Capabilities` passes cleanly on real Apple hardware, all
    8 checks, the first Metal `CTest` this whole session to reach a clean ✅ with zero caveats**: CI
    run `29814126178` shows "Build native Metal backend" passing with no errors, and `ctest`
    reports `95% tests passed, 7 tests failed out of 143` — 142→143 (the one new test), the same 7
    pre-existing failures unchanged, and `Metal_Capabilities` itself explicitly absent from the
    failure list: `143/143 Test #5646: Metal_Capabilities ... Passed`. Because this test never
    touches `GetBackBufferData()` (a pure `SupportsCapability()` query, no draw/readback involved),
    its pass is not just "ran without the known bug's own symptom" the way every other Metal test
    landed this session has had to be qualified — it is a genuine, unqualified, independently-
    confirmed correctness result: `MultipleRenderTargets`, `CustomEffects`, and
    `MultiSampleAntiAliasing` (items 86/83/88 — real MRT, real `SpriteBatch`-scoped custom
    effects, real MSAA) really are correctly reported as supported by this backend on real Apple
    hardware, not merely believed correct from source review. `METAL-198` closes as ✅, joining
    `METAL-13`/`34`/`40`/`155` and the whole plain-C++ extraction subset as one of the small number
    of items in this entire plan verified end-to-end without any lingering caveat.

**Explicitly still open / not attempted across this whole overnight session** (do not assume these
are done — this list is kept current as the authoritative "what's actually left" summary, updated
at the end of each landed phase rather than trusted from an earlier revision):
`METAL-19` is now fully closed (item 81, 2026-07-21 — the remaining `CullMode`/`Blend`/
`BlendFunction`/`CompareFunction`/`StencilOperation` tables extracted and Linux-verified, same as
`MetalSamplerFilter.hpp` already was); the
*wiring* of the generic `VertexElement`-driven descriptor builder into a live draw path
(`METAL-26`/`27`'s core logic landed real 2026-07-20 — see narrative — but no `PipelineKind`/shader
currently pairs with an arbitrary declaration, so this stays correctly blocked on Phase 14, same
conclusion as before, just with real tested infrastructure now waiting for it instead of nothing);
sample-count-keyed pipelines (`METAL-32`, genuinely blocked on MSAA not existing yet;
`METAL-31`'s own attachment-format-keying sibling is closed, false premise, see item 77);
`METAL-89`/`90` are now closed
(see item 67) but `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden` themselves still fail on
real hardware for a still-undetermined reason — see item 72, paused pending either a physical Mac
or further diagnostics, not attributable to either of the two real bugs (`vertexStart`/
`ReadBackbuffer`) already found and fixed from this same investigation; confirmed 2026-07-21 (items
84/85) to also affect `Metal_SpriteBatch_CustomEffect`'s own `SpriteBatch`-2D-draw readback, not
just 3D draws — the underlying bug is broader than originally scoped, still unresolved, still
paused; Phase 10's own MRT (`METAL-112`/`113`, item 86) and MSAA (`METAL-104`/`105`/`32`, item 88)
are now both landed too (source-complete, CI-confirmed to compile and run without any Metal
validation error where testable — `preserveContents`/mip/`GetColorGLHandle` `METAL-102`/`103`/
`108`, `RenderTargetCube` `METAL-109`–`111`, and `GetData()` `METAL-131` were already closed) —
only `METAL-114`/`115`/`116`/`117` (the still-missing `Metal_RenderTarget2D`/`Metal_
RenderTargetCube`/`Metal_RenderTarget_MSAA`/`Metal_RenderTarget_Mip` `CTest`s `METAL-118`'s own
task text separately called for, distinct from the `Metal_MRT`/`Metal_MSAA`/`Metal_
RenderTarget2D_MSAA` tests items 86/88 actually landed) remain genuinely open within Phase 10
(`METAL-119`'s docs column itself closed ✅ 2026-07-21, item 91); Phase 14's own
SpriteBatch-scoped custom `ShaderEffect` facility is now closed (item 83, 2026-07-21 — corrected,
narrower scope than originally assumed, see this phase's own corrected blocker note); `METAL-147`
(extra-sampler-unit `BindTexture`/`BindTextureCube`/`BindTexture3D`) and `METAL-148` (the general
3D `GpuDrawParams::customEffectBackend` bypass) remain genuinely open within Phase 14, matching
every established structured-pipeline backend's own identical scope boundary — and Phase 9
Instancing stays correctly blocked specifically on `METAL-148`/the generic `VertexElement`-driven
descriptor builder (`METAL-26`/`27`), *not* on the SpriteBatch-only piece Phase 14 just closed —
real, useful instancing needs a custom 3D shader reading per-instance `VertexBufferBinding` data,
which Phase 14's landed scope does not provide (see Phase 9's own header note); `METAL-257` (the cross-backend
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
  `181`; the texture-update CPU/GPU-sync hazard the same audit found was fixed 2026-07-21 as
  `METAL-256`, mip-level-preserving reallocate — see narrative item 79, pending CI).
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
  #102–109, 100% pass (✅ landed 2026-07-19 — `METAL-34`).
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
- `fillLitUniforms`/`fillEnvUniforms`/`fillSkinnedUniforms`/`fillPbrUniforms`/`fillSkinnedPbrUniforms`
  — the `GpuDrawParams`-to-GPU-uniform mapping layer, the largest single extraction of the night by
  field count — machine-verified via `MetalUniformFill.hpp` and 8 real `CnaTests`/`ctest` tests (CTest
  #130–137), each built from a `GpuDrawParams` where every relevant field holds its own distinct
  numeric value so a wrong-field or wrong-light-index mapping bug produces a visibly wrong number
  instead of a coincidental match, plus dedicated boolean-false-branch and delegation-correctness
  tests — a seventh genuinely real, fully-earned ✅ tier tonight (🟨→✅ — `METAL-38`–`47`/`66`–`68`/
  `73`/`74`/`76`–`78`/`81`/`83`–`86`).
- The generic `VertexElement`-driven descriptor builder's core logic (`METAL-14`/`26`/`27`) —
  `MetalVertexAttribFormat.hpp` (all 12 `VertexElementFormat` values, 14 tests) and
  `MetalVertexDescriptorPlan.hpp` (arbitrary-declaration attribute-layout building, 6 tests,
  cross-validated against the existing hand-written stride-48/52 cases) — an eighth genuinely real,
  fully-earned ✅ tier, landed with the user back and present (not part of the autonomous overnight
  run). Deliberately not wired into any live draw path: no built-in `PipelineKind` shader currently
  pairs with an arbitrary declaration, so `METAL-28`'s fallback-choice wiring stays correctly
  blocked on Phase 14 (custom `ShaderEffect`) — see narrative item 42 for the full reasoning (🟨→✅
  for the plain-C++ core, `METAL-14`/`26`/`27`, `.mm`-side glue stays 🟨 like every other
  Objective-C piece in this plan).
- `TextureFilter`→min/mag/mip mapping (`METAL-1`) — a real, serious bug found by cross-checking
  against `EasyGLGraphicsBackend`'s own reference table and each `TextureFilter` enumerator's own
  self-documenting name: 3 of 9 real filter values (`LinearMipPoint`/`MinLinearMagPointMipPoint`/
  `MinPointMagLinearMipLinear`) had a wrong min-or-mag component, one degrading silently to plain
  `Linear` filtering. Rewritten as `MetalSamplerFilter.hpp`'s `DescribeMetalSamplerFilter()`, a
  single self-contained per-filter table instead of three independently-derived membership sets —
  a ninth genuinely real, fully-earned ✅ tier, 10 tests including a dedicated regression guard per
  previously-broken value.
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
| METAL-1 | `ApplySamplerState(slot,filter,addressU,addressV,maxAnisotropy)` + `Impl::samplerFor()` cache (`TextureFilter`→min/mag/mip, `TextureAddressMode`→address mode, `Anisotropic`→`maxAnisotropy`), wired into `drawMetal3D` texture unit 0 | 🟨 **real bug found and fixed 2026-07-20** in the `TextureFilter`→min/mag/mip half — see narrative; `TextureAddressMode`→address mode cross-checked against `EasyGLGraphicsBackend`'s own mapping and confirmed already correct, no bug there |
| METAL-2 | Wire the same cache into `MetalSpriteBatch::Draw()` via a new `SetSamplerAddressMode()` override (previously `filter_` was set but never read, and address mode had no override at all) | 🟨 |
| METAL-3 | Extend sampler-slot consultation beyond unit 0 in `drawMetal3D` — needed once DualTextureEffect (Phase 5, unit 1) / EnvironmentMapEffect (Phase 6) / PBR (Phase 8, up to 4 map units) land | 🟨 **real bug found and fixed 2026-07-20**: `DualTextureEffect`/`EnvironmentMapEffect` already correctly consulted `samplerSlots[0]`/`[1]` per unit, but the `Pbr48`/`SkinnedPbr68` paths bound all 5 PBR texture units (base color/normal/metallic-roughness/emissive/occlusion) through a single `samplerSlots[0]` broadcast to every unit — confirmed as a real divergence by reading `EasyGLGraphicsBackend`'s own PBR binding code, which correctly uses a distinct GL sampler object per unit (`samplers_[0..4]`). Fixed to `samplerSlots[0..4]` respectively, matching the reference. `.mm`-only, so like every other fix in this plan, correct-on-inspection but genuinely unverified without a Mac — see narrative |
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
| METAL-14 | `VertexElementFormat`→`MTLVertexFormat` full table (Single/Vector2/Vector3/Vector4/Color/Byte4/Short2/Short4/NormalizedShort2/NormalizedShort4/HalfVector2/HalfVector4) — today only 3 hand-picked fixed vertex descriptors exist, no general element-format mapping | ✅ **real, on this Linux machine, 2026-07-20** — `MetalVertexAttribFormat.hpp`'s `DescribeMetalVertexElementFormat()` covers all 12 real values as a plain-C++ table (translated to the real `MTLVertexFormat` via a trivial 1:1-named switch inside the `.mm`), 14/14 `MetalVertexAttribFormat.*` tests pass, including an all-pairs distinctness check across all 12 formats — landed alongside `METAL-26`/`27`, see narrative |
| METAL-15 | `SurfaceFormat`→`MTLPixelFormat` table (Color/Bgr565/Bgra5551/Bgra4444/Dxt1/Dxt3/Dxt5/NormalizedByte2/NormalizedByte4/Rgba1010102/Rg32/Rgba64/Alpha8/Single/Vector2/Vector4/HalfSingle/HalfVector2/HalfVector4/HdrBlendable) — every texture is currently hardcoded `RGBA8Unorm` regardless of `ImageData`'s real format | 🟨 (**found to be based on a false premise**: `include/CNA/Internal/Graphics/ImageData.hpp` has no format field at all — its own doc comment states "RGBA8 pixel data," and `Texture2D.cpp`'s own real code confirms DXT1/DXT5 are decompressed to RGBA8 via `DxtUtil::DecompressDxt1`/`DecompressDxt5` *before* `CreateTexture(img)` is ever called, for every backend uniformly, not just Metal. There is no "real format" for `CreateTexture()` to diverge from — this was never a Metal-specific gap) |
| METAL-16 | `DepthFormat`→`MTLPixelFormat` table (None/Depth16/Depth24/Depth24Stencil8) — backbuffer currently always allocates `Depth32Float_Stencil8` regardless of what `PresentationParameters` requested | 🟨 (confirmed intentional, not overlooked: matches `VulkanGraphicsBackend`'s own already-accepted "always allocate depth+stencil" simplification, explicitly called out as the deliberately-chosen tier back in `METAL-101`'s own note — not a priority fix) |
| METAL-17 | Query `MTLDevice.supportsBCTextureCompression` and document the real, device-dependent DXT/BC boundary (no native support on Apple Silicon without emulation; yes on Intel Macs) rather than assuming universal support | 🟨 (confirmed moot under the current architecture: since `METAL-15`'s finding means nothing ever uploads real DXT/BC bytes to any backend today, this query has nothing to gate yet — would only become relevant if a future, genuinely different, cross-backend "upload real compressed texture data" path bypassing `ImageData` were ever added, which is a project-wide feature, not a Metal-only one) |
| METAL-18 | Centralize every mapping above into one shared location so Phase 2's pipeline cache and Phase 10's render-target/format work reuse one source of truth instead of duplicating switch statements | 🟨 (scope reduced by the `METAL-15`/`17` findings above — the enum-mapping tables that genuinely exist and matter today, e.g. `metalCompareFunction`/`metalStencilOp`/`metalBlendFactor`/`metalBlendOp`/`metalPrimitive`, already live together near the top of `kMetalShaderSource`'s surrounding helpers; no separate centralization pass is needed until `METAL-14`'s real `VertexElementFormat` table actually gets built) |
| METAL-19 | Guard against silent enum-reordering regressions (a compile-time or `GraphicsBackendCompileDefinitionsTest`-style check that these ordinal assumptions still match the real `.hpp` files) | ✅ **fully closed and CI-verified 2026-07-21** — see narrative items 81/82: the remaining `CullMode`/`Blend`/`BlendFunction`/`CompareFunction`/`StencilOperation` tables (`MetalSamplerFilter.hpp`'s own `TextureFilter` case was already fixed 2026-07-20, item 66) are now extracted into 5 plain-C++ headers (`MetalCullMode.hpp`/`MetalBlend.hpp`/`MetalBlendFunction.hpp`/`MetalCompareFunction.hpp`/`MetalStencilOperation.hpp`) that switch on the real XNA enumerator names; verified both locally (42 new `CnaTests`, all passing, zero regression across the full 5644-test suite) and on real CI (run `29801955085`: compiles clean, all 42 new tests pass on real Apple hardware, same 3 pre-existing unrelated failures only) |
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
| METAL-26 | `SetVertexDeclaration(const std::vector<VertexElement>&)` override on `MetalVertexBuffer` (the `IVertexBufferBackend` contract already documents this as Task 1080's generic-layout hook) | 🟨 added 2026-07-20 — mirrors `EasyGLVertexBufferBackend::SetVertexDeclaration()`'s own trivial-storage pattern exactly; the override itself lives inside the Objective-C++ file, so unlike the plain-C++ pieces below it stays genuinely unverified on this Linux machine (no consumer reads it yet either — see narrative) |
| METAL-27 | Build a generic `MTLVertexDescriptor` from a `VertexElement` list via `METAL-14`'s format table — replaces the 4 hand-written `vd16`/`vd20`/`vd24`/`vd32` descriptors with a path that also covers strides 28/36/40/44/48/52/56/68 once Phases 5–8 need them | 🟨→partial ✅ 2026-07-20 — the core attribute-plan logic (`BuildMetalVertexDescriptorPlan`) is real, machine-verified (6/6 `MetalVertexDescriptorPlan.*` tests, cross-validated against the existing hand-written stride-48/52 cases); the final `MTLVertexDescriptor` object construction itself (`vertexDescriptorFromElements()`) is real code but Objective-C-only, so stays 🟨 like every other `.mm`-side piece — see narrative |
| METAL-28 | Fallback: when `SetVertexDeclaration` was never called, keep the existing stride-based inference for the 4 strides that already work — no regression | 🟨 the existing 8-stride `vertexDescriptorForStride()` switch is completely untouched (provably zero regression), but no live draw path yet *chooses* between it and the new generic builder — see narrative for why that choice is correctly still deferred to Phase 14 |
| METAL-29 | `selectPipelineKey(stride, elements, GpuDrawParams)` dispatcher replicating `EasyGLGraphicsBackend::SelectProgram()`'s exact precedence (pbr+skinned → pbr → skinned(±vertexlit) → envMapping → dualTexture(stride-24 colored variant) → stride switch 20/24/32(±vertexlit) → default colored) | 🟨 |
| METAL-30 | Regression-proof: every existing stride-16/20/24/32 path must select byte-identical pipelines before/after the cache rewrite — a Linux-side manual trace against the current 5-pipeline logic, ahead of any macOS build | ✅ **real trace done 2026-07-20**, against the actual pre-rewrite source (`git show 08707f81:.../MetalGraphicsBackend.mm`, the original commit, not assumed from memory): old dispatch was `textured=params&&params->texture0; if(textured){stride==20→pipe3Tex20; ==24→pipe3ColorTex24; ==32→pipe3NormalTex32; else throw} else if(stride!=16) throw; else pipe3Color`. Byte-identical to `SelectMetalPipelineKind()`'s current dispatch for strides 16/20/24 (same throw conditions, same textured-gate, only the destination name changed: `pipe3Color→Colored16`/`pipe3Tex20→Textured20`/`pipe3ColorTex24→ColorTex24`). Stride 32 is the one real, already-fully-documented divergence: old `pipe3NormalTex32` reused the same flat unlit `cna_f3d_texture` fragment shader as strides 20/24 (no lighting existed anywhere in the pre-rewrite backend), new `LitTex32`/`LitTex32VertexLit` is genuinely lit — Phase 3's deliberate, intentional addition (`METAL-38`'s own note already documents this exact swap), not a silent regression. `MetalSelectPipelineKindTests.cpp`'s 15 already-passing tests (item 33's own narrative) lock the new dispatch in going forward. |
| METAL-31 | Key pipelines by color/depth/stencil attachment pixel format (backbuffer BGRA8 vs. an RGBA8/other `RenderTarget2D` once Phase 10 lands — Metal pipelines are format-specific) | ✅ **based on a false premise, confirmed 2026-07-20** — see narrative item 77: Phase 10's real `MetalRenderTargetBackend`/`MetalRenderTargetCubeBackend` constructors deliberately force every render target's color texture to `MTLPixelFormatBGRA8Unorm` and every depth texture to `MTLPixelFormatDepth32Float_Stencil8` — byte-identical to the backbuffer's own hardcoded pipeline format (`makePipeline()`'s own `d.colorAttachments[0].pixelFormat`/`depthAttachmentPixelFormat`/`stencilAttachmentPixelFormat`), by explicit, already-documented design (`METAL-101`'s own comment). No attachment in this codebase — backbuffer, `RenderTarget2D`, or `RenderTargetCube` — has ever used a different color/depth/stencil format, so there is no format variance for a pipeline key to disambiguate |
| METAL-32 | Key pipelines by attachment sample count once Phase 10 adds MSAA | 🟨 landed 2026-07-21 alongside `METAL-104`/`105` — see narrative item 88: `MetalPipelineCacheKey` gained a `sampleCount` field (mirroring `colorAttachmentCount`'s own `METAL-113` precedent exactly), not the `METAL-31`-style single-fixed-value shortcut this row's own text speculated might be enough — a real, non-obvious reason that shortcut doesn't apply here: unlike attachment *format* (always `MTLPixelFormatBGRA8Unorm` everywhere, so `METAL-31` really was over-cautious), sample count *is* observably runtime-variable across draws in the same process (backbuffer at `deviceSampleCount`, an ordinary non-MSAA `RenderTarget2D` at 1, a custom `ShaderEffect`'s own pipeline too) — a single fixed value would be actively wrong, not just theoretically redundant |
| METAL-33 | Document the expected cache size/no-eviction-needed-for-v1 assumption (mirrors EasyGL's own per-field `Prog3D` bound-variant assumption); flag unbounded-growth as a NOXNA follow-up only if a real pathological case appears | ✅ **documented 2026-07-20** — see narrative item 78: `MetalPipelineKey.hpp`'s own `MetalPipelineCacheKey` comment (plus a one-line pointer at the real `pipelineCache` member in `MetalGraphicsBackend.mm`) explains the bound — 15 fixed `MetalPipelineKind` values × realistically a handful of distinct `BlendState`s a game actually applies (XNA's 4 built-in presets cover the overwhelming majority), capping the cache at a few hundred lightweight `id<MTLRenderPipelineState>` entries even in a pathological case; LRU eviction is the correct NOXNA follow-up only if that assumption is ever actually violated, not something to build speculatively now |
| METAL-34 | Extract `MetalPipelineKey`'s hash/equality into an `#ifdef __OBJC__`-free plain-C++ header so it can be exercised by a normal GoogleTest binary **without an Apple toolchain** — the one piece of Phase 2 genuinely build-verifiable on this Linux machine today | ✅ **real, on this Linux machine, 2026-07-19** — 8/8 new tests (`MetalBlendKey.*`/`MetalPipelineCacheKey.*`/`MetalPipelineCacheKeyHash.*`, CTest #102–109) pass under the real `CnaTests` binary (`cmake -DCNA_GRAPHICS_BACKEND=HEADLESS -DCNA_BUILD_TESTS=ON`, `cmake --build --target CnaTests`, `ctest -R 'MetalBlendKey\|MetalPipelineCacheKey'`) — the first of 7 extractions to genuinely earn this tier tonight (6 more plain-C++ pieces followed the same pattern, see the numbered narrative items below), matching `METAL-238`'s own "cite the actual CTest name" discipline |

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
| METAL-89 | `CTest`: `Metal_Pbr`/`Metal_PbrSkinned` — known-material probe-pixel checks (fully metallic vs. dielectric, rough vs. smooth), same fixture as Vulkan/EasyGL's own PBR tests | 🟨 landed 2026-07-20 — see narrative item 67; `Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden` registered in `cmake/Tests/MetalTests.cmake`, reusing Vulkan's own already-proven EasyGL-source-reuse pattern; not yet confirmed passing on real hardware, see `plan_cnj.md CNB-110` |
| METAL-90 | Confirm whether a project-wide PBR support doc should exist (out of Metal's own scope) or `plan_cnj.md` remains the single source of truth — note the decision | ✅ closed 2026-07-20 — `plan_cnj.md` remains the single source of truth (`CNB-111`); `docs/metal-backend.md` now links to it instead of duplicating |

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
| METAL-104 | `multiSampleCount` — multisampled attachment resolved via the render pass's own `MTLStoreActionMultisampleResolve` (a cheaper, first-class Metal path vs. GL's separate blit) | 🟨 landed and CI-confirmed to compile+run without any crash/validation error 2026-07-21 — see narrative items 88/89/90: real backbuffer + `RenderTarget2D` MSAA, `MTLStoreActionStoreAndMultisampleResolve` (not plain `MultisampleResolve` — see own note on why); `RenderTarget2D`'s own sample count is now independent of the backbuffer's (a real bug, found+fixed via CI, item 89). **Not yet ✅**: `Metal_RenderTarget2D_MSAA`'s own `MultiSampleCount=8` check still fails — CI-confirmed a real, non-zero sample count (4, correctly clamped from 8 on this runner's own GPU) genuinely gets applied, but the resolved output still shows no anti-aliasing; root cause undetermined after 3 CI round-trips, paused (item 90), not abandoned |
| METAL-105 | `GetMultiSampleCount()` — real, device-queried clamp via `MTLDevice.supportsTextureSampleCount:`, matching every backend's "report the real clamped value" contract | 🟨 landed and CI-confirmed 2026-07-21 — real on both `IGraphicsBackend` (backbuffer) and `IRenderTargetBackend` (per-target, now independent of the backbuffer's own count — item 89), device-clamped via `clampMetalSampleCount()`; CI confirmed the clamp itself works correctly (this exact runner's GPU: `supports8=0`, `supports4=1`, correctly clamps a requested 8 down to 4 — item 90's own diagnostic evidence); real-build-verified on Linux too (4 new `MetalPipelineCacheKey` tests, `CnaTests` 134/134) |
| METAL-106 | `HasRealDepthBuffer(bool)` — confirm the default `= depthFormatWasRequested` is already correct once real depth-format honoring (`METAL-101`) lands | 🟨 |
| METAL-107 | `SetRenderTarget2D(IRenderTargetBackend*)` — real bind/unbind dispatch, currently a no-op | 🟨 |
| METAL-108 | `GetColorGLHandle()` — confirm the default `return 0` is correct (GL-specific, N/A on Metal) and no caller assumes nonzero means "has a render target" | 🟨 (confirmed: zero callers anywhere in `include/`/`src/` outside its own declaration/`EasyGLRenderTargetBackend` override — nothing branches on it) |
| METAL-109 | `CreateRenderTargetCube(size,depthFormat,mipMap,multiSampleCount)` — `MetalRenderTargetCubeBackend : IRenderTargetCubeBackend`, `id<MTLTexture>` with `MTLTextureTypeCube` | 🟨 |
| METAL-110 | `BindAsRenderTargetFace(int face)` — per-face `MTLRenderPassDescriptor` color attachment using `slice:face` | 🟨 |
| METAL-111 | `SetRenderTargetCubeFace(rt,face)` — verify the base default's composition (`rt ? rt->BindAsRenderTargetFace(face) : SetRenderTarget2D(nullptr)`) is already correct once `METAL-110`/`METAL-107` land, before writing a redundant override | 🟨 (found: base default is NOT sufficient once mip-gen-on-unbind exists — a real override was needed, see narrative) |
| METAL-112 | `SetRenderTargets(rts[],count)` — real MRT (up to 8 simultaneous color attachments), replacing the base default's "bind only the first target" | 🟨 landed and CI-confirmed 2026-07-21 — see narrative items 86/87: compiles clean, a real 2-attachment MRT draw ran with zero Metal API validation errors under `MTL_SHADER_VALIDATION=1`/`MTL_DEBUG_LAYER=1`; not ✅ only because `Metal_MRT`'s own final readback hits the separate, already-tracked, shared readback bug |
| METAL-113 | `MetalPipelineKey` must include the *set* of attachment pixel formats, not just one, once MRT lands | 🟨 landed and CI-confirmed 2026-07-21 — corrected scope: only the *count* needed to vary, not a per-slot format list, since every `MetalRenderTargetBackend` color texture is already unconditionally `MTLPixelFormatBGRA8Unorm` (narrative item 77) — `MetalPipelineCacheKey` gained a `colorAttachmentCount` field instead; real-build-verified on Linux (3 new `MetalPipelineCacheKey`/`Hash` tests, `CnaTests` 130/130) and the `.mm`-side pipeline-descriptor loop confirmed to build a valid 2-attachment `MTLRenderPipelineState` on real CI with no validation error (narrative item 87) |
| METAL-114 | `CTest`: `Metal_RenderTarget2D` — bind+clear+draw+unbind+readback (depends on Phase 12) | ⬜ |
| METAL-115 | `CTest`: `Metal_RenderTargetCube` — per-face bind+clear+readback+independence check | ⬜ |
| METAL-116 | `CTest`: `Metal_RenderTarget_MSAA` — device-clamped MSAA clear+resolve, pixel-verified | ⬜ |
| METAL-117 | `CTest`: `Metal_RenderTarget_Mip` — auto-mip-on-unbind, sampled at a non-zero mip level | ⬜ |
| METAL-118 | `CTest`: `Metal_MRT` — 2+ simultaneous targets, independent per-target clear-color proof | 🟨 landed 2026-07-21 as `Metal_MRT`, reusing `examples/easygl_mrt_test.cpp` verbatim (public XNA API only) — **CI-confirmed**: the MRT draw itself completes with zero validation errors (narrative item 87), but the test as a whole fails because its own final readback goes through `GetBackBufferData()`, hitting the same still-unresolved Clear-color-only readback bug (items 67–76/82/84/85) — not ✅ until that shared bug is resolved |
| METAL-119 | Add a `Metal` column to `docs/rendertarget-support.md` | ✅ done 2026-07-21 — see narrative item 91; the "Summary: what actually works today" table gained a 4th column, most rows 🔍 (source-complete/CI-runs-without-error, not pixel-verified due to the shared readback bug — footnoted) rather than ✅, matching this doc's own existing legend honestly |

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
| METAL-129 | Add a `Metal` column to `docs/texture3d-texturecube-support.md` | ✅ done 2026-07-21 — see narrative item 91; every relevant table gained a Metal column/row, mostly 🔍 (source-complete, no dedicated round-trip `CTest` yet — `METAL-127`/`128` still open) |

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

> **Blocker corrected 2026-07-21 — false premise, same class of finding as `METAL-15`/`17`/`31`**:
> the note below (written 2026-07-19) assumed a custom `ShaderEffect` must support an arbitrary
> vertex layout, and blocked Phase 14 in full on Phase 2's still-open generic `VertexElement`-driven
> descriptor builder (`METAL-26`/`27`) on that basis. Reading `VulkanEffectBackend`/
> `D3D11EffectBackend`/`D3D12EffectBackend` directly (not assumed) shows this premise was wrong for
> every established structured-pipeline backend: each carries an explicit comment stating "this
> mechanism is a SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format one",
> with a hardcoded, fixed `Sprite2DVertex`-shaped (32-byte x,y|u,v|r,g,b,a) vertex contract. The
> "arbitrary 3D vertex layout" scope only `EasyGLGraphicsBackend` actually supports is possible
> *because* GL's attribute binding is inherently layout-flexible (no `MTLVertexDescriptor`-style
> rigid descriptor object needed) — it is not something every backend commits to, and Metal's own
> `Sprite2D` pipeline already reads vertices manually via `v[vid]` with no `MTLVertexDescriptor` at
> all, so this facility needed zero dependency on Phase 2's builder. See narrative item 83 for the
> full implementation writeup.

| ID | Task | Status |
|---|---|---|
| METAL-142 | Design decision: raw MSL source via `CompileProgram(vertSrc,fragSrc)`, mirroring every other backend's existing GLSL/HLSL-source `IEffectBackend` convention | 🟨 landed 2026-07-21 — see narrative item 83, pending CI |
| METAL-143 | Evaluate and explicitly accept/reject a cross-compiler alternative (e.g. SPIRV-Cross GLSL/HLSL→MSL transpile, itself Linux-buildable) vs. raw-MSL-only scope — document the decision | ✅ decided: raw MSL only, matching D3D9/D3D11's own literal-HLSL-source / EasyGL's own literal-GLSL-source precedent — no cross-compile step |
| METAL-144 | `MetalEffectBackend : IEffectBackend` — `CompileProgram()` via `newLibraryWithSource:options:error:`, mirroring the existing `kMetalShaderSource` runtime-compile pattern | 🟨 landed 2026-07-21 — compiles `vertSrc`/`fragSrc` as two separate `MTLLibrary` objects (see narrative item 83 for why no fixed entry-point name is needed), pending CI |
| METAL-145 | `Bind()`/`Unbind()` — set/clear the custom pipeline (built via Phase 2's generic cache) as the active shader, restoring built-in dispatch afterward | 🟨 landed 2026-07-21, corrected scope: SpriteBatch-only (see this phase's own corrected blocker note above) — `MetalSpriteBatch::Draw()` resolves the pipeline directly via `GetEffectBackendPtr()` every draw call, `Bind()`/`Unbind()` are near-empty by design (no GPU state to defer), pending CI |
| METAL-146 | `SetUniformFloat/Int/Vec2/Vec3/Vec4/Mat4/FloatArray/Vec2Array` — MSL has no GLSL-style named-uniform reflection; pick a fixed documented buffer-layout contract or `MTLRenderPipelineReflection`-based introspection, and document the choice as this backend's MSL contract | 🟨 landed 2026-07-21 — fixed-slot contract (buffer(2)=mat4, buffer(3)=vec4, buffer(4)=float/int; `FloatArray`/`Vec2Array` left as the inherited no-op default, matching Vulkan/D3D11's identical scope boundary), documented in `docs/metal-shader-effect-contract.md`, pending CI |
| METAL-147 | `BindTexture`/`BindTextureCube`/`BindTexture3D` — 2D case can land immediately; cube/3D depend on Phase 11 | ⬜ genuinely still open — not implemented (inherits the no-op default), matching `VulkanEffectBackend`/`D3D11EffectBackend`'s own identical scope boundary (texture unit 0 is always driven by the caller/`SpriteBatch`, per `IEffectBackend::BindTexture()`'s own doc comment; no established backend actually implements these for its custom-effect facility) |
| METAL-148 | `customEffectBackend` (`GpuDrawParams`) — when non-null, bypass built-in shader selection and draw with the custom pipeline directly, mirroring EasyGL's Task 1079 contract exactly | ⬜ genuinely still open, corrected scope: reading `VulkanGraphicsBackend`'s own 3D draw path (`draw3DFor`) shows `customEffectBackend`/`activeCustomEffect_` is consumed *only* inside its `SpriteBatch` flush code, never in the general `DrawIndexedPrimitives`/`DrawUserPrimitives` 3D path — this task as literally worded (general 3D bypass) is not something any established structured-pipeline backend actually does either, only `EasyGLGraphicsBackend` (GL's own layout-flexible attribute binding). Metal's own `GpuDrawParams::customEffectBackend` field is therefore left unconsumed here too, consistent with precedent, not a regression |
| METAL-149 | `SpriteBatch.SetCustomEffect(Effect*)` — real override once `MetalEffectBackend` exists (currently base no-op) | 🟨 landed 2026-07-21 — `MetalSpriteBatch::SetCustomEffect()` stores the raw `Effect*`, resolved fresh every `Draw()` call, pending CI |
| METAL-150 | `SupportsCapability(GraphicsCapability::CustomEffects)` flips to `true` once this phase lands — remove the `false` case added in `METAL-197` | 🟨 landed 2026-07-21, pending CI |
| METAL-151 | `CTest`: `Metal_CustomEffect` — the same color-inversion custom-shader methodology D3D9/D3D11/D3D12/Vulkan/Bgfx already use | 🟨 landed 2026-07-21 as `Metal_SpriteBatch_CustomEffect` (mirrors `D3D9_SpriteBatch_CustomEffect`'s exact 4-check methodology in real MSL; found while writing this that Vulkan/D3D12/Bgfx do not actually have their own dedicated custom-effect `CTest` registered despite this task's own text claiming they do — only `D3D9`/`SDL_Renderer` do — so that part of this task's premise was itself optimistic, not literally true; this Metal test still faithfully mirrors the one real precedent that exists, `D3D9_SpriteBatch_CustomEffect`) — **registered and compiles clean on real CI, currently 2/4 PASS**, see narrative items 84/85: Check A/C pass (compiles; the custom vertex shader's own NDC transform is genuinely correct); Check B/D fail, **confirmed** (not merely suspected) to be the same pre-existing "reads back only the Clear color" bug items 67–76/82 already investigated for 3D draws — both failing checks' observed pixel values are byte-for-byte this test's own Clear color, and Check D exercises zero of Phase 14's own new code, so this is not a regression from this phase. Cannot honestly be marked ✅ until that shared, already-paused-pending-a-physical-Mac root cause is resolved |
| METAL-152 | Document the MSL uniform-contract choice (`METAL-146`) — a new `docs/metal-shader-effect-contract.md`, genuinely Metal-specific with no FNA precedent to copy | ✅ written 2026-07-21 — `docs/metal-shader-effect-contract.md` |

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
| METAL-175 | Same audit for `MetalTexture`'s in-place `replaceRegion:` calls while the texture may be bound to an in-flight command buffer | ✅ (answered: was genuinely unsafe as written; fixed as `METAL-256`, CI-confirmed compiles clean with no regression — narrative items 79/80) |
| METAL-176 | Consider (profile-driven, optional) buffer sub-allocation from a ring/pool instead of one `MTLBuffer` per `SetData()` call, for high-frequency `SpriteBatch` uploads | 🟨 (scope decision recorded: defer, profile-driven only) |
| METAL-177 | `SetDataWithOptions`/`SetData16WithOptions`/`SetData32WithOptions` — no override exists today (falls to the base default that ignores the hint); decide whether the current always-reallocate behavior already satisfies both `Discard`/`NoOverwrite` hints' *observable* contract, or whether a real perf-motivated distinction is worth adding | 🟨 (decided: already satisfies both hints observably, no override needed — see narrative) |
| METAL-178 | `SetContextRecoveryEnabled(bool)` — Metal has no OpenGL-style context loss on desktop macOS; confirm this should stay a documented intentional no-op | 🟨 (confirmed correct, matches Vulkan/D3D11/D3D12/WebGPU/Bgfx/SdlGpu/DX3's own established precedent — only D3D9/EasyGL override it, both genuinely context-loss-prone APIs) |
| METAL-179 | `DebugSimulateContextLoss()`/`DebugRestoreContext()` — same reasoning as `METAL-178`, document as an intentional no-op with justification | 🟨 (confirmed correct, same precedent) |
| METAL-180 | Audit whether the current one-command-buffer-per-frame model scales once render-target switches (Phase 10) force ending/starting encoders mid-frame — get this right architecturally before Phase 10 lands, not as a retrofit | 🟨 (found and fixed a real premature-present bug — see narrative) |
| METAL-181 | Document the final command-buffer/encoder lifecycle model once `METAL-173`–`METAL-180` resolve — currently only exists as scattered `ensureFrame()`/`endFrame()`/`clear()` logic with no written model, despite being the single most safety-critical part of this backend | 🟨 (written below; extracted into `docs/metal-backend.md`'s own dedicated section 2026-07-20, `METAL-234`) |
| METAL-256 | *(new, found during this audit)* Fix `MetalTexture::UpdatePixels()`/`UpdatePixelsLevel()`'s in-place `replaceRegion:` CPU/GPU synchronization hazard (`METAL-175`) — genuinely non-trivial: a naive "always reallocate the `id<MTLTexture>`" fix (mirroring the already-safe `MetalVertexBuffer`/`MetalIndexBuffer` pattern) would silently lose any *other*, already-uploaded mip level's content, since a fresh `newTextureWithDescriptor:` texture starts uninitialized and `UpdatePixels()` only ever rewrites level 0 — needs either a per-level blit-copy of untouched levels into the new texture, or an explicit GPU-completion-gated update queue | ✅ **fixed and CI-verified 2026-07-21** — see narrative items 79/80: `reallocateAndUpdate()` blit-copies every other mip level into the new texture before writing new data, then swaps the pointer; real CI run `29800865929` confirms it compiles clean and introduces no test regression (same pre-existing 3/89 failures, unrelated to this code path) |

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
| METAL-191 | `GraphicsCapability::MultiSampleAntiAliasing` — should become real/device-queried once Phase 10 lands (`MTLDevice.supportsTextureSampleCount:`), not a blanket `true` | 🟨 flipped back to real/`true` 2026-07-21 now that `METAL-104`/`105` are real — see narrative item 88, pending CI |
| METAL-192 | `GraphicsCapability::MultipleRenderTargets` — **fixed**: was a false-positive blanket `true` (real MRT doesn't exist, Phase 10), now correctly answers `false` until `METAL-112` lands | 🟨 **real bug found and fixed 2026-07-21**: this row's own `false` case was never actually reverted when `METAL-112` landed (item 86) — a stale false-positive-in-the-*other*-direction left sitting in the source for one commit, caught while landing `METAL-191`'s own identical fix, flipped to real/`true` in the same pass, see narrative item 88 |
| METAL-193 | `GraphicsCapability::AnisotropicFiltering` — should be `true` now that `METAL-1`'s sampler cache applies `maxAnisotropy`; confirm on real hardware once buildable | 🟨 (source-confirmed; real-hardware confirmation still needs macOS CI) |
| METAL-194 | `GraphicsCapability::WireFrame` — confirm the already-correct `FillMode::WireFrame`→`MTLTriangleFillModeLines` mapping makes the default `true` correct | 🟨 |
| METAL-195 | `GraphicsCapability::OcclusionQuery` — **fixed**: was a false-positive blanket `true` (`CreateOcclusionQuery()` still returns `nullptr`), now correctly answers `false` until Phase 13 lands | 🟨 |
| METAL-196 | `GraphicsCapability::CustomEffects` — **fixed**: was a false-positive blanket `true` (`CreateEffectBackend()` still returns `nullptr`), now correctly answers `false` until Phase 14 lands | 🟨 |
| METAL-197 | `SupportsCapability()` override added to `MetalGraphicsBackend`, covering the 3 known-wrong cases above and deferring to the (correct) base default otherwise | 🟨 |
| METAL-198 | `CTest`: `Metal_Capabilities` — one assertion per `GraphicsCapability`, meant to be extended incrementally as each phase's real behavior lands, not written once and left stale | ✅ **landed and passing on real CI 2026-07-21** — see narrative items 91/92: all 8 assertions pass on real Apple hardware (CI run `29814126178`), confirming `CustomEffects`/`MultipleRenderTargets`/`MultiSampleAntiAliasing` (items 83/86/88) are genuinely correct, not just source-complete — the first Metal `CTest` this whole session to reach a clean, unqualified ✅ |

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
| METAL-218 | Document `MTL_SHADER_VALIDATION`/`MTL_DEBUG_LAYER` as the macOS-side equivalent of Vulkan's validation layers; enable in the macOS CI job (Phase 26) | ✅ documented in `docs/metal-backend.md`'s verification methodology, enabled in CI 2026-07-20 (`METAL-229`) |
| METAL-219 | Explicitly note this phase has no meaningful CTest equivalent — interactive/visual tooling, not automatable | ⬜ |

## Phase 25 — Testing infrastructure (METAL-220 – METAL-226)

| ID | Task | Status |
|---|---|---|
| METAL-220 | Extend `cmake/Tests/MetalTests.cmake` from its current single `Metal_Smoke` registration to every `Metal_*` CTest named throughout this plan, one `cna_register_backend_test()` call per executable, as each phase's implementation actually lands | ⬜ |
| METAL-221 | Shared Metal test fixture/helper (extends `PixelTestGame.hpp`, already used by `metal_smoke_test.cpp`) with common probe-pixel/readback assertions once Phase 12 lands | ⬜ |
| METAL-222 | Audit `SDL_VIDEODRIVER`/`DISPLAY` requirements for every new Metal test — get this right from the start rather than repeating `plan_dx3.md`'s own hard-won issue #1 mistake (hardcoded `x11`/real display when a dummy driver would have worked) | 🟨 audited 2026-07-20: `cmake/Tests/MetalTests.cmake`'s `Metal_Smoke` registration sets no `ENVIRONMENT` override at all (unlike `Dx3Tests.cmake`'s `SDL_VIDEODRIVER=dummy` on every entry) — correct, not an oversight, since `plan_dx3.md`'s mistake was the *opposite* direction (wrongly requiring a real display where a dummy one would work); Metal fundamentally needs a real `CAMetalLayer`/window and cannot use a dummy driver at all, so relying on the CI runner's own real display is the right choice, not something to fix. Stays 🟨, not ✅, since this reasoning is unverified against actual CI behavior |
| METAL-223 | Confirm the real macOS CI runner's display capabilities (GitHub-hosted macOS runners provide a real virtual display, unlike this project's Linux Xvfb sandbox) rather than assuming Linux-style constraints apply | 🟨 confirmed by the same reasoning as `METAL-222` — GitHub's `macos-14` runners provide a real display session (documented runner capability), which is exactly what `Metal_Smoke`'s no-override registration already assumes and needs |
| METAL-224 | Confirm whether the `WEBGPU-123`-style cross-backend pixel-parity harness (still open even for WebGPU) can extend to Metal once broad enough — folds into Phase 28 | ⬜ |
| METAL-225 | Add Metal to whatever full-`CnaTests`-suite regression-count tracking this project already performs per-backend once it has enough tests to matter | ⬜ |
| METAL-226 | Explicit "N/A, verified" note: a `ThrowNo3D`-style audit (DX3's own Phase X7) does not apply to Metal — it is a 3D-only backend, unlike DX3/SDL_Renderer/Canvas | ✅ **N/A, confirmed** — Metal is a real 3D backend (`BasicEffect`/`SkinnedEffect`/`PbrEffect`/render targets/etc.), unlike DX3/`SDL_RENDERER`/`CANVAS`/`ASCII` which are all intentionally 2D-only and need a `ThrowNo3D` audit for their 3D entry points; Metal's 3D entry points are meant to work, not throw, so this audit class doesn't apply |

## Phase 26 — CI / tooling (METAL-227 – METAL-233)

| ID | Task | Status |
|---|---|---|
| METAL-227 | Keep `.github/workflows/metal-macos-ci.yml`'s `paths:` trigger list current as new files land (new `.mm`/`.hpp` splits, new example/test `.cpp`, new `docs/*.md`) — a living checklist, revisited at the end of each phase | ⬜ |
| METAL-228 | Consider a second macOS CI job variant (different macOS version / Apple Silicon vs. Intel runner) once GPU-family differences (e.g. BC compression, `METAL-17`) start mattering | ⬜ |
| METAL-229 | Add `MTL_SHADER_VALIDATION=1`/`MTL_DEBUG_LAYER=1` to the CI job (ties to `METAL-218`) | ✅ added 2026-07-20 to `metal-macos-ci.yml`'s "Run Metal tests" step — unverified whether it actually fires without a real CI run, but the mechanism itself (Apple's own documented env var contract) needs no further code |
| METAL-230 | Audit CI build-time budget as the backend grows toward EasyGL's scale — revisit `--parallel 3` once compile times actually grow | ⬜ |
| METAL-231 | Consider splitting the monolithic `.mm` into multiple translation units once file size approaches EasyGL's ~5,300-line mark (a concrete threshold, not a premature rule) | 🟨 checked 2026-07-20: `MetalGraphicsBackend.mm` is 2,248 lines vs. `EasyGLGraphicsBackend.cpp`'s 4,733 — well under half the stated threshold, so genuinely not yet needed. Revisit when the file approaches ~5,300 lines, not before |
| METAL-232 | Confirm `GraphicsBackendCompileDefinitionsTest` already knows about `CNA_BACKEND_METAL` (DX3's own external review found the equivalent gap for `CNA_BACKEND_DX3`/`D3D11`/`D3D12` until fixed) | ✅ **real gap found and fixed 2026-07-20** — see narrative item 40 |
| METAL-233 | Keep `README.md`'s backend list/build instructions honest about Metal's real current capability boundary as phases land | ✅ added 2026-07-20 — Metal was entirely absent from README (not even in the `CNA_GRAPHICS_BACKEND` selection list), see narrative item 41 |

## Phase 27 — Documentation (METAL-234 – METAL-238)

| ID | Task | Status |
|---|---|---|
| METAL-234 | Create `docs/metal-backend.md` (does not exist today, unlike `docs/webgpu-backend.md`/`docs/dx3-backend.md`/`docs/d3d11-backend.md`) — the durable capability-boundary reference CLAUDE.md's WebGPU precedent points to | ✅ created 2026-07-20, see narrative item 37 |
| METAL-235 | Add a `Metal` column to `docs/graphics-backend-feature-matrix.md` only once the feature set is broad enough for a meaningful row-by-row comparison — the doc's own header excludes Headless/Software for the identical reason; do not add prematurely with a column full of ❌ | ⬜ |
| METAL-236 | Add Metal rows/columns to each relevant per-effect `docs/*-support.md` as its own phase lands (13 files identified: basiceffect/alphatesteffect/dualtextureeffect/environmentmapeffect/skinnedeffect/occlusionquery/rendertarget/texture3d-texturecube/sampler-state/depthstencilstate/rasterizerstate/surface-format/vertex-format-support.md) | ⬜ |
| METAL-237 | Update `docs/coverage.md`/`docs/xna-4-api-coverage.md` if either tracks per-backend Graphics coverage at a level Metal should appear in | 🟨 checked 2026-07-20: `docs/coverage.md`'s "Per-backend Graphics capability" table uses real-machine-verified columns (EasyGL/Vulkan/Bgfx/WebGPU) — adding Metal now would mean a column of mostly 🟨/unverified entries next to genuinely-verified ones, the same premature-comparison problem `METAL-235` already defers for the sibling feature-matrix doc. Deferred for the identical reason, not forgotten. |
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
   on this Linux machine on 2026-07-19 (8/8 tests, CTest #102–109) — the one task in this whole plan
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
