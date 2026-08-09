# Metal graphics backend

## Supported target

`METAL` is CNA's direct native Metal backend, selected with
`-DCNA_GRAPHICS_BACKEND=METAL`. SDL3 owns the window and supplies
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
the final four commits only change `NEXT.md` or `plan_metal.md`.

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
fresh macOS workflow result is mandatory before claiming the adaptation itself compiles or runs on
Metal.

### Post-audit findings

These IDs continue the existing `plan_metal.md` sequence without changing any historical finding:

| Finding | Severity | Evidence | Disposition |
|---|---|---|---|
| `METAL-258` — backbuffer readback returned successful clear-only pixels | High | Historical run `29814126178`: six draw/readback tests observed only the clear color. | Supported contract disabled: `ReadBackbuffer` throws `NotSupportedException`; dependent native tests are not registered as supported gates. |
| `METAL-259` — RenderTarget2D MSAA reported four samples without edge coverage | High | The same run applied sample count four but its diagonal edge remained binary. | Supported contract disabled: MSAA capability is false and every requested sample count clamps/reports zero. |
| `METAL-260` — cached `CAMetalDrawable` lacked an owned reference | High | MRR source audit found a `nextDrawable` (+0) result stored across calls and mid-frame commits without retain/release ownership. | Implementation fixed: a portable-tested retained owner keeps it alive, mid-frame commits preserve it, and presentation releases it after command commit. Adapted-Mac validation remains pending. |
| `METAL-261` — partial backend construction had no MRR rollback | Medium | Source audit showed that a throw after device/view/layer/queue acquisition (including runtime MSL-library failure) destroyed `impl_` but `Impl` had no destructor. | Implementation fixed: `Impl` now owns bounded teardown for constructor failure and normal destruction, with drawable/layer/view ordering explicit. Adapted-Mac validation remains pending. |
| `METAL-262` — default device was retained twice | Medium | `MTLCreateSystemDefaultDevice()` supplied the create-rule ownership reference and the constructor immediately sent an additional `retain`. | Implementation fixed: the redundant retain is removed; stored create/`new*` objects each have exactly one owning reference. Adapted-Mac validation remains pending. |

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
| `OcclusionQuery` | true | Native visibility-result buffer and query objects. |
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
  `GpuDrawParams::customEffectBackend` throw;
- malformed stream metadata throws `std::invalid_argument`; multistream and instancing throw
  `System::NotSupportedException`;
- TextureCube, Texture3D, and `CreateRenderTarget2DEXT` accept only
  `SurfaceFormat::Color`; unsupported formats throw;
- non-default per-target color-write masks, multisample coverage masks, sampler maximum mip level,
  and sampler LOD bias throw instead of being ignored.

## Adapted architecture

The backend implements the current `IGraphicsBackend` surface, including normalized
`RenderTargetBindingDescriptor`, `BlendWriteState`, applied format/depth queries, depth/stencil
capability queries, and boolean texture transfer contracts. A portable compile-time test asserts
that `MetalGraphicsBackend` is not abstract, catching future pure-virtual interface drift without
requiring Apple headers.

Ordinary draws consume current `GpuDrawParams::vertexStreams` metadata. The supported shape is
exactly one valid per-vertex stream whose buffer, slot, stride, and combined stride agree with the
draw argument; the documented legacy empty-stream route remains valid. Pipeline selection uses
`CombinedVertexStrideOr`, and fog uniforms use the current FNA-compatible four-component
`fogVector` dot-product contract.

Render-target type, descriptor, slice, cube-face, and foreign-backend checks occur before changing
the active target. Backend resources retain their native device/queue dependencies, active target
destructors end encoders before releasing attachments, and render-target switches end encoding
without presenting. Presentation remains an explicit end-of-frame action.

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

Metal window creation adds `SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY` only in the Metal
selection branch, leaving other backend window flags unchanged.

MSL is embedded in `MetalGraphicsBackend.mm` and compiled at runtime with
`newLibraryWithSource:`. The CPU-side matrix, uniform, enum, vertex-layout, capability, format,
sample, and stream-policy helpers remain plain C++ so they can be compiled and tested off Apple
platforms. Historical MRT, MSAA, and custom-effect implementation scaffolding is not reachable
through the supported contract and must not be re-enabled merely by changing a capability bit.

## Validation

Portable validation on Linux uses a stable in-repository HEADLESS build with `DISPLAY` unset:

```sh
env -u DISPLAY cmake -S . -B cmake-build-headless \
  -DCNA_GRAPHICS_BACKEND=HEADLESS -DCNA_BUILD_TESTS=ON \
  -DCNA_USE_CCACHE=ON -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCNA_MAX_VENDORED_BUILD_JOBS=2
env -u DISPLAY cmake --build cmake-build-headless \
  --target CnaTests cna_test_metal_portable -j4
env -u DISPLAY ctest --test-dir cmake-build-headless -R '^Metal' \
  --output-on-failure -j4
```

At the 2026-08-09 adaptation checkpoint, both targets built successfully and all 143 unique
Metal-prefixed portable tests passed. CTest reports 144/144 because it registers those 143 tests
individually and also registers the same set once as the `Metal_PortableHelpers` aggregate. The
HEADLESS build graph contained no `MetalGraphicsBackend.mm` reference. A Linux configure with
`-DCNA_GRAPHICS_BACKEND=METAL` failed at the intended macOS-only gate and never enabled
Objective-C++.

The helper-only sanitizer checkpoint used the stable `cmake-build-asan` directory, GNU C++ 14.2.0,
`Debug`, HEADLESS, ccache, vendored job limit two, and these exact sanitizer flags:

```text
CMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
```

`cna_test_metal_portable` built with `-j4` and passed 143/143 tests under
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:strict_string_checks=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Its registered aggregate passed 1/1, and a
complete scan of the build, direct-test, and CTest logs found no AddressSanitizer,
LeakSanitizer, UndefinedBehaviorSanitizer, or runtime-error diagnostic. The broader sanitizer
`CnaTests` target is not evidence here: its link was stopped by an unrelated existing
Headless/audio-harness `CNA::Logger::Warn` static-library ordering failure.

These checks cover interface shape and portable logic, not Objective-C++ syntax, Apple framework
linking, native Objective-C object lifetime, runtime MSL compilation, native resource validation,
or pixels. The macOS workflow builds the backend with at most three parallel jobs, enables Metal
validation, and runs the supported native smoke/capability checks plus the portable Metal suites.
Its result is the remaining delivery gate.

## Historical record

`plan_metal.md` retains the original task-by-task narrative as historical evidence. It is not the
current support matrix. `docs/metal-history-map.tsv` accounts for all 99 commits in the historical
lane and maps the 88 chronological signed replays plus 11 documented omissions onto this
adaptation.
