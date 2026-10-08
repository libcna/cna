# Metal graphics renderer

## Supported target

`METAL` is CNA's direct native Metal renderer, selected with
`-DCNA_GRAPHICS_RENDERER=METAL`. SDL3 owns the window; the renderer takes the platform's native
window (`NSWindow*` on macOS, `UIWindow*` on iOS, through `IPlatformWindow::GetNativeHandle()`),
adds its own `CAMetalLayer`-backed view to it, and renders directly through `MTLDevice`,
`MTLCommandQueue`, `MTLRenderCommandEncoder` and that layer. It does not route draws through
SDL_Renderer, SDL_GPU, or a third-party graphics abstraction.

Supported targets are **macOS** and, since `plans/plan_apple_m4.md` `AM4-037`, **iOS/iPadOS**
(UIKit view path; `NativeWindowSystem::UIKit`). CMake rejects `METAL` on every other system.
iOS evidence is the Simulator only -- see `docs/apple-platforms.md`; no physical device has run
it. On iOS there is no `displaySyncEnabled`: the layer always presents on the display's refresh,
so `PresentInterval::Immediate` is recorded but not applied. CNA's macOS deployment floor is
13.3 (`CNA_MACOS_DEPLOYMENT_TARGET`); sampler LOD bias needs macOS/iOS 26 (`AM4-034`).

## Evidence boundary

The immutable historical lane is `feature/metal` through `48928d113cb864f78d754256d2d559d914d4f1a7`.
Its latest production-changing commit is `e0f42426836ce9f2d4823d50732850877020aef1`;
the final four commits only change `NEXT.md` or `plans/plan_metal.md`.

GitHub Actions run `29814126178` built `e0f42426836ce9f2d4823d50732850877020aef1`
successfully on `macos-14` with Xcode 15.4 and Metal validation enabled. It then passed 136 of
143 tests. The seven failures were:

| Test | Historical result |
|---|---|
| `Metal_PbrEffect_Golden` | Backbuffer readback contained only the clear color. |
| `Metal_SkinnedPbrEffect_Golden` | Backbuffer readback contained only the clear color. |
| `Metal_DrawUserPrimitives_VPC` | Backbuffer readback contained only the clear color. |
| `Metal_SpriteBatch_CustomEffect` | Backbuffer readback contained only the clear color. |
| `Metal_MultipleRenderTargets` | Backbuffer readback contained only the clear color. |
| `Metal_Backbuffer_MSAA` | Backbuffer readback contained only the clear color. |
| `Metal_RenderTarget2D_MSAA` | Applied sample count four, but the rendered edge remained binary. |

`Metal_Capabilities` passed in that run, but it only asserted a small boolean set and did not
prove the advertised rendering paths. The repository records the run number and conclusions in
prose; it does not contain downloaded Actions logs, artifact checksums, a git note, or a usable
GPU-capture artifact. The attempted GPU-trace capture was unsupported on the hosted runner.

The post-audit adaptation changes interfaces and supported behavior after that run. Therefore the
historical successful compile is not compile or runtime evidence for the adapted `.mm` source. A
fresh macOS workflow result is required before claiming the adaptation itself compiles or runs on
Metal; under the repository's authoritative source-continuity policy it is an external validation
and support-confidence boundary, not an integration blocker.

### Apple M4 run (2026-10-07)

`plans/plan_apple_m4.md` built the adapted source natively on a Mac mini M4 (macOS 27, Xcode 27,
Apple clang 21) and ran it on a real window with `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`. The
historical clear-only readback had two causes, both fixed: readback itself was disabled
(`AM4-025`) and every 3D vertex was clipped by a transposed WVP (`AM4-028`). With the Metal
renderer's 81 shared renderer-neutral pixel/contract tests registered (`AM4-026`, label
`MetalShared`), `ctest -L Metal` passes 80 of 80 (one skip: the DXT TextureCube fixture, since
cube textures accept only `SurfaceFormat::Color`), with no validation-layer assertion. The fixes
it took are `AM4-025`, `AM4-027`..`AM4-029` and `AM4-031`..`AM4-035`.

### Post-audit findings

These IDs continue the existing `plans/plan_metal.md` sequence without changing any historical finding:

