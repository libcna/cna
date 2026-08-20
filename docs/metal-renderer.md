# Metal graphics renderer

## Supported target

`METAL` is CNA's direct native Metal renderer, selected with
`-DCNA_GRAPHICS_RENDERER=METAL`. SDL3 owns the window and supplies
`SDL_Metal_CreateView`/`SDL_Metal_GetLayer`; rendering goes directly through
`MTLDevice`, `MTLCommandQueue`, `MTLRenderCommandEncoder`, and `CAMetalLayer`. It does not route
draws through SDL_Renderer, SDL_GPU, or a third-party graphics abstraction.

The supported platform contract is **macOS only**. CMake rejects `METAL` unless
`CMAKE_SYSTEM_NAME` is `Darwin`, before enabling Objective-C++ or collecting `.mm` sources. iOS
and tvOS are unvalidated and are not claimed. CNA does not set an explicit macOS deployment
target; compatibility below the SDK and deployment defaults used by a successful build is not
established.

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

### Post-audit findings

These IDs continue the existing `plans/plan_metal.md` sequence without changing any historical finding:

| Finding | Severity | Evidence | Disposition |
|---|---|---|---|
| `METAL-258` — backbuffer readback returned successful clear-only pixels | High | Historical run `29814126178`: six draw/readback tests observed only the clear color. | Supported contract disabled: `ReadBackbuffer` throws `NotSupportedException`; dependent native tests are not registered as supported gates. |
| `METAL-259` — RenderTarget2D MSAA reported four samples without edge coverage | High | The same run applied sample count four but its diagonal edge remained binary. | Supported contract disabled: MSAA capability is false and every requested sample count clamps/reports zero. |
| `METAL-260` — cached `CAMetalDrawable` lacked an owned reference | High | MRR source audit found a `nextDrawable` (+0) result stored across calls and mid-frame commits without retain/release ownership. | Implementation fixed: a portable-tested retained owner keeps it alive, mid-frame commits preserve it, and presentation releases it after command commit. Adapted-Mac validation remains pending. |
| `METAL-261` — partial renderer construction had no MRR rollback | Medium | Source audit showed that a throw after device/view/layer/queue acquisition (including runtime MSL-library failure) destroyed `impl_` but `Impl` had no destructor. | Implementation fixed: `Impl` now owns bounded teardown for constructor failure and normal destruction, with drawable/layer/view ordering explicit. Adapted-Mac validation remains pending. |
| `METAL-262` — default device was retained twice | Medium | `MTLCreateSystemDefaultDevice()` supplied the create-rule ownership reference and the constructor immediately sent an additional `retain`. | Implementation fixed: the redundant retain is removed; stored create/`new*` objects each have exactly one owning reference. Adapted-Mac validation remains pending. |
| `METAL-263` — fixed-stride draws ignored declaration meaning | High | All native routes selected descriptors only by stride, so same-stride semantic/offset/format mismatches could silently reinterpret bytes. | Every indexed/non-indexed ordinary/direct route calls the shared declaration-fidelity oracle before submission; portable canonical/mismatch tests pass. |
| `METAL-264` — cube/3D transfers were unchecked, tightly pitched, and mutated in flight | High | Face/mip/range/length arithmetic was incomplete, buffer blits used tight rows, and SetData mutated resources prior draws could still sample. | Overflow-safe transfer layouts use a macOS-safe 256-byte staging-row alignment; readback de-pads; SetData preserves untouched subresources and swaps a completed replacement. Native pixel proof remains pending. |
| `METAL-265` — Clear/encoder recreation lost viewport and scissor | High | Attachment setup overwrote requested state and fresh encoders omitted effective scissor state. | Requested state is separate from extent and preserved across encoders; the requested viewport is applied unchanged, while the enabled scissor is intersected with the attachment and rasterizer enable toggles apply immediately. |
| `METAL-266` — OcclusionQuery overclaimed split/exhausting code | High | Clear split query commands and slots were never recycled. | Capability false, factory throws, visibility allocation omitted. |
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
| `MultiSampleAntiAliasing` | false | Every requested public sample count is clamped to zero. |
| `MultipleRenderTargets` | false | More than one descriptor is rejected before binding state changes. |
| `AnisotropicFiltering` | true | Native sampler-state mapping. |
| `WireFrame` | true | `FillMode::WireFrame` maps to `MTLTriangleFillModeLines`. |
| `OcclusionQuery` | false | Creation throws until command-boundary completion and slot recycling are fixed. |
| `CustomEffects` | false | Effect creation and non-null SpriteBatch custom effects throw. |
| `Texture3D` | true | Color-format native 3D textures. |
| `MultiStreamVertexInput` | false | More than one per-vertex stream is rejected. |
| `Instancing` | false | Instance streams and instance counts other than one are rejected. |
| `StencilBuffer` | true | Native stencil plane and state mapping. |
| `AdditiveBlending` | true | Native blend factors and operations. |

The following boundaries are deterministic rather than silent degradation:

- `ReadBackbuffer` throws `System::NotSupportedException`; it never returns known-wrong pixels.
- backbuffer and render-target MSAA report zero and allocate single-sample attachments;
- `SetRenderTargets` accepts zero descriptors (restore backbuffer) or one normalized 2D/cube-face
  descriptor and throws for MRT;
- custom effect construction, non-null `SpriteBatch::SetCustomEffect`, and a non-null
  `GpuDrawParams::customEffectRenderer` throw;
- malformed stream metadata throws `std::invalid_argument`; multistream and instancing throw
  `System::NotSupportedException`;
- TextureCube, Texture3D, and `CreateRenderTarget2DEXT` accept only
  `SurfaceFormat::Color`; unsupported formats throw;
- non-default per-target color-write masks, multisample coverage masks, sampler maximum mip level,
  and sampler LOD bias throw instead of being ignored.

## Adapted architecture

The renderer implements the current `IGraphicsRenderer` surface, including normalized
`RenderTargetBindingDescriptor`, `BlendWriteState`, applied format/depth queries, depth/stencil
capability queries, and boolean texture transfer contracts. A portable compile-time test asserts
that `MetalRenderer` is not abstract, catching future pure-virtual interface drift without
requiring Apple headers.

Ordinary draws consume current `GpuDrawParams::vertexStreams` metadata. The supported shape is
exactly one valid per-vertex stream whose buffer, slot, stride, and combined stride agree with the
draw argument; the documented legacy empty-stream route remains valid. Pipeline selection uses
`CombinedVertexStrideOr`, and fog uniforms use the current FNA-compatible four-component
`fogVector` dot-product contract.

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
platforms. Historical MRT, MSAA, and custom-effect implementation scaffolding is not reachable
through the supported contract and must not be re-enabled merely by changing a capability bit.

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
