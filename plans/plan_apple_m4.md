# Apple silicon (Mac mini M4) stabilization campaign

Task IDs `AM4-*`. This plan tracks the work of qualifying CNA and sharp-runtime on a physical
Apple-silicon Mac (macOS 27, Xcode 27, Apple clang 21) and in the iOS Simulator. It complements
[`plan_apple.md`](plan_apple.md) (the Apple platform subsystem) and
[`plan_metal.md`](plan_metal.md) (the Metal renderer's history); the current support boundary is
in [`docs/apple-platforms.md`](../docs/apple-platforms.md).

## Ground rules

- The Apple platform is `CNA_PLATFORM=SDL3`. No `COCOA`/`APPKIT`/`UIKIT` platform backend.
- Vulkan/MoltenVK on Apple is out of scope; the DirectX renderers are not applicable.
- Builds live outside the source tree (`~/Desktop/build/cna-<os>-<platform>-<renderer>-<config>`), and
  the persistent SDL cache is redirected there with `-DCNA_SDL_PREBUILT_ROOT=...`.
- Every row names the evidence behind it. "Builds" and "works" are separate claims.

## Host

| Item | Value |
|---|---|
| Machine | Mac mini M4, arm64, 16 GB |
| OS / SDK | macOS 27.0.1 (26A434); MacOSX27.0, iPhoneOS27.0, iPhoneSimulator27.0 SDKs |
| Toolchain | Xcode 27.0 (27A266a), Apple clang 21.0.0, CMake 4.4.4, Ninja 1.13.2 |
| Homebrew deps used | ffmpeg 9.0.2, zstd 1.5.7, curl, nlohmann-json 3.12, opus 1.6.1 |

## Tasks

| ID | Task | Status |
|---|---|---|
| `AM4-001` | sharp-runtime: Apple CSPRNG. `Guid::NewGuid` and `RandomNumberGenerator` called `getentropy()` after including only `<unistd.h>`; macOS declares it only in `<sys/random.h>` and the iOS SDKs not at all. Apple now uses `arc4random_buf()` (the Android arm), which is what libc++'s `std::random_device` uses on Apple. | Done (sharp-runtime) |
| `AM4-002` | sharp-runtime + CNA: floating-point `std::from_chars` under Xcode 26+/LLVM 20 libc++. The overload is now *declared* at every deployment target with `availability(strict, introduced=26.0)`, so `SharpRuntime::FromCharsFloat`'s `requires` probe passed and the call was a hard error at CNA's 13.3 floor. The shim also consults `_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT`; CNA's four direct floating `from_chars` calls go through the shim. | Done |
| `AM4-003` | `CNAEXT` placed before the class-key (`CNAEXT class X`) in 61 declarations. In strict-API mode `CNAEXT` is `[[deprecated(...)]]`, which GCC ignores there with a warning and Clang rejects as a misplaced attribute, so `cna_strict_xna_api_check` could not compile with Clang. Moved after the class-key (`class CNAEXT X`), the form the other 60 declarations already used. | Done |
| `AM4-004` | FFmpeg 8+: `AVCodec::sample_fmts` was removed. The WMA encoder in `BuildTimeMediaDecoder.cpp` uses `avcodec_get_supported_config()` on libavcodec >= 61.13 and the field below it. | Done |
| `AM4-005` | libzstd linked by bare name: `pkg_check_modules`' `_LIBRARIES` is `zstd`, which only links where the directory is a default search path (Linux `/usr/lib`, not Homebrew's `/opt/homebrew/lib`). Use `_LINK_LIBRARIES` (absolute paths). | Done |
| `AM4-006` | Build hygiene found by Apple clang/libc++: `PathUtf8Tests.cpp` used `std::sort` without `<algorithm>`; `cna_test_sdl_unsupported_3d_behavior` includes `<SDL3/SDL.h>` without linking `SDL3::SDL3` (it only resolved through system headers on Linux). | Done |
| `AM4-007` | Renderer/window tests are registered with `SDL_VIDEODRIVER=x11` (Linux-only); on macOS SDL has only `cocoa`/`offscreen`/`dummy`, so every such test failed before testing anything. The post-registration display-policy sweep drops `x11`/`wayland` pins on Darwin (SDL then picks `cocoa`), keeping `dummy`/`offscreen` and any test that asserts a native window system. Pure rule `cna_adapt_test_video_driver_to_host` with script-mode cases. | Done |
| `AM4-008` | `AvatarRenderer`'s GPU upload set its "uploaded" marker (`effect`) before building the buffers; when a 2D-only renderer refused `VertexBuffer`, the marker claimed a complete upload over an empty part list and the next `Draw` indexed past its end (segfault). Upload is now built into locals and committed whole. The 3D draw test is gated on `GraphicsCapability::ThreeD`; a new test pins clean repeated failure on a renderer without 3D. | Done |
| `AM4-022` | Process exit terminated with `recursive_mutex lock failed: Invalid argument` once a preset state (e.g. `SamplerState::LinearClamp`) had been shared: `GraphicsResource`'s identity mutex was a lazily constructed function-local static, so it was destroyed *before* the static presets whose destructors lock it. glibc tolerates locking a destroyed mutex; Darwin returns `EINVAL` and libc++ throws. The mutex is now intentionally immortal. | Done |
| `AM4-023` | `ContentFileSha256` on a directory returned a digest on macOS (libc++ opens a directory as an `ifstream` and reports EOF) where libstdc++ fails; directories are now refused up front on every library. | Done |
| `AM4-024` | `PathUtf8Test.DirectoryEnumeration...` assumed a byte-exact filesystem; APFS is normalization-insensitive, so the NFC and NFD spellings of `étude` are one file. The test expects only names that created their own entry. | Done |
| `AM4-025` | Metal `ReadBackbuffer` re-enabled (it threw `NotSupportedException` since `METAL-258`). It reads the drawable through the logical viewport, so `GetBackBufferData` answers in backbuffer pixels at Retina scale and under letterbox, after completing the pending pass. Evidence: `metal_capabilities_test`, the shared pixel tests of `AM4-026`. | Done |
| `AM4-027` | Metal SpriteBatch applied the window letterbox twice: `GraphicsDevice` already maps the logical viewport to its physical rectangle (offset and Retina scale) and sets it on the encoder, and the sprite projection folded the letterbox in again (a 64x64 batch in a 160x96 window landed at x 51..109 instead of 32..127). The sprite renderer now implements `SetViewportSizeEXT` and projects over the logical viewport it is handed, as Vulkan's does. Evidence: `spritebatch_presentation_test` 11/11 on Metal. | Done |
| `AM4-028` | Metal drew no 3D geometry at all (the 3D half of `METAL-258`'s clear-only readback). `drawMetal3D` transposed W*V*P before upload, but MSL reads a `float4x4` column by column, so the shader's `wvp * position` already multiplies by the transpose of what is uploaded; transposing on top computed WVP*v instead of XNA's v*WVP and put the translation row into w (w = 1 - x for an orthographic BasicEffect), clipping every vertex. `world` was always uploaded untransposed. Evidence: the BasicEffect control triangle of `xna_pixel_center_contract_test` went from 0 to 120 covered pixels; `rasterizerstate_cullmode_*`, `rendertarget2d_golden_test`, `basic_effect_test` pass. | Done |
| `AM4-029` | Metal did not implement XNA's Direct3D 9 pixel-centre convention, so the Primitives sample's exact 1x1 triangle `(x,y),(x+1,y),(x,y+1)` covered nothing. Metal now applies the clip-space translation EasyGL, Vulkan and WebGPU apply (63/128 of a pixel, filled topologies only, not on multisampled render targets), measured in the XNA backbuffer's pixels: on a Retina drawable half a *physical* pixel leaves the logical pixel's centre uncovered. Evidence: `xna_pixel_center_contract_test` 1 covered pixel (XNA: 1). | Done |
| `AM4-031` | Metal regenerated the mip chain of every `mipMap=true` render target on unbind, including a 1x1 one whose chain has one level; Metal validation aborts on `generateMipmapsForTexture:` there (`mipmapLevelCount(1) must be > 1`). RenderTarget2D and RenderTargetCube now generate only when the chain has more than one level, as FNA3D gates it. Evidence: `rendertarget_invalid_mip_level_test` leg D1 and `rendertarget_msaa_mip_readback_test` legs H1/H10 no longer abort under `MTL_DEBUG_LAYER=1`. | Done |
| `AM4-032` | Metal render targets ignored their `DepthFormat`: every one reported `Depth24Stencil8`, claimed real depth and stencil planes, and depth-tested a `DepthFormat::None` target. A target now reports the planes its format has (`None` none, `Depth24` depth only, `Depth16` as the `Depth24` it is stored as -- a reported substitution), so `GraphicsDevice` refuses a Clear of a missing plane, and while it is bound a missing plane is inert in the depth/stencil state (the shared `Depth32Float_Stencil8` attachment keeps pipelines compatible). Pure policy in `MetalDepthPolicy.hpp` with portable tests. Evidence: `rendertarget_msaa_depth_contract_test` M01/M05, `rendertarget_depthstencil_usage_test` X1/X2. | Done |
| `AM4-033` | Metal SpriteBatch mishandled `origin` with a destination rectangle: it pivoted about `destination + origin` (unscaled) without moving the quad, so any non-zero origin drew the sprite displaced. `origin` is in source texels and XNA places that point at the destination position: the quad now starts at `-origin * (destination / source)` and rotates about the destination, the arithmetic WebGPU's sprite renderer uses. Evidence: `point_sampling_contract_test` L2 (184 mismatched pixels before, 0 after); L1 (rotation about the centre) unchanged. | Done |

### sharp-runtime (branch `apple/m4-stabilization`)

`AM4-009`..`AM4-021` are sharp-runtime commits (see its `docs/Platform-macOS.md`): header conformance under Clang,
evaluation-order and moved-from-`std::function` bugs, libc++ `Regex` differences, Darwin `ProcessPath`, pre-1900 time
zones, process-tree kill, `Ping`, `NetworkInterface`, dual-ABI layout pins and test portability. Suite on M4:
18,084 run, 18,052 passed, 7 failed (`FileSystemWatcher` has no macOS backend), 25 skipped.