| Finding | Severity | Evidence | Disposition |
|---|---|---|---|
| `METAL-258` — backbuffer readback returned successful clear-only pixels | High | Historical run `29814126178`: six draw/readback tests observed only the clear color. | Supported contract disabled at the time; re-enabled by `plans/plan_apple_m4.md` `AM4-025` with pixel proof on an M4 (see below). The 3D half of the clear-only symptom was `AM4-028`. |
| `METAL-259` — RenderTarget2D MSAA reported four samples without edge coverage | High | The same run applied sample count four but its diagonal edge remained binary. | Supported contract disabled: MSAA capability is false and every requested sample count clamps/reports zero. |
| `METAL-260` — cached `CAMetalDrawable` lacked an owned reference | High | MRR source audit found a `nextDrawable` (+0) result stored across calls and mid-frame commits without retain/release ownership. | Implementation fixed: a portable-tested retained owner keeps it alive, mid-frame commits preserve it, and presentation releases it after command commit. Adapted-Mac validation remains pending. |
| `METAL-261` — partial renderer construction had no MRR rollback | Medium | Source audit showed that a throw after device/view/layer/queue acquisition (including runtime MSL-library failure) destroyed `impl_` but `Impl` had no destructor. | Implementation fixed: `Impl` now owns bounded teardown for constructor failure and normal destruction, with drawable/layer/view ordering explicit. Adapted-Mac validation remains pending. |
| `METAL-262` — default device was retained twice | Medium | `MTLCreateSystemDefaultDevice()` supplied the create-rule ownership reference and the constructor immediately sent an additional `retain`. | Implementation fixed: the redundant retain is removed; stored create/`new*` objects each have exactly one owning reference. Adapted-Mac validation remains pending. |
| `METAL-263` — fixed-stride draws ignored declaration meaning | High | All native routes selected descriptors only by stride, so same-stride semantic/offset/format mismatches could silently reinterpret bytes. | Then: every route called the shared declaration-fidelity oracle before submission. Superseded by `AM4-035`: the descriptor is now built from the declaration, so the meaning is bound rather than refused. |
| `METAL-264` — cube/3D transfers were unchecked, tightly pitched, and mutated in flight | High | Face/mip/range/length arithmetic was incomplete, buffer blits used tight rows, and SetData mutated resources prior draws could still sample. | Overflow-safe transfer layouts use a macOS-safe 256-byte staging-row alignment; readback de-pads; SetData preserves untouched subresources and swaps a completed replacement. Native pixel proof remains pending. |
| `METAL-265` — Clear/encoder recreation lost viewport and scissor | High | Attachment setup overwrote requested state and fresh encoders omitted effective scissor state. | Requested state is separate from extent and preserved across encoders; the requested viewport is applied unchanged, while the enabled scissor is intersected with the attachment and rasterizer enable toggles apply immediately. |
| `METAL-266` — OcclusionQuery overclaimed split/exhausting code | High | Clear split query commands and slots were never recycled. | Then: capability false, factory throws. Fixed by `AM4-038` (per-encoder slots and completion, slot recycling). |
| `METAL-267` — BGRA targets returned swapped RGBA channels | High | Raw BGRA target bytes were copied into CNA RGBA Color storage. | Format-aware padded-row conversion swizzles target data; RGBA cube/3D remains unchanged. |
| `METAL-268` — RenderTarget2D uploads silently no-op'd | High | Public inherited Texture2D.SetData reached inherited empty void hooks. | RGBA→BGRA reallocate/copy/swap uploads are implemented; invalid input rejects before mutation. |
| `METAL-269` — missing stock textures carried stale encoder bindings | High | Null slots were not rebound, making output draw-order dependent. | Every used slot binds native or owned white/flat-normal/white-cube fallback; foreign non-null resources reject. |
| `METAL-270` — MRR wrapper/cache/state allocation was not transactional | High | Throw-after-retain, nil, release-before-new, cache-emplace, and logical/native divergence paths were found by source audit. | Scoped rollback owners, checked allocation/multiplication, ownership-through-emplace, and depth-state rollback close those paths; native lifetime proof remains pending. |
| `METAL-271` — texture pointer truthiness selected pipeline shape | High | Null untextured lit BasicEffect fell to Colored16 instead of the lit stride-32 pipeline. | Effect flags/canonical stride select shape; neutral textures represent absent stock samples. |
| `METAL-272` — Texture2D ignored ImageData format/shape | High | Every ImageData was treated as RGBA8 Color. | Only positive Color-format complete mip shape with exact base RGBA bytes is accepted. |
| `METAL-273` — SetBlendEnabled violated last-writer state | Medium | The control was inert and an intermediate latch model retained older state. | False installs opaque, true installs straight alpha, and later Set/Apply completely replaces it. |
| `METAL-274` — disabled depth still wrote storage | High | Always-compare state retained native depth writes. | Effective native write is `depthEnabled && requestedWrite`; requested state survives re-enable. |
| `METAL-275` — lighting-disabled uniforms retained light contributions | High | Metal diverged from EasyGL's accepted unlit normalization. | Ambient is one and directional diffuse/specular is zeroed where represented. |
| `METAL-276` — generated RT2D mips were reported undefined | High | Partial SetData could seed untouched generated texels from zero. | Levels become defined only after full upload or successful generation; uninitialized levels remain false. |
| `METAL-277` — expected nil drawable threw/retried | Medium | Background/minimized `nextDrawable=nil` reached an uncaught frame error. | One attempt per frame; backbuffer Clear/draw/marker/Present skip, RT work proceeds, Present resets. |
| `METAL-278` — command failure/readback ordering could return stale data | High | Errors were not latched, and active RT readback waited only for the later blit. | Common abandon/reset teardown surfaces async failure; exact sync commands are checked; active RT source render completes successfully before readback conversion/output. |
| `METAL-279` — empty logical scissor was illegal natively | High | Metal rejects zero native scissor extent. | Empty state submits a legal placeholder and suppresses draws, persisting across toggles/encoders. |
| `METAL-280` — Metal static archive reverse edge was undeclared | High | Objective-C++ calls CNA-owned Effect/math/color symbols. | METAL joins the existing renderer→CNA cycle declaration; native tests assert ordinary-link coverage. |
| `METAL-281` — retained texture/render-target renderers dereferenced a destroyed owner | High | Plain texture callbacks captured raw `Impl*`; RT2D/RTCube stored raw `Impl&`. Copyable/movable texture wrappers and public renderer handles can legally retain those renderers after `GraphicsDevice` tears down its Metal `Impl`. | A shared health token becomes inactive before native teardown. Resources hold only a weak owner, route live pending failures through the common consume/abandon path, reject operations after owner death, and RT destructors skip owner-state cleanup when the weak lock is gone while still releasing independently owned native textures. Portable lifetime/retry/escape tests pass; native lifetime proof remains pending. |

## Current capability contract

Every current `CNA::GraphicsCapability` is handled explicitly. There is no permissive default.

| Capability | Reported | Boundary |
|---|---:|---|
| `ThreeD` | true | Built-in fixed-layout Metal pipelines only. |
| `DepthStencilBuffer` | true | Native combined depth/stencil attachments. |
| `MultiSampleAntiAliasing` | true where the device multisamples (every Metal GPU does at 4x) | `AM4-141`: backbuffer, RenderTarget2D, RenderTargetCube and MRT; a request is rounded down to a count `supportsTextureSampleCount:` accepts (the M4: 2 and 4, not 8). Each target renders into a multisample texture and resolves at every encoder boundary (`StoreAndMultisampleResolve`); a cube face has its own 2D multisample texture. `MultiSampleMask` is ignored single-sampled, as in XNA; on a multisampled target a mask keeping every sample draws normally, one keeping none is not drawn, and a partial one is written by the stock fragment's `[[sample_mask]]` (a pipeline variant specialized on a function constant), refused for a SpriteBatch custom effect's MSL. Evidence: `Metal_Shared_rendertarget_msaa_*`, `rendertargetcube_msaa_face_test`, `gfx077_colorwritechannels_3d_test`'s MSAA leg, `Metal_MRT_StockEffectContract` 22/22 with its multisampled scenarios, under the validation layers. |
| `MultipleRenderTargets` | true | `AM4-097`: up to eight RenderTarget2D or RenderTargetCube-face members; slot 0 owns depth; stock pipelines write COLOR0 only and mask the other attachments out; each member's mip chain is regenerated at unbind; a destroyed member leaves the set. Evidence: the shared `mrt_stock_effect_contract_test` as `Metal_MRT_StockEffectContract`, 19/19 with the validation layers (22/22 with its multisampled scenarios since `AM4-141`). |
| `AnisotropicFiltering` | true | Native sampler-state mapping. |
| `WireFrame` | true | `FillMode::WireFrame` maps to `MTLTriangleFillModeLines`. |
| `OcclusionQuery` | true | `AM4-038`: one counting slot per encoder the query spans, summed; slots recycled; one query open at a time (a second `Begin` is refused). |
| `CustomEffects` | true | `AM4-077`: SpriteBatch-scoped MSL (`docs/metal-shader-effect-contract.md`); a 3D draw with a custom effect throws. |
| `CompiledEffects` | true with `CNA_METAL_COMPILED_EFFECTS=ON` (off by default), else false | `AM4-144`: XNA Effect Framework bytecode -- MojoShader's SPIR-V profile linked per draw against the bound declarations, translated to MSL in process by SPIRV-Cross and compiled by Metal once per linked body. 3D draws (buffered, user, indexed, multi-stream, instanced) and `SpriteBatch.Begin(..., effect)` (each same-texture run once per pass, XNA's SpriteEffect supplying a pixel-only pass's vertex stage). Direct3D 9 pixel centres, integer `vPos`, device slots authoritative after `Apply()`, unbound samplers opaque black. Evidence: every shared conformance section Vulkan/SDL_GPU/WebGPU run plus nine Shader Model 1-3 sections, 43 Metal tests, under the validation layers; what the SPIR-V profile cannot express (and vertex textures, FX-109) refuses by name -- `docs/fx-compiled-effects.md`. |
| `Texture3D` | true | Color-format native 3D textures. |
| `MultiStreamVertexInput` | true | `AM4-143`: up to sixteen streams, each bound at its own vertex-buffer index and `VertexOffset`; the stock functions' semantics are looked up in the per-vertex streams' combined declaration, with XNA's binding-time usage-index remap, and each attribute reads the stream that holds it. A stream that supplies nothing is not fetched. |
| `Instancing` | true | `AM4-143`: `DrawInstancedPrimitives` with every stock effect. The first four elements of the per-instance streams (concatenated in slot order, whatever their usages) are CNA's per-instance world matrix, as on EasyGL (`REMED-GFX-122`), applied before the effect's World -- after skinning, and to normals and PBR tangents -- in a vertex-function variant specialized on a function constant, so ordinary pipelines are unchanged; a missing column reads (0, 0, 0, 1). Each per-instance stream steps once per `InstanceFrequency` instances; `baseVertex` advances the per-vertex streams only. A per-instance stream, or one of several per-vertex streams, needs a VertexDeclaration. Evidence: the shared `Instanced*`/`*MultiStream*`/`*BindingOffset*` CnaTests (103) and `instanced_textured_draw_test` (37 legs: texture, World, alpha test, lighting, DualTexture, skinned, PBR, env-map), under the validation layers; each of four deliberate breakages (step rate, stream offset, instance matrix, stream index) fails 14-27 of them. |
| `StencilBuffer` | true | Native stencil plane and state mapping. |
| `AdditiveBlending` | true | Native blend factors and operations. |

The following boundaries are deterministic rather than silent degradation:

- `ReadBackbuffer` (`AM4-025`) answers in the game's backbuffer coordinates, as EasyGL does since
  KF-6: each requested pixel is the drawable pixel under its centre, through the logical viewport
  (Retina scale and letterbox offset included). The pending pass completes first; a frame with no
  drawable reads zero. It throws while a render target is bound.
- `SetRenderTargets` accepts zero descriptors (restore backbuffer), one normalized 2D/cube-face
  descriptor, or an MRT set of up to eight (`AM4-097`); array slices are refused;
- a non-null `GpuDrawParams::customEffectRenderer` (a 3D draw with a custom effect) throws; custom
  effects are a SpriteBatch facility;
- malformed stream metadata throws `std::invalid_argument`; a stream layout the stock vertex
  functions cannot read (an integer instance-matrix element, an undeclared per-instance stream)
  throws `System::NotSupportedException` naming it;
- Texture2D stores every uncompressed XNA format natively (`AM4-142`): the float, half, 10:10:10:2,
  16-bit-unorm, signed-normalized and Alpha8 formats in their own Metal pixel formats, and the packed
  Bgr565/Bgra5551/Bgra4444 as `B5G6R5Unorm`/`BGR5A1Unorm`/`ABGR4Unorm` (the 4444 texel's nibbles
  rotated on upload) on Apple-family GPUs only -- an Intel Mac's GPU has none of the three, and there
  they refuse by name. One- and two-channel formats are sampled through a swizzled view as Direct3D 9
  reads them: (r, 1, 1, 1) and (r, g, 1, 1); Alpha8 reads (0, 0, 0, a). XNA's point-filter-only rule
  for the float formats is enforced by the shared draw validation, which reads each texture's format.
  DXT has no native route on macOS yet (iOS has no BC at all);
- RenderTarget2D and RenderTargetCube render into Color, Rgba1010102, Rg32, Rgba64, Single, Vector2,
  Vector4, HalfSingle, HalfVector2, HalfVector4 and HdrBlendable (`AM4-142`), so HDR, bloom and float
  shadow maps keep their range; any other preference becomes Color, as XNA's
  `QueryRenderTargetFormat` does. Pipelines are keyed on the bound targets' pixel formats;
- TextureCube and Texture3D accept only `SurfaceFormat::Color`; unsupported formats throw;
- render target 0's `ColorWriteChannels` is honoured (a pipeline property, `AM4-079`), and
  `ColorWriteChannels1..3` for the MRT attachments a compiled pixel shader writes (`AM4-146`; the
  built-in effects write COLOR0 only and leave the other attachments untouched). Sampler `MaxMipLevel` is the sampler's
  `lodMinClamp` and `MipMapLevelOfDetailBias` its `lodBias` (`AM4-034`), for 3D draws and
  SpriteBatch alike; `lodBias` exists from macOS/iOS 26, and below that a non-zero bias still
  throws.

## Adapted architecture

The renderer implements the current `IGraphicsRenderer` surface, including normalized
`RenderTargetBindingDescriptor`, `BlendWriteState`, applied format/depth queries, depth/stencil
capability queries, and boolean texture transfer contracts. A portable compile-time test asserts
that `MetalRenderer` is not abstract, catching future pure-virtual interface drift without
requiring Apple headers.

Ordinary draws consume current `GpuDrawParams::vertexStreams` metadata. The supported shape is
exactly one valid per-vertex stream whose buffer, slot, stride, and combined stride agree with the
draw argument; the documented legacy empty-stream route remains valid. Fog uniforms use the
current FNA-compatible four-component `fogVector` dot-product contract.

Vertex input is declaration-driven (`AM4-035`, `MetalDeclaredVertexInput.hpp`). A buffer that
received a `VertexDeclaration` selects its stock pipeline from the channels it carries and the
effect's flags, and every input that pipeline's vertex function reads is bound at the declared
element's own offset and format, matched by usage and usage index; channels the effect does not
read are ignored, and the layout is part of the pipeline key. A buffer with no declaration is the
canonical XNA vertex type its stride names, with exactly the old fixed descriptor. Refused by name:
a missing input, a raw integer format for a float input, `BLENDINDICES` other than `Byte4`, and an
element outside the record. The lit `BasicEffect` functions read `COLOR0` (`AM4-081`) exactly when
`VertexColorEnabled` is on, as XNA's permutations do; with it off a declared colour is ignored
(`AM4-098`). `DualTextureEffect` samples its second texture
with `TEXCOORD1`; a record with one set feeds it to both, as `VULKAN-150` does.

Render-target type, descriptor, slice, cube-face, and foreign-renderer checks occur before changing
the active target. Renderer resources retain their native device/queue dependencies, active target
destructors end encoders before releasing attachments, and render-target switches end encoding
without presenting. A transient `nextDrawable=nil` is a one-attempt, non-error backbuffer-frame
skip: Clear, draw, marker, and Present do not retry during that logical frame, while offscreen
render-target work remains available. Presentation resets availability for the next frame and
remains the only action that presents a drawable.

Requested viewport/scissor state is independent of attachment extent and survives Clear and
encoder recreation. Each new encoder applies the requested viewport unchanged and intersects only
the enabled scissor with its current target; disabled scissoring uses the full attachment, and a
logically empty enabled scissor installs a legal native placeholder
while suppressing draw submission. Texture transfers validate face, mip, coordinates, extents, and
length before native work; buffer readback uses padded 256-byte rows on macOS and converts/de-pads
only after the exact source/blit commands have completed successfully.

The Objective-C++ file uses manual retain/release. `MTLCreateSystemDefaultDevice()` and stored
`new*` results each contribute their one create-rule ownership reference; borrowed layer,
command-buffer, encoder, and drawable results are retained only when they outlive the acquiring
call. A retained `CAMetalDrawable` survives mid-frame command commits, then is released exactly
once after `presentDrawable:` is encoded and the presenting command buffer is committed. `Impl`
owns cleanup, so a constructor exception rolls back every partially acquired native object and
destroys the SDL Metal view after drawable/layer use has ended. This follows Apple's
[manual memory-management rules](https://developer.apple.com/library/archive/documentation/General/Conceptual/DevPedia-CocoaCore/MemoryManagement.html)
and Clang's documented
[retained-return conventions](https://clang.llvm.org/docs/AutomaticReferenceCounting.html#retained-return-values).
Asynchronous command failures are recorded through a lifetime-safe latch and consumed at the next
synchronous renderer entry. Consumption abandons any uncommitted command/encoder and resets cached
frame state before throwing; synchronous mip/readback operations check the exact command they
submitted rather than treating a later queue operation as proof that the source work succeeded.
Texture and render-target renderers share an owner-health token but retain only a weak `Impl`
reference, so copied/moved wrappers or a retained public renderer handle do not prolong the graphics
device. A live resource operation locks the owner for common failure cleanup and can retry after
that one failure is consumed; after teardown begins it rejects before dereferencing the owner.
Render-target destruction conditionally cleans active binding state only while the weak owner can
be locked, then releases its independently owned native attachments in either case.

Metal window creation adds `SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY` only in the Metal
selection branch, leaving other renderer window flags unchanged.
This is also the post-audit correction to historical `METAL-257`: its claim that CNA never set
`SDL_WINDOW_HIGH_PIXEL_DENSITY` was false. The Metal branch has requested both flags since the
initial replay, so that finding's Retina-window premise was already satisfied rather than an open
cross-renderer fix.

MSL is embedded in `MetalRenderer.mm` and compiled at runtime with
`newLibraryWithSource:`. The CPU-side matrix, uniform, enum, vertex-layout, capability, format,
sample, and stream-policy helpers remain plain C++ so they can be compiled and tested off Apple
platforms. The custom-effect scaffolding was re-enabled by `AM4-077`, MRT by `AM4-097` and MSAA by
`AM4-141`, each only with the evidence listed for it above. `METAL-259`'s binary edge did not
reproduce once the policy applied real sample counts: the shared multisampled contracts read back
partially covered edges.

## Validation

Portable validation on Linux uses a stable in-repository HEADLESS build with `DISPLAY` unset:

```sh
env -u DISPLAY cmake -S . -B cmake-build-headless \
  -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON \
  -DCNA_USE_CCACHE=ON -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCNA_MAX_VENDORED_BUILD_JOBS=2
env -u DISPLAY cmake --build cmake-build-headless \
  --target CnaTests cna_test_metal_portable -j4
env -u DISPLAY ctest --test-dir cmake-build-headless -R '^Metal' \
  --output-on-failure -j4
```

At the 2026-08-09 adaptation checkpoint, both targets built successfully and all 206 unique
Metal-prefixed portable tests passed. CTest reports 207/207 because it registers those 206 tests
individually and also registers the same set once as the `Metal_PortableHelpers` aggregate. The
HEADLESS build graph contained no `MetalRenderer.mm` reference. A Linux configure with
`-DCNA_GRAPHICS_RENDERER=METAL` failed at the intended macOS-only gate and never enabled
Objective-C++.

The helper-only sanitizer checkpoint used the stable `cmake-build-asan` directory, GNU C++ 14.2.0,
`Debug`, HEADLESS, ccache, vendored job limit two, and these exact sanitizer flags:

```text
CMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
```

`cna_test_metal_portable` built with `-j4` and passed 206/206 tests under
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:strict_string_checks=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. A complete scan of the saved direct-test log
found no AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer, or runtime-error diagnostic.

These checks cover interface shape and portable logic, not Objective-C++ syntax, Apple framework
linking, native Objective-C object lifetime, runtime MSL compilation, native resource validation,
or pixels. The macOS workflow builds the renderer with at most three parallel jobs, enables Metal
validation, and runs the supported native smoke/capability checks plus the portable Metal suites.
Its result is the external validation and support-confidence boundary before claiming adapted
native compile/runtime evidence; authoritative source continuity does not make it an integration
blocker.

## Historical record

`plans/plan_metal.md` retains the original task-by-task narrative as historical evidence. It is not the
current support matrix. `docs/metal-history-map.tsv` accounts for all 99 commits in the historical
lane and maps the 88 chronological signed replays plus 11 documented omissions onto this
adaptation.
