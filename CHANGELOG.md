# Changelog

All notable changes to CNA are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major version is
0, the public API may change in any release — see the pre-1.0 note in
[`docs/releasing.md`](docs/releasing.md).

## [Unreleased]

## [0.3.0] — 2026-11-14

The 0.3.0 release, cut from `next`. CNA was reviewed against XNA 4.0 Refresh's IL and against
ported games. Wherever CNA behaved differently from XNA, it now follows XNA:
- content loading and building,
- the game loop,
- graphics objects and effects,
- audio and XACT,
- input and media,
- networking and the Guide.

The release also adds region and borrowed-window presentation, XACT reverb and xWMA wave banks,
compressed `.cnb` textures, SystemLink beyond one broadcast domain, online guests and Windows IDE
presets. It moves the FNA3D and MojoShader forks, and the experimental C ABI goes from `0.50.0` to
`0.60.0`.

**C++ programs and libraries must be rebuilt against this release.** Class layouts, virtual
functions and several signatures changed, and many refusals now throw XNA's exception types
instead of CNA's (see *Changed*). The release requires **sharp-runtime 0.3.0**, whose
`System::Version` lost its public data members, **easy-gl 0.1.2** and **meta-gl 0.4.2**.

### Dependencies

CNA consumes three separately versioned sibling checkouts with `add_subdirectory`, so checking out
this tag does not select them. This release requires, and was built and tested against:

| Sibling | Required | Verified against |
|---|---|---|
| sharp-runtime (`../sharp-runtime`) | **0.3.0** | [`v0.3.0`](https://github.com/libcna/sharp-runtime/releases/tag/v0.3.0), `3289baaee57987dfc2efef84871b989ceee57738` |
| easy-gl (`../easy-gl`) | **0.1.2** | [`v0.1.2`](https://github.com/libcna/easy-gl/releases/tag/v0.1.2), `9c6b12ef3c273532831c1943b1f503013598936c` |
| meta-gl (easy-gl's `../meta-gl`) | **0.4.2** | [`v0.4.2`](https://github.com/libcna/meta-gl/releases/tag/v0.4.2), `52d12d09c98f0e802239c40028738075b0fb241b` |

All three requirements moved, and each checkout was at its release tag:
- **sharp-runtime 0.3.0** brings the .NET Framework 4.0 behaviour CNA asked for (see *Changed*).
- **meta-gl 0.4.2** only adds enumerators and fixes its typed `glUniformMatrix` helpers for
  `transpose = GL_TRUE`.
- **easy-gl 0.1.2** only moves onto meta-gl 0.4.2. It changes no source.

`cmake/DependencyVersions.cmake` enforces all three at configure time. The installed C++ SDK's
`find_dependency(SharpRuntime)` asks for the same sharp-runtime version.

The easy-gl and meta-gl working trees carried the owner's uncommitted deletion of `AGENTS.md` and
`CLAUDE.md`; no build reads those files. Every other dependency is pinned by this repository,
through submodule gitlinks and the `GIT_TAG` values in `cmake/ThirdParty*.cmake` and
`cmake/RendererSelection.cmake`. This release moves FNA3D and MojoShader (see *Changed*).

### Added

- **C ABI `0.52.0` through `0.60.0`**: nine additive generations. The library exports 3,397
  symbols, and no existing layout or identity changed. Four planned rows remain, all of them
  Windows path qualification
  ([`docs/c-api/ABI_VERSIONING.md`](docs/c-api/ABI_VERSIONING.md)).
  - `0.52`: `cna_content_manager_load_video`, an independently owned Video from
    `ContentManager.Load<Video>`.
  - `0.53`: the GraphicsProfile legality queries
    `cna_texture_is_{,cube_,render_target_}format_allowed_by_profile_ext`.
  - `0.54`: value text and hashes, `CNA_GraphicsDeviceTypeEXT` and adapter queries,
    `cna_graphics_device_get_logical_size_ext`, the input-emulation switches and
    `cna_project_graphics_profile_{get,set}_ext`.
  - `0.55`: typed cube and volume transfers, raw index and back-buffer readback,
    `cna_graphics_device_present_region_ext`, base-instance drawing, and shader code and package
    snapshots.
  - `0.56`: model schema 2 (`cnb_model_v2.h`) and CNB helpers. The four CNB filesystem routes now
    convert UTF-8 to native paths (unverified on Windows).
  - `0.57`: gamer and `PropertyDictionary` routes, file-drop and text callbacks, and the clipboard.
  - `0.58`: `content_values.h` reflective loading with shared-resource fixups.
  - `0.59`: native cursors, and component and `VertexDeclaration` views. It also changes two rules:
    - `cna_gamer_enumerator_get_current` keeps its cached `Current` after a mutation, which
      `MoveNext` and `Reset` refuse.
    - `cna_sound_effect_create_from_encoded_ext` parses with XNA's strict PCM WAV reader, so it
      refuses non-WAV data it used to accept.
  - `0.60`: `cna_network_gamer_{get,set}_shared_machine_ext` and
    `cna_game_window_set_logical_size_provider_ext`.

  The renderer rename of `0.51.0` is under *Changed*. The C behaviour corrections listed under
  "Behaviour corrections within ABI 0.60" there change no route, layout or symbol.
- **`GraphicsDevice.Present(sourceRectangle, destinationRectangle, overrideWindowHandle)` works**
  (GAP-08a/b/c). Before, every non-default argument was refused, except `SOFTWARE`'s source
  rectangle. Now pixels outside the destination keep what was presented before, and an override
  is a native X11, Win32 or Cocoa window that SDL3 borrows.
  - Verified on `SOFTWARE`, `OPENGL33`, `SDLGPU` and `WEBGPU`. `DIRECTX9`'s pixels, `OPENGLES3` and
    `WEBGL2` are unverified.
  - FNA3D's OpenGL and SDL GPU drivers present a source rectangle and a whole-client destination. A
    partial destination throws `NotSupportedException` naming FNA3D-8 and the driver, and its
    D3D11 driver refuses every rectangle (KI-FNA3D-PRESENT).
  - New virtuals: `IPlatform::AdoptNativeWindowHandle` and
    `IPlatformSurfacePresenter::PresentRegion`.
- **XACT reverb** (GAP-04). DSP presets and `REVERBSEND`, which were discarded, now drive a stereo
  wet bus in both mixers through FAudio's reverb, ported under zlib (`third_party/faudio-reverb`).
- **XACT wave banks with xWMA (WMAv2) entries play**, decoded by FFmpeg. Before, a WMA or XMA
  entry returned null.
  - `CNA_ENABLE_WAVEBANK_CODECS=OFF|AUTO|ON` (default `AUTO`) is independent of
    `CNA_ENABLE_VIDEO`. It shares that option's FFmpeg discovery and platforms (not Windows, MinGW,
    Android, Emscripten or iOS), so a build without FFmpeg needs both options `OFF`, and `ON` fails
    configuration without FFmpeg.
  - Entries decode whole, on demand, at most 256 MiB each. XMA, XMA2 and WMA Pro are refused by
    name ([`docs/wavebank-codecs.md`](docs/wavebank-codecs.md)).
- **An Xbox 360 content build writes XMA sound effects when an XMA encoder is attached**
  (`SetBuildXmaEncoder`), as XNA's processor does (CNA-FIX-L16).
  - Without an encoder the build stays PCM, and `.cnb` refuses XMA.
  - New cache options `CNA_XMA_ENCODER_EXECUTABLE` and `CNA_XMA_ENCODER_LAUNCHER` set the encoder's
    default (a `-D` entry used to be ignored).
  - The adapter validates the XMA2 format and returns the encoder's quantized loop instead of the
    requested one. It fingerprints the program's SHA-256, launcher and arguments into
    `CNA.SoundEffectProcessor`'s version, so sound effects rebuild when the encoder changes. Only
    stub encoders are verified.
  - `ProcessedSoundEffect` gains an `encoded` member, so **C++ code that uses it must be rebuilt**
    ([`docs/xma-encoder-backend.md`](docs/xma-encoder-backend.md)).
- **Compressed `.cnb` textures.** A `Texture2D` built with DXT compression writes `Bc1`, `Bc2` or
  `Bc3` first and its processed `Rgba8` pixels second. Before, a `.cnb` build kept only the pixels
  and warned.
  - At load, a renderer that uploads the block format natively takes the blocks, and every other
    renderer takes the fallback. A file offering nothing the renderer can upload throws
    `ContentLoadException`.
  - `EncodeTexture2DToCnb` and `cna_cnb_encode_texture2d` accept `Bc1`–`Bc3`, with no C ABI
    version change. Cube, volume and SpriteFont textures stay `Rgba8`.
  - `CNA.TextureProcessor` goes to version 4, so Texture2D content rebuilds once
    ([`docs/cnb-format.md`](docs/cnb-format.md)).
- **Online sessions.**
  - Listings measure `QualityOfService` before joining (GAP-17b), from a service that advertises
    `qos-prejoin-v1`. Before, PlayerMatch and Ranked results always said `IsAvailable` was false.
  - Online guests can join PlayerMatch and Ranked sessions through a
    cna-gamer-services-server that offers `online-guests-v1` (schema 27). The server backs each
    guest with its host's account (the CNA replacement-service policy). With an older server,
    guests are still refused online.
  - Guests never earn achievements or own leaderboard rows. They leave when their parent signs
    out, and they count toward the local and session slot limits. Ranked still admits no join in
    progress.
- **SystemLink beyond one broadcast domain** (CNAEXT, for port forwards and virtual networks).
  - `CNA::Net::SystemLinkDiscovery`'s `setExtraSearchAddressesProperty({"host[:port]", …})` makes
    `Find`/`BeginFind` also query each address by unicast. An unresolvable name is skipped with a
    warning, and a malformed entry throws `ArgumentException`.
  - `setHostPortProperty(port)` binds a host's game port to a fixed number. 0 stays ephemeral, and
    a busy port makes `Create` fail with `NetworkNotAvailableException`.
  - Announces gain a host instance id, which older decoders ignore, so two hosts on one fixed port
    are listed separately.
- **`Keyboard::KeyDownEXT`** (CNAEXT), a `System::MulticastAction<Keys>`. It is raised on the
  game-loop thread once per non-repeat key press, in platform event order, so a press released
  before the next `Update` is seen without text input. `Keyboard.GetState` is unchanged.
- **CNAEXT seams for XNA's Model reader** (TQ-033): `ModelBone::SetParentAndChildren`,
  `Model::setRootEXTProperty`, and `CNA::Content::ObjectValueEXT`, which boxes a `Tag` of any type.
  `SpriteEffect` exposes FNA's `MatrixTransform`, which is set on every `Apply` (TQ-035).
- **SOFTWARE keeps `Alpha8` render targets in their format** (CNA-FIX-L22).
  `SupportsSurfaceFormatAsRenderTargetEXT(Alpha8)` answers true, texels are one byte sampled as
  (0, 0, 0, A), and `GetData` returns the alpha bytes. GPU renderers keep XNA's permitted fallback
  to `Color`.
- **Windows IDE support.** `CMakePresets.json` (now requiring CMake 3.21) gains configure, build
  and test presets:
  - for Ninja + `cl.exe`: `windows-msvc-headless-debug` and
    `windows-msvc-sdlgpu-{debug,relwithdebinfo}`;
  - for Visual Studio 2022: `windows-vs2022-sdlgpu`.

  These presets leave out the C API, networking, Gamer Services, video and Draco.
  `cna_deploy_runtime(<exe>)`, also in the installed C++ SDK, copies SDL3, SDL3_mixer,
  wgpu-native or the MinGW C++ runtime beside an executable. CNA's targets get IDE folders, and
  `.vscode/` recommends CMake Tools.
- **SDK and tools.**
  - The installed C++ SDK names its renderers in `CNA_SDK_GRAPHICS_RENDERER[S]` (CNA-10).
  - The C++ SDK exports `CNA::EmscriptenAsyncify` and `CNA::EmscriptenAbi`, and the C package
    exports `CNA::CApiStaticAsyncify`.
  - `cna_tool_cnb_info --refs0` lists references NUL-delimited, and `--refs` now refuses CR/LF
    names (CNA-FIX-L10).

### Changed

- **C++ programs must be rebuilt.**
  - Layouts or virtual functions change in `GraphicsResource` (so in every graphics resource),
    `GraphicsDevice`, `Effect`, `SpriteEffect`, `DrawableGameComponent`, `GamePadState`,
    `TouchCollection`, `MediaLibrary`, `Song`, `AvailableNetworkSession`, `IPlatform`,
    `IPlatformSurfacePresenter` and native content-writer plugins.
  - The same applies to the audio classes `Cue`, `SoundBank`, `WaveBank`, `AudioEngine`,
    `AudioCategory`, `SoundEffect` and `DynamicSoundEffectInstance`.
  - `ContentManager::Unload()` is virtual again, as in XNA (CONTENT-PARITY-F3).
  - Source changes are marked **Source change** below. The C ABI's layouts are not affected.
- **CNA requires sharp-runtime 0.3.0, easy-gl 0.1.2 and meta-gl 0.4.2**
  (`cmake/DependencyVersions.cmake`). The configure-time check refuses a checkout that declares
  an older minor or patch release.
  - sharp-runtime 0.3.0 is the first release with the .NET Framework 4.0 parity work CNA asked
    for:
    - `BinaryReader` decoding (HND-TQ-16) and `PeekChar` (KI-SHARP-PEEKCHAR);
    - `BinaryWriter`'s character and array overloads (rv-netapi #1/#4);
    - `XmlReader`'s UTF-8 checks (GAP-22) and source locations (HND-TQ-01);
    - `OrdinalIgnoreCase` (TQ-078).
  - sharp-runtime 0.3.0 makes `System::Version`'s data members private. CNA does not use them.
- **The public renderer identity `SDL_GPU` is now `SDLGPU`.** The new spelling is used by the CMake
  selector, runtime name, compile definitions, build options, test and CI selectors, and the C
  constant. `CNA_GRAPHICS_RENDERER_SDLGPU` keeps value 31, and the old spelling is not an alias.
  This took the experimental C ABI to 0.51.0, because the old C constant name was removed. SDL's
  own `SDL_GPU*` API, the `SDL_GPU_DRIVER` hint and the MojoShader SDL_GPU adapter names are
  unchanged.
- **`Game::RunOneFrame` only ticks**, as XNA's does (TQ-037).
  - A host that needs initialization calls `RunFrameEXT()` (C: `cna_game_run_frame_ext`).
  - A profile change that `CanResetDevice` refuses, or a repeated `CreateDevice`, replaces the
    device.
  - `GraphicsDeviceManager::Dispose` removes only its `IGraphicsDeviceService`.
  - `BeginDraw` refuses an inactive fullscreen device.
- **`Game`'s clock and lifecycle follow XNA's IL** (TQ-013, TQ-030, MTH-CONF-004, CNA-FIX-D18):
  - `IsRunningSlowly` follows XNA's catch-up history.
  - `SuppressDraw` needs every Update of a frame.
  - The elapsed-time cap applies before retained debt.
  - No Draw follows `Exit()`, and `Exiting` has a null sender.
  - Every device creation reloads content, and a second `Run` throws.
  - A `GameComponentCollection` enumerator throws after a change.
  - As in FNA, a nonpositive preferred back-buffer size throws `ArgumentOutOfRangeException`.
- **`DrawableGameComponent.Initialize` throws without an `IGraphicsDeviceService`** (TQ-060).
  The component loads when the service has a device, otherwise on `DeviceCreated`, and unloads on
  `DeviceDisposing`. An unpaired `GameWindow.EndScreenDeviceChange` throws.
- **F9 and F10 no longer touch the graphics context in a release build.** `Game` turned every F9
  press into `DebugSimulateContextLoss()` and every F10 into `DebugRestoreContext()`, in every
  build. So a player's F9 recreated a shipped game's GL context, and on the web it left the canvas
  black until F10.
  - The new CMake option `CNA_DEBUG_CONTEXT_HOTKEYS` decides. `AUTO` (the default) compiles the
    hotkeys into the Debug configuration only; `ON` and `OFF` force them in every configuration.
  - The renderer methods stay callable from tests and examples.
- **Content failures keep XNA's exception types** (TQ-053, HND-TQ-16-XNB, TQ-069).
  - Only an `IOException` becomes `ContentLoadException` (`Error loading "<asset>".`, with the
    cause kept).
  - A device refusal or bad LZX data is `InvalidOperationException`.
  - A format outside the profile is `NotSupportedException`
    (`cna_content_manager_load_texture2d` answers `CNA_RESULT_NOT_SUPPORTED`, was
    `CNA_RESULT_IO`).
  - A wrong-type `ReadObject<T>` is `ContentLoadException`, not `std::bad_any_cast`.
  - **Source change:** `ContentLoadException(string, std::exception_ptr)` replaces the
    `const std::exception&` overload.
- **`ContentManager` checks the `OpenStream` result as XNA's `BinaryReader` does**
  (KI-CONTENT-NULLSTREAM, KI-CONTENT-CANREAD, RV-CONTENT-H1). This holds for `ReadAsset`,
  `Load<T>`, `Load<SoundEffect>` and external references.
  - A null stream throws `ArgumentNullException` ("input") instead of `ContentLoadException`.
  - A stream whose `CanRead` is false throws `ArgumentException` ("Stream was not readable.")
    before any read. Before, CNA read it anyway.
  - An `IOException` from `CanRead` or `CanSeek` becomes `ContentLoadException`, as one from `Read`
    does.
- **`ContentManager` follows XNA's names, provider and root** (TQ-004, TQ-045, TQ-047, TQ-078).
  - A null provider throws `ArgumentNullException`. The CNAEXT parameterless constructor is
    unchanged.
  - Overrides and readers see the cleaned backslash name, and `./a` and `a` share one cache entry.
  - A rooted name under a relative root is refused.
  - Cache keys fold case as `OrdinalIgnoreCase`.
  - `RootDirectory` throws after `Dispose`.
- **The texture readers fail where, and with what, XNA's do** (KI-CONTENT-TEXDIM,
  KI-CONTENT-OVERSIZE, KI-CONTENT-REACH3D, RV-CONTENT-LEVELS). They construct the texture after
  the header, then read and set one level at a time. What was `ContentLoadException` is now:
  - a size of zero or less: `ArgumentOutOfRangeException`;
  - a `Texture2D` or `SpriteFont` atlas past the profile's `MaxTextureSize`, a `Texture3D` past 256
    on HiDef, or any `Texture3D` on Reach: `NotSupportedException`, before any level is read;
  - a level past the mip chain: `InvalidOperationException`;
  - a wrong level byte count: `ArgumentException`;
  - a zero level byte count: `ArgumentOutOfRangeException` ("elementCount").

  A negative byte count stays `ContentLoadException`.
- **Content reading and writing follow XNA** (TQ-009, TQ-018, TQ-033, TQ-034, TQ-063).
  - A bad bone reference throws `ArgumentOutOfRangeException`.
  - `DictionaryReader` adds into an existing instance.
  - Importers throw XNA's argument exceptions for an empty filename.
  - A custom writer passes `WriteExternalReference` a compiled `.xnb` name, and Phone content is
    never compressed.
  - `BitmapContent` resizing, the `TextureProcessor` step order and `ModelProcessor` materials
    follow the genuine XNA pipeline, so processed output changes.
  - The writer and XNB codec identities and `TextureProcessor` (version 6) move, so content
    rebuilds once (`docs/downstream-migrations.md`).
- **Graphics objects follow XNA** (TQ-018-SEED-01, TQ-021, TQ-035, TQ-041, TQ-046, TQ-058, TQ-079,
  HND-TQ-12, HND-TQ-25 D9).
  - `ResourceCreated` fires after the most-derived constructor, and `ResourceDestroyed` fires
    before `Disposing`. `Effect` and `OcclusionQuery` raise no `Disposing`.
  - A non-instanced draw with an instance stream, or an instanced draw without one, throws.
  - User-primitive draws unbind vertex buffers and `Indices`.
  - A render target's `IsContentLost` clears on a draw, a `Clear` or a `SetData`, no longer on
    binding.
  - `Effect(GraphicsDevice, byte[])` refuses code with `InvalidOperationException`, not
    `std::runtime_error`.
  - Four stock techniques take their type's name, and stock passes are named `""`, not `P0`.
  - `Effect::Clone` starts on `Techniques[0]`. A stock-effect clone keeps the techniques, passes
    and values, and a base-`Effect` or `EffectMaterial` clone of a stock effect draws with the
    stock shaders.
  - `Model.Draw` needs each mesh's `ParentBone` (there is no bone-0 fallback).
  - `DiscardContents` slots clear to RGBA 68,34,136,255, not black.
  - A `SpriteBatch` is not device-tracked: it raises no resource events and survives its device,
    after which it throws `ObjectDisposedException`.
  - A C route's `NullReferenceException` answers `INVALID_STATE`, not `INTERNAL`.
- **Stock effects behave as XNA's compiled effects** (KI-GFX-STOCK-LATCH,
  KI-GFX-SETVALUE-DISPOSED, KI-GFX-STOCK-DEFAULTS).
  - A draw uses what the last `Apply` latched, so a property set between `Apply` and a draw shows
    only after the next `Apply`. Textures still come from the device's slots.
  - A stock texture parameter refuses a disposed texture (`ObjectDisposedException`) and the active
    render target. A non-texture parameter refuses a texture (`InvalidCastException`).
  - An unwritten parameter reads back zero, not identity or (0, 0, 0, 1).
- **Effects and textures follow XNA** (TQ-028, MTH-CONF-003).
  - A wrong-typed annotation getter throws `InvalidCastException`.
  - `FromStream` zeroes the RGB of alpha-0 texels, as FNA does.
  - `HalfSingle`, `HalfVector2`, `HalfVector4` and `cna_half_*` decode `0x7c00` as 65536, not
    infinity.
- **`SpriteBatch::Begin` takes `const BlendState&`** in the four overloads that took a `BlendState`
  by value, and retains the caller's state as XNA does (CNA-FIX-D23). A Deferred batch observes a
  mutation or disposal of that state at `End`, and Immediate binds the original state at `Begin`.
  **Source change**; the C ABI signatures are unchanged.
- **Font search configuration is per registry** (experimental build-tool interface,
  CNA-FIX-L06).
  - Each `FontDescriptionImporter` snapshots its search directories and `CNA_FONT_PATH`. This is
    importer version 3, so `.spritefont` content rebuilds once.
  - Built-in registration passes `ContentCompiler` `fontDirectories` straight to its registry.
  - `SetFontSearchDirectoriesEXT` now sets synchronized defaults for future importers and
    standalone lookups.
  - `FontSearchDirectoriesEXT()` returns a copied `std::vector` instead of a reference. **C++
    clients of this interface must be rebuilt**; native XNA signatures and the C ABI are unchanged
    ([`docs/content-pipeline.md`](docs/content-pipeline.md)).
- **Audio and XACT refuse what XNA refuses** (TQ-005, TQ-022, TQ-032, TQ-075, TQ-084).
  - `SoundEffect.Play` throws for out-of-range or NaN arguments, instead of returning false and
    clamping.
  - Disposed instances, bad sizes, and `Cue.Apply3D` after an unpositioned play are refused.
  - A bad `AudioEngine` or `SoundBank` file throws `ArgumentException`, as does `SoundBank.GetCue`
    for an unknown name (it was `InvalidOperationException`).
  - A bad global-variable name throws `IndexOutOfRangeException`, and a released engine or bank
    refuses.
  - `AudioCategory` equality uses the engine and the index.
  - In C, a released engine's global variable answers `CNA_RESULT_INVALID_ARGUMENT` (was
    `INVALID_STATE`), and an unknown category answers `CNA_RESULT_INVALID_STATE` (was
    `INVALID_ARGUMENT`).
- **XACT `Cue` follows XNA in four places.**
  - An unknown or inaccessible cue variable throws `IndexOutOfRangeException` ("The specified
    variable index is invalid.") instead of `InvalidOperationException`. The C routes
    `cna_cue_get_variable` and `cna_cue_set_variable` now give the documented
    `CNA_RESULT_INVALID_ARGUMENT`.
  - The built-in cue variables a project authors (`DopplerPitchScalar`, `AttackTime`, ...) start
    at their authored value and keep its range and read-only flag.
  - A disposed cue's `IsStopped` is `false`, like every other state.
  - `Stop(AsAuthored)` neither revives a stopped cue nor restarts a release already under way.
- **A cue started with `SoundBank.PlayCue` plays until it ends.** A still-playing, paused or
  stopping fire-and-forget cue was destroyed five minutes after it started, so music played with
  `PlayCue` stopped after five minutes. XNA's `PlayCue` sets no expiry; now only a finished cue is
  released.
- **`SoundEffect` follows XNA in three places** (RV-SFX-8, RV-SFX-DISPOSE, RV-READER-1).
  - `Play` throws `InvalidOperationException` before the first `FrameworkDispatcher.Update`. A
    `Game` runs one in its constructor, so games and the C API are unaffected.
  - `Dispose` also stops the sounds its `Play` started.
  - A loaded `SoundEffect` reports the `.xnb`'s stored `Duration` (one far from the samples' length
    used to be refused) and an empty `Name`, from `.xnb` and `.cnb` alike.
- **`DynamicSoundEffectInstance` raises `BufferNeeded` as XNA does**, not three times per update
  as FNA does (RV-SFX-BUFFERNEEDED, RV-DSI-STOP).
  - `Play` from Stopped posts one notification when at most two buffers are pending. Each finished
    buffer that leaves at most two pending posts one more.
  - `FrameworkDispatcher.Update` delivers the notifications. `Play` runs no update itself, and
    `PendingBufferCount` drops as buffers finish.
  - `Stop(true)` keeps the posted notifications and posts the flush's own. `Stop(false)` does
    nothing where it used to throw. Only `Dispose` drops notifications.
  - A throwing handler gets its batch again at the next update.
- **`MediaPlayer`, `Song` and `MediaLibrary` follow XNA** (TQ-023, HND-TQ-39, RV-MEDIA-2,
  RV-FIX7-1).
  - `Play(null)`, and empty, disposed or out-of-range collections, throw before the queue changes
    (C: `INVALID_ARGUMENT`).
  - Manual `MoveNext` and `MovePrevious` wrap, and `ActiveSongIndex` clamps.
  - `Song.FromUri` refuses an empty name.
  - `Song.Album`, `Artist` and `Genre` are never null. A song without library metadata, or a
    queued song whose `MediaLibrary` is gone, returns XNA's shared empty objects, also through the C
    API.
  - `MediaLibrary` keeps the very `MediaSource` it is opened on, as a
    `std::shared_ptr<MediaSource>`, and so does `GetAvailableMediaSources()`. **Source change.**
- **Input values follow XNA** (TQ-019, TQ-025, TQ-027, TQ-036, RV-INPUT-4).
  - `TouchCollection` is read-only: mutators throw `NotSupportedException`, the C++ indexer is
    const, and C mutators answer `NOT_SUPPORTED`. It refuses more than eight locations and keeps
    `IsConnected` per snapshot.
  - `TouchLocation.Equals` ignores states.
  - `TouchPanel.IsGestureAvailable` and `ReadGesture` throw `InvalidOperationException` until
    `EnabledGestures` is assigned (`None` included). C answers `CNA_RESULT_INVALID_STATE`.
  - `GamePadButtons` holds only physical buttons. Analog buttons and stick dead zones use XNA's
    integer thresholds, and `GamePadTriggers` compares exactly. An undefined `PlayerIndex` throws
    `InvalidOperationException`.
  - `KeyboardState` drops unnamed keys.
  - Value hashes use XNA's formulas.
- **Network sessions follow XNA's host and event order** (TQ-015, TQ-024, TQ-039, TQ-043, TQ-054,
  HND-NET-01).
  - `IsHost` is per gamer, and `GamerLeft` covers local gamers.
  - `Update` raises events after processing its whole batch.
  - A departing host leaves `Host` null.
  - In Local and SystemLink, `AddLocalGamer` joins at the next `Update`, another local gamer of the
    host's machine succeeds the host, and a signed-out local gamer leaves.
  - A `Local` session takes one gamer.
  - `SendData` validates as XNA does, and `SendData(PacketWriter)` sends `Length`, then clears. A
    null recipient and an empty `PacketWriter` throw.
  - A Reach mipmapped NPOT render target is refused.
- **The Guide refuses until gamer services are initialized** (RV-GUIDE-INIT, RV-GUIDE-INIT-2). The
  refusal is `InvalidOperationException` ("...add a GamerServicesComponent..."), after each call's
  argument checks.
  - It covers the message box, keyboard input, sign-in, marketplace, compose-message, per-player
    game-invite, friends, party, gamer-card, message, player and review screens, plus
    `ShowAchievementsEXT`, `DelayNotifications` and a changed `NotificationPosition`.
  - Before, these opened CNA's dialogs, succeeded, or threw `GamerServicesNotAvailableException`.
  - The C API answers `CNA_RESULT_INVALID_STATE`.
  - `PropertyDictionary` throws `KeyNotFoundException` and `InvalidCastException` instead of
    standard-library exceptions.
- **`LeaderboardKey.BestScoreRecent` and `BestTimeRecent` keep a row for seven UTC days** after it
  was last written, instead of forever. This is a CNA policy, since no XNA source gives Xbox
  LIVE's window.
  - An equal or better score renews the row, and a worse one replaces only an expired best.
  - The local store stamps existing rows once, on migration.
  - The service side is cna-gamer-services-server schema 26
    ([`docs/leaderboard-recent-policy.md`](docs/leaderboard-recent-policy.md)).
- **FNA3D moves to the libcna fork at `76a76cf98544`** (release 26.10 plus CNA's audit fixes),
  from `10f650b9d0f6`, with MojoShader `6f1e91b65c8d` (was `03b686fe1a33`)
  ([`docs/fna3d-dependency-defects.md`](docs/fna3d-dependency-defects.md)).
  - The forks repair OpenGL two-sided stencil (FNA3D-1), SDL GPU upload ordering (FNA3D-4),
    unsigned mip bounds (part of FNA3D-5) and leaked duplicate effect slots (FNA3D-2/-6).
  - ps_1_4 `BEM` and `TEXDEPTH` draw on SPIR-V.
  - The SDL GPU driver clears to opaque black around a destination rectangle that does not cover
    the swapchain.
- **`CNA_EASYGL_COMPILED_EFFECTS` defaults to `ON`** in a fresh configuration whose EasyGL
  identities include `OPENGL33` and neither `OPENGLES3` nor `WEBGL2`. Explicit values and existing
  caches keep theirs.
  - A 43-case corpus checks that each renderer draws a compiled effect or refuses it by name.
  - DIRECTX9's compiled effects bind vertex textures after `Apply`, apply `BUMPENV`, check ps_1_x
    sampler types and refuse predication by name. They stay off by default
    ([`docs/fx-compiled-effects.md`](docs/fx-compiled-effects.md)).
- **EasyGL sends far fewer GL calls per draw** (OPENGLES3, OPENGL33).
  - Stock programs skip uniform writes they already hold (CNA-FIX-L46).
  - Texture units are rebound only when GL may not hold the texture.
  - A vertex buffer keeps its resolved layout across draws, at a cost of 672 bytes more per vertex
    buffer.
  - In the new `cna_easygl_state_benchmark` (OPENGLES3, shared host, identical frame),
    `glBindTexture` calls per frame fell from 544 to 95, `glVertexAttribDivisor` calls from 7,552
    to 256, and the median CPU submit time from 2.65 to 1.84 ms.
- **WEBGPU** no longer copies the back buffer to a staging buffer on every `Present`.
  `GetBackBufferData` captures it on demand and reuses the copy while it is unchanged. Adjacent
  stock sprites with identical state are one native draw.
- **Linking and build.**
  - ELF consumers of `libcna.so` compile with `-fPIC` and link with `-z nocopyreloc -z text`. This
    ends the "different size in shared object" failures and whole-tree relinks (CNA-FIX-L34).
  - CMake refuses `CNA::CApiStatic` beside the C++ SDK at generate time (MTH-PLAT-004).
  - A project that adds CNA with `add_subdirectory` no longer runs CNA's own checks:
    `CNA_PLATFORM_RATCHET` defaults to `ON` only for a top-level CNA, so an overrun in a CNA test
    cannot stop a consumer's configure.
  - An undefined `EASYGL_BUILD_TESTS` follows `CNA_BUILD_TESTS`.
  - Reconfiguring a `CNA_SHARED_LIBRARY` build no longer relinks `libcna.so`.
- **MSVC builds default to the dynamic CRT** for CNA, sharp-runtime and the vendored dependencies,
  unless `CMAKE_MSVC_RUNTIME_LIBRARY` is set first.
  - The vendored SDL is configured with the parent's generator, platform, toolset, Visual Studio
    instance and CRT.
  - Its manifest (now v2) records those settings with the compiler, native builds included, so a
    changed selection no longer reuses an SDL built for another one.
  - Existing vendored SDL builds are rebuilt once.
- **All of CNA's own sources are MS-PL.**
  - 29 CNA-original MIT files (the `CNA::Internal::Net` backend, its tests and harness, among
    others) became MS-PL, and 170 files gained an SPDX line.
  - MIT stays on five copies of other projects: the DirectXMesh-derived face optimizer and four
    cna-gamer-services-server protocol sources.
  - `LicenceHeadersAreMsPl` enforces the rule.

### Fixed

- **Content pipeline matches genuine XNA.**
  - `TextureProcessor`'s step order and resize kernel.
  - 16-bit PNGs are kept as `Rgba64` (refused on Reach).
  - `EffectProcessor` refusal identities.
  - XML and `.spritefont` refusals carry their line and column (HND-TQ-01).
  - The `.x` importer resolves each face corner to the vertex d3dx9's loader picks (210 d3dx9_41
    probes): normals compare by declared value, and texture coordinates and colours take part. A
    malformed `VertexDuplicationIndices` map is `InvalidContentException` (KI-XVERT-WINDOW).
- **Content loading.**
  - LZX with Intel E8 translation decodes; it used to be refused.
  - Reflective readers read value-type members raw (they misread the object), and raw reads reuse
    the manifest's reader.
  - A derived `OpenStream` result's `Close()` runs. An uncompressed container's stream is closed
    twice and an LZX one once, as in XNA.
  - A stream that cannot seek is not asked its `Length` or checked against its declared size.
  - A corrupt LZX frame loses the frames decoded with it in its native buffer (at most 64 KiB), as
    measured on XnaNative 4.0.30901.
  - A texture level count of zero or less creates the texture and reads no level. A partial
    `Texture2D` chain sets only its levels, and the texture readers ignore an existing instance.
  - Enums use their declared width, `DateTime` keeps its `Kind`, and the manifest reads compressed
    `.xnb` files.
  - On MSVC, loose assets no longer fail with "File not found." (CONTENT-MSVC-01).
  - On Android, each APK directory is listed once, not once per miss: `LoadContent` used to take
    over 15 minutes (UB-006).
- **Malformed content is refused instead of crashing.** This covers LZX streams, `.xnb` reference
  cycles and object nesting, `ColorBgraEXT` in version-5 textures, CNJ nesting and integers, CNB
  animation totals, glTF buffers and CUBICSPLINE baking, DDS masks, WAV NaNs and loops, MS-ADPCM
  steps and `Curve` cycle counts.
- **Content building and assets.**
  - glTF unlit materials stay on `BasicEffect`.
  - `SkinnedModelEXT` and `AnimationPlayer` validate skeletons and key order.
  - `SkinnedModelEXT` manifests decode JSON escapes (galaxy-eggbert UB-004). A part name written
    as a `\uXXXX` escape loads, and a malformed manifest throws `ContentLoadException`.
  - Overflowing `BitmapContent` regions behave as measured in XNA, and
    `PixelBitmapContent<Vector3>` converts in a fresh process.
  - A `.spritefont` depends on its system font (manifest 11).
  - Windows content paths are UTF-8.
  - `gltf_to_cnj` checks `unitScale`.
- **Graphics API parity with XNA 4.0 Refresh** (TQ-048, -056, -059, -064 to -066, -068, -070, -071,
  -074, -076, -080, -082, HND-TQ-19a).
  - Draws, buffers, textures, state objects, `EffectParameter`, `SpriteBatch`, `SpriteFont` and
    `OcclusionQuery` refuse in XNA's order, with XNA's types and texts.
  - Edits to the returned `PresentationParameters` no longer reach a parameterless `Reset()`.
  - `GraphicsDeviceManager.RankDevices` ranks devices (it was a no-op), and overrides are honoured.
- **Graphics resources and text** (RV-TEX-1/3/4/6/7, RV-GFXSTATE-9, RV-SPRITE-1).
  - `Texture2D.FromStream` registers its texture with the device.
  - The `Type` index-buffer constructors take `Single` and `Boolean` as 32-bit.
  - A float texel saved as PNG or JPEG rounds half to even.
  - A render target's `IsContentLost` latches a lost or disposed device when read, atomically.
  - Binding and instancing refusals carry XNA's messages.
  - `SpriteFont` and `DrawString` take a character above U+FFFF as two UTF-16 units (two
    `DefaultCharacter` glyphs, or `ArgumentException`), not one `'?'`.
- **Effects** (KI-EFFECT-TEXSLOT, RV-MIX2, RV-LATCH-4, KI-GFX-SPRITEEFFECT-CLONE, KI-GFX-ODE-NAME,
  RV-LATCH-2).
  - A parameter holds one texture whichever `SetValue` overload wrote it, and the C API retains
    that one.
  - `SpriteEffect` reflects FNA's `Texture` parameter and binds it at `Apply`, and its clones carry
    it. A base-`Effect` or `EffectMaterial` clone of `SpriteEffect` draws with the sprite shaders.
  - `ObjectDisposedException` names a disposed effect or texture by its type.
  - The untyped `DrawUser*` overloads keep an applied `VertexColorEnabled = false`.
  - Stock `SetValue`, texture transfers, and `CRTEffect` and `DepthEffect` `Clone` validate their
    arguments.
- **Graphics on every renderer.**
  - `GetBackBufferData` reads through the presentation rectangle on SDLGPU, WEBGPU and DIRECTX9.
  - Deferred sprites see a `SetData` made before `End`.
  - Adapter queries ask the selected renderer, and an unknown capability answers false.
  - One/Zero blends keep non-Add equations.
  - Flipped text crops as XNA does.
  - A resource whose creation handler throws is unregistered.
  - A boxed `Current` of `ModelEffectCollection` and `GamerCollection` is null outside the
    collection.
- **EasyGL.**
  - Stock `SkinnedEffect` fits GLES's uniform floor and draws on Android. A failed stock link
    throws. Highp stock fragments keep the precision of `Single` targets.
  - Custom effects no longer write another effect's program. They recompile atomically and replay
    their uniforms after a context loss, and compiled effects keep partial writes across `Clone`
    and recovery.
  - Volume textures and lost WebGL2 contexts are restored, and mips are restored after a context
    loss.
  - A resource that outlives its device no longer crashes.
  - `MultiSampleAntiAlias = false` works when an ES-default build picks desktop GL.
- **FNA3D and DIRECTX9.**
  - Both: unbound classic stock-effect textures sample opaque black, and `MultiSampleAntiAlias`
    reaches the renderer.
  - FNA3D: a shaderless pass throws instead of crashing. PBR without tangents draws on the SDL GPU
    and D3D11 drivers. Sprites keep the viewport and fractional positions. Float textures, cubes
    and targets, and BC1 cubes, work where the driver implements them. Back-buffer MSAA is
    normalized.
  - DIRECTX9: presentation resets fall back instead of losing the device, rigid `PbrEffect` takes
    skinned layouts, and HLSL support is reported. Float render targets and multi-stream draws work
    (both were refused).
  - `RenderTargetCube` faces bind in MRT sets on DIRECTX9 and WEBGPU.
- **WEBGPU, SDLGPU and HEADLESS.**
  - WEBGPU: sprites honour the depth state, matrix and logical viewport; stencil windings and
    disabled depth tests are right; pipeline keys no longer abort; app-written mips are kept;
    occlusion sums over passes; effect depth and stencil state apply; split skinned and PBR streams
    are accepted.
  - SDLGPU: fog uses fragment alpha, depth formats multisample, pipelines no longer alias, and
    custom-effect sprites honour depth.
  - SDLGPU and WEBGPU sprites keep the transform's W, so perspective transforms no longer draw
    wrongly.
  - HEADLESS tracks cube-target sizes, traces state changes and validates textures.
- **Compiled effects.**
  - The FNA3D and MojoShader fork pins repair legacy shader instructions, sampler inference,
    FOG/PSIZE, SDL GPU sample counts and OpenGL presentation. Truncated headers are refused, not
    asserted (TQ-SWEEP12-7, -8, -10 to -15, TQ-FORK-PIN-01 to -05).
  - On EasyGL and WebGPU, a pass that omits a shader stage keeps the device's shader and constants.
    On `SOFTWARE` (`CNA_SOFTWARE_COMPILED_EFFECTS`) it does so after another compiled pass, not
    after a stock draw.
  - WebGPU and `SDLGPU` use a pixel sampler set after `Apply`, and an `SDLGPU` SpriteBatch draw
    keeps the pass's s0 sampler.
  - WebGPU stops sharing a cached sampler between differing descriptors (`MaxMipLevel` ≥ 256, an
    out-of-range filter or address) and maps an unknown address mode to Wrap.
  - `SOFTWARE` accepts `_bx2` on TEXM3X2PAD and TEXM3X2DEPTH, and samples ps_1_x cube and volume
    textures (RV-STAGE, RV-LATCH-3, RV-MIX*, SW-*).
- **XACT cues play as authored** (GAP-01a/b/c, -02, -03, HND-XACT-05, KI-XACT-EFFECT46).
  - Every track's `PlayWave`, `Stop`, pitch and volume events run at their time; before, only the
    first wave played.
  - Varied waves are selected again for each loop.
  - A finite loop count n plays n+1 passes in every family, as on XACT 3.6; before, the loop ran
    forever.
- **XACT values, limits and banks follow native XACT 3.6 and FAudio** (TQ-XACT-*, HND-XACT-01 to
  -03, RV-CUE-2 to RV-CUE-8, XACT-HEADER-WARN-01).
  - Track RPCs, gain above 1, ±2400-cent pitch, unity category gain, replacement and instance
    counts, and cue preparation follow native XACT.
  - XWB 42/43 banks were read four bytes off.
  - `Cue.Apply3D` writes its three variables in XNA's order and as `SetVariable` does (clamped; an
    inaccessible one throws `IndexOutOfRangeException`).
  - Teardown survives a `Disposing` handler that destroys a sibling or parent, throws, or asks for
    a cue, and a destructor no longer terminates on one.
  - An `AudioCategory` that outlives its engine refuses with `ArgumentException` instead of reading
    freed memory.
  - The public headers no longer warn under `-Wmissing-field-initializers`.
- **Audio and media** (RV-SFX-3, RV-SFX-5-6, RV-SFX-7, RV-SFX-10, RV-MEDIA-1/4/8, HND-TQ-14).
  - Reading `SoundEffect.MasterVolume` opens no output, and `Duration` survives `Dispose`.
  - `Pitch` after `Apply3D` writes the octave ratio alone until the next `Apply3D`.
  - `SoundEffect.FromStream` refuses the wave formats XNA's kernel rejects.
  - An immediate `Stop` before the first `Play` flushes a dynamic instance, and data given to a
    paused one is queued at once.
  - `Microphone` is fixed: a failed `Start` throws and can be retried.
  - Fixed: URI `Song` metadata, read-only `Picture` files (they threw), media hashing, a replayed
    `Video`'s frame texture, and audio refusal texts.
  - `MediaPlayer.Queue` entries keep their songs' duration and metadata. `Play` no longer writes
    the caller's `Song.Duration` or throws for a removed file.
  - A change made in a media event handler, or a throwing handler's batch, is raised at the next
    `Update`.
  - `VideoPlayer` no longer refuses a declared frame rate, and both `PlayPosition`s are whole
    milliseconds.
  - A streaming WaveBank honours its offset. ALSA reopens a vanished device.
- **Math bit-matches XNA**: vector division, axis-angle trigonometry, `Hermite`, `SmoothStep`,
  `Ray`, and `Plane` and frustum classification. Arithmetic order and `ToString` text follow XNA
  (CurrentCulture "G7", packed vectors in hex). Geometry hashes use XNA's formulas.
- **Input and platform** (RV-INPUT-1/2/4, PLATFORM-HEADLESS-MOUSE-01).
  - NaN thumb sticks clamp to +1, `Mouse` coordinates are integral, and `ScrollWheelValue` counts
    fractions of a notch.
  - A polled trigger comes from its byte through XNA's dead zone.
  - Pinch replacement fingers are tracked.
  - An invalid `DisplayOrientation` has XNA's message.
  - Under SDL's offscreen and dummy drivers, `RelativeMouse` and `CursorShapes` are false, and their
    calls throw `PlatformNotSupportedException`.
  - The web runs native `Tick()`'s clock and keeps the fullscreen canvas.
  - SDL3 cameras report removal and denial.
  - Android landscape sensors rotate instead of reflecting.
  - File dialogs complete once.
  - Headless and Terminal refuse a borderless-fullscreen window.
- **Game and device lifecycle.**
  - A refused second `GraphicsDeviceManager` leaves the first intact.
  - Direct device disposal raises `DeviceDisposing`.
  - The asset disk cache keeps its capacity across processes.
- **Storage** (RV-STORAGE-3, RV-STORAGE-7).
  - A path with a control character or one of `" < > |` is refused before the filesystem is
    touched (an embedded NUL let `DeleteDirectory` act on the prefix).
  - Search patterns are normalized as Framework 4 does.
  - Storage path refusals are fixed, and `StorageDevice.DeviceChanged` is raised.
  - `DeleteContainer` reports `UnauthorizedAccessException` on macOS.
- **Gamer services and networking.**
  - Guide: argument and `End*` refusals are fixed. `Guide.EndShowMessageBox` and
    `EndShowKeyboardInput` wait on the result's `AsyncWaitHandle`, and the async handle signals on
    completion. `ShowComposeMessage` counts UTF-16 units.
  - Gamer-property floats compare as .NET does.
  - `PacketWriter(n)` allocates n bytes, and a negative capacity is refused with XNA's text.
  - Service threads stop before OpenSSL's exit cleanup.
  - SystemLink: a joined client no longer answers discovery as a host, which listed unjoinable
    sessions from the third player on (UB-011). QoS probe trains carry an identifier in former
    padding, and a lost probe no longer counts toward the rate. A host reports an upstream train
    within the search and drops an identified train's stragglers (RV-NET-11 to RV-NET-16).
- **C ABI adapter defects.**
  - Buffer `is_content_lost` read false.
  - The resource-device getter missed owned devices.
  - Standalone content managers blocked `cna_game_destroy`.
  - Event injection refused `Chat` combinations.
  - Video and network handles lost their backings.
  - Subscriptions and standalone devices crashed once their game was gone.
  - WEBGPU and FNA3D format refusals answered `INVALID_STATE` instead of `NOT_SUPPORTED`.
  - `cna_effect_material_create` and `cna_effect_clone` keep the C textures their source and
    parameters hold (HND-TQ-25, KI-CAPI-CLONE-RETAIN).
  - An `IndexOutOfRangeException` keeps its canonical type, and `cna_cue_apply_3d` and
    `cna_sound_bank_play_cue_3d` answer `CNA_RESULT_INVALID_ARGUMENT` for an inaccessible spatial
    variable (RV-CAPI-IOOR).
- **Build and packaging.**
  - A consumer of an `OPENGL33` or `WEBGL2` build was told `OPENGLES3` by
    `getCurrentGraphicsRendererName()` (CNA-2).
  - A read-only CNA checkout configures (CNA-9).
  - A shared `CNA_SDL_PREBUILT_ROOT` is not rebuilt for another tree.
  - A moved Linux SDK's SDL3_mixer finds its SDL3.
  - The wasm SDK carries its storage pre-js and C link options.
  - An installed C++ SDK with SDL_shadercross links: the exported link group named the
    build-tree `SDL3_shadercross-static` and now names `CNA::SDL3_shadercross-static`.

### Known limitations

- Pre-release quality remains: a minor release may change the public API, and renderer coverage
  is uneven by design (`docs/<renderer>-renderer.md`).
  - The known defects and limitations are in [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md), which replaced
    `handoff.md` in this release.
  - The deliberate divergences from FNA and XNA are in
    [`docs/xna-fna-behavior-decisions.md`](docs/xna-fna-behavior-decisions.md).
  - `Content` has no general `.xnb` reader.
- The release gate is Linux x86-64. `DIRECTX9`, `WEBGL2`, the Windows (MSVC and MinGW), Apple,
  Android and Emscripten builds are not covered by this release's gate.
- The C ABI release gate stays **Not ready**: its four Windows qualification rows are planned
  ([`docs/c-api/RELEASE_GATE.md`](docs/c-api/RELEASE_GATE.md)).
- Region presentation is unverified on `DIRECTX9`'s pixels, `OPENGLES3` and `WEBGL2`, and FNA3D
  refuses a partial destination (FNA3D-8).
- The XMA encoder path is verified only with stub encoders. The xWMA wave-bank codecs are absent
  where FFmpeg is: Windows, MinGW, Android, Emscripten and iOS.
- The three advisory C API checks `CApiCoverageMatrix`, `CApiLimitations` and `CApiReleaseGate`
  report Skipped. All three stop at one planning-metadata gap: `CBIND-080` owns two planned rows
  but has no `planned_owners` entry.
- The 508 skipped tests break down as follows:
  - 455 are the tests' own renderer and feature conditions. This tree's active renderer is
    `OPENGLES3`, so another renderer's contract skips.
  - 30 need an ASan build's allocator hooks.
  - 4 native dialog tests need their own runner, and 2 tests need `XNA_SAMPLES_ROOT`. All six
    pass with those (see *Verification*).
  - 3 shader-package reproducibility tests need `naga`.
  - 2 EasyGL conformance cases need `CNA_CNAEXT=ON`.
  - 9 are platform conditions: no gyroscope or accelerometer, a Headless platform without an
    OpenGL context, and video already open on another driver.
  - The 3 advisory checks are described above.

### Verification

- `cmake-build-multi` holds `OPENGLES3` as the default renderer plus `OPENGL33`, `WEBGPU`,
  `SDLGPU`, `FNA3D`, `SOFTWARE`, `HEADLESS` and `STUB` in one binary (Debug, tests on). It
  configures with `CNA: version 0.3.0` and one accepted-version line each for sharp-runtime 0.3.0,
  easy-gl 0.1.2 and meta-gl 0.4.2. The incremental build from the tree's state of 2026-11-13 ran
  647 steps with **0 errors and 0 warnings**.
- The complete suite ran on the private display: **15,709 tests, 15,201 passed, 508 skipped,
  0 failed**, in 21.7 minutes at `-j8`. Every skipped test was run again verbosely and its reason
  read (see *Known limitations*).
- The skips that need only a prerequisite pass with it:
  - with `XNA_SAMPLES_ROOT` pointing at the XNA Game Studio samples, all six
    `XnaMathXmlSerializationTest` cases;
  - under `tools/platform/run_message_box_tests_private.sh`, the four native dialog keyboard
    tests.
- Verified at `355f2d390` plus this release's version files, on Debian GNU/Linux 13 with GCC
  14.2.0, CMake 3.31.6, Ninja 1.12.1 and Mesa 25.0.7, with the siblings at their release tags (see
  *Dependencies*).

## [0.2.0] — 2026-10-28

The 0.2.0 release, cut from `next`. It narrows what a program links to what it uses, retires four
more renderer identities and the experimental modern GPU layer, makes `WEBGPU` a supported
renderer, ships an installed C++ SDK, brings PBR, image-based lighting and shadows to the FNA3D,
DIRECTX9 and SOFTWARE renderers, introduces Avatar v3, and follows XNA wherever the ported XNA
demos and the XNA museum measured CNA behaving differently. The experimental C ABI goes from
`0.46.0` to `0.50.0`.

**C++ programs and libraries must be rebuilt against this release** — class layouts, virtual
functions and exception types changed (see *Changed*) — and it requires **sharp-runtime 0.2.0**,
whose own `FileStream` layout changed.

### Dependencies

CNA consumes three separately versioned sibling checkouts with `add_subdirectory`, so checking out
this tag does not select them. This release requires, and was built and tested against:

| Sibling | Required | Verified against |
|---|---|---|
| sharp-runtime (`../sharp-runtime`) | **0.2.0** | [`v0.2.0`](https://github.com/libcna/sharp-runtime/releases/tag/v0.2.0), `1797b51b6e5ed29d2676f2c78f73adccdbbeb2c4` |
| easy-gl (`../easy-gl`) | **0.1.1** | `d911020e8cab69f5ad1e94e3a1d81d83406b4020`: [`v0.1.1`](https://github.com/libcna/easy-gl/releases/tag/v0.1.1) (`f4d6afaf16cdc7e7e73130950bc45e6d865b41bd`) plus one commit that changes only `AGENTS.md` and `CLAUDE.md` |
| meta-gl (easy-gl's `../meta-gl`) | **0.4.1** | `4e9f7b523c935f4159402393bf2412a21b7fd9c6`: [`v0.4.1`](https://github.com/libcna/meta-gl/releases/tag/v0.4.1) (`dd3b30055d55fdc42240de7af248259338256992`) plus one commit that changes only `AGENTS.md` and `CLAUDE.md` |

Only the sharp-runtime requirement moved. `cmake/DependencyVersions.cmake` enforces all three at
configure time, as it did for `0.1.0`, and the installed C++ SDK's `find_dependency(SharpRuntime)`
asks for the same version. Every other dependency is pinned by this repository through submodule
gitlinks and the `GIT_TAG` values in `cmake/ThirdParty*.cmake` and
`cmake/RendererSelection.cmake`; this release moves several of them (see *Changed*).

### Added

- **An installed, relocatable C++ SDK.** `CNA_INSTALL_CXX=ON` installs the configured modules;
  a consumer without the source checkout uses `find_package(CNA CONFIG COMPONENTS …)` and the
  `CNA::` and `SharpRuntime::` targets ([`docs/installed-cpp-sdk.md`](docs/installed-cpp-sdk.md)).
- **C ABI `0.50.0`** (additive): the `SkinnedModelEXT` part diffuse colour routes, a typed
  `CNA_DisplayColorSpace`, glTF cameras in compiled models (`CNA_CnbModelCamera` and five
  `cna_cnb_model_*camera*` routes), and the renderer features `SamplerLevelOfDetailBias`,
  `PbrEffectShading`, `SkinnedPbrEffectShading`, `MultiSampleMask` and `RenderTargetReadback`
  (32–36), which C++ `RendererFeature` gains too. `cna_shader_effect_create` also accepts SPIR-V,
  so a C game can give `SDL_GPU` a custom effect
  ([`docs/c-api/ABI_VERSIONING.md`](docs/c-api/ABI_VERSIONING.md)).
- **PBR on more renderers.** FNA3D renders `PbrEffect`/`SkinnedPbrEffect` (metallic-roughness,
  image-based lighting, double-sided materials, a UV set per map, punctual lights), and so do
  DIRECTX9 and SOFTWARE; all three gain stock-effect shadows and punctual lights in the stock
  effects. DIRECTX9 also gets native cube and volume formats, real render-target mip chains and a
  working `SetStringMarkerEXT`.
- **`SDL_GPU`** draws with a base instance, binds `RenderTargetCube` faces in any slot of a
  multi-target set, samples textures in vertex shaders, and applies a `MultiSampleMask` that keeps
  all samples or none (any other mask throws `NotSupportedException`). Float `Texture3D` formats
  work on `SDL_GPU` and `WEBGPU`.
- **`ContentCompiler` writes `.xnb`** for processed textures (2D, cube and volume), sound effects,
  songs, sprite fonts, compiled effects and video, as XNA's compiler does.
- **glTF cameras** survive into the compiled `Model`, through an optional `.cnb` `MCAM` chunk and
  CNJ cameras ([`docs/cnj-format.md`](docs/cnj-format.md)).
- **XNA API filled in:** `SoundEffect.FromStream(Stream)`, `ResourceContentManager.Load<T>()`,
  `HttpNotificationChannel.ErrorOccurred`, `DisplayMode` equality and hashing, and `XmlSerializer`
  support for `Color`, `Rectangle` and `Point`. New CNAEXT: `PresentationParameters::
  CreateWindowedEXT(width, height)` and a public `SamplerStateCollection::ActiveSamplerCount`.
- **Avatar v3.** Avatar description format 3 (eyebrows, ear shapes, earrings, wristwear, gloves,
  rings, makeup, body proportions and more face controls), avatar catalog v3 through revision 5,
  a redesigned Guide avatar editor with outfits and named shapes, and `demo_avatar`
  ([`docs/avatars.md`](docs/avatars.md)).

### Changed

- **`WEBGPU` is a supported renderer** (`GraphicsRendererMaturity::Supported`, beside `SDL_GPU`)
  instead of experimental, and ranks with the other supported renderers in the automatic fallback
  order. **The fallback tries `HEADLESS` after every renderer that shows a picture**, `SOFTWARE`
  included, and before `STUB`.
- **`new PresentationParameters()` has XNA's defaults**: a 0×0 back buffer and
  `IsFullScreen = true`, not an 800×480 window; `cna_presentation_parameters_init` follows. A
  device built without a `GraphicsDeviceManager` states its size with
  `PresentationParameters::CreateWindowedEXT`; `GraphicsDevice()` and a `Game`'s implicit device
  use it for 800×480 and behave as before.
- **Stock-effect draws are checked against the vertex declaration**, as XNA does — draws from
  vertex buffers and every `DrawUser*` overload. A missing input throws
  `InvalidOperationException` naming it, and a `DrawUser*` call with a non-trivially-copyable
  vertex type no longer compiles.
- **XNA-facing errors throw the matching `System::` exceptions** (`ArgumentException`,
  `InvalidOperationException`, `FileNotFoundException`, …) instead of `std::invalid_argument`,
  `std::out_of_range` or `std::runtime_error`, across math, storage, content, input, net, runtime
  and graphics. A `catch` of the standard type no longer catches them.
- **CNA's headers no longer leak sharp-runtime's `Prop.hpp` macros** (`YES`, `NO`, `nothing`,
  `getter1`, …); code that used them without including the header itself must include it.
- **`StorageContainer` follows XNA:** `CreateFile`/`OpenFile` default to `FileShare::None` and
  honour an explicit `FileShare`, `DeleteContainer` removes only its own player's folder, errors
  use XNA's exception types, and a dangling symlink can no longer reach outside the container.
- **`ContentManager`** refuses a `RootDirectory` change while assets are cached, disposes what it
  loaded on `Unload`, routes `Load<T>` through an `OpenStream` override, and resolves compiled
  references beside the asset. The glTF pipeline builds extracted textures as child assets by
  default and keeps STEP and CUBICSPLINE animation; its component versions moved, so existing
  content rebuilds once.
- **`GraphicsDevice::SetBlendEnabled`** applies the same non-premultiplied blend on every
  renderer, and `SpriteBatch.Begin` with a `ColorMatrixEffect` throws `NotSupportedException`
  where the renderer cannot apply it (everywhere but `SOFTWARE`). EasyGL promises and delivers at
  most 4× MSAA.
- **`SendDataOptions` is a flags enumeration**, so `Chat` combines with `Reliable` and `InOrder`.
- **`CNA_SHARED_LIBRARY` needs CMake 3.30**, and with it `libcna_c_api.so` links `libcna.so`, so a
  program mixing the C and C++ APIs shares one runtime
  ([`docs/c-api/CONSUMING.md`](docs/c-api/CONSUMING.md)).
- **Dependency moves:** FNA3D (release 26.10) and MojoShader are fetched from the libcna forks
  without configure-time patches, SDL moves to `86296ac8`, and ENet is unmodified upstream master
  instead of a patched 1.3.17.

- **A program links only the CNA subsystems it uses.** Mobile Eggbert (textures, sounds,
  `Guide.IsTrialMode`, `Guide.ShowMarketplace`) went from 208 MB to 62.4 MB as a Debug executable
  and from 44.6 MB to 6.2 MB stripped (3.0 MB as a Release build with `CNA_OPTIMIZE_FOR_SIZE`): it
  no longer contains the 30 MB embedded avatar catalog, the avatar system, the system Guide, the
  Gamer Services service client, the Model reader, glTF, Draco, MediaPlayer, Video, Microphone,
  networking, cube and volume textures, the camera, the tray, the `.cnb` writer or the Headless
  and Terminal platforms. Nothing was removed or disabled; a program
  that uses those features links them. See `docs/link-size-architecture.md`. A `Game` and every
  `ContentManager` declare every built-in reader and `.cnb` loader registered, and
  `ContentTypeReaderManager`, `CnbLoaderRegistry` and `GetXnbReaderUsageSummary()` answer queries,
  removals and re-registrations as they did when everything was registered eagerly; each reader
  or loader is installed when its type is first loaded, or when one of those registries is asked
  for it by name, which is what keeps unused importers out of a game. The
  system Guide's panels are installed by `GamerServicesDispatcher.Initialize`, as before
  reachable only by signed-in players.
- **C++ ABI: rebuild against this revision.** The link-boundary work adds virtual functions and
  changes class layouts: `IPlatform::Supports()` and the protected
  `GetCameraService()`/`GetTrayService()` (`GetCamera()`/`GetTray()` are no longer virtual),
  `Effect::IsStockEffectEXT()`, `Texture::AsTextureCubeEXT()`/`AsTexture3DEXT()`,
  `IRenderTargetBindingSource` on the render targets, and `Game`'s members. C++ programs and
  libraries compiled against an earlier CNA are not binary-compatible with it; source that only
  calls these APIs compiles unchanged, and an `IPlatform` implementation renames its two
  overrides. The C API's headers are unchanged.
- **`CNA_ENABLE_GAMER_SERVICES`** builds GamerServices independently of networking; it defaults to
  `CNA_ENABLE_NET`'s value, and `CNA_ENABLE_NET=ON` requires it. **`CNA_OPTIMIZE_FOR_SIZE`**
  compiles CNA into per-function sections and collects unreferenced ones in every program that
  links it. SDL3_image is gone, its `third_party/SDL_image` submodule included: nothing in CNA
  used it, and images are decoded by CNA's own loader as before.
- **With a shared C++ runtime, mold links the same CNA objects as GNU ld and lld.** On ELF,
  `CNA::BuildConfig` names the shared runtime ahead of CNA's archives, so mold no longer extracts
  an unrelated CNA object for a standard-library symbol a shared library needs (a multi-renderer
  2D game linked the avatar catalog through libshaderc). A program whose flags, or the usage
  requirements of a target it links, link the runtime statically (`-static`, `-static-libstdc++`,
  ...) keeps its static runtime, in the configurations in which they do; `-static-libgcc` alone
  does not count. `CNA_CXX_RUNTIME_LINKAGE=STATIC|SHARED` declares a runtime CMake cannot show.
  See `docs/link-size-architecture.md`, "Linker independence".
- `Effect::IsStockEffectEXT()` (CNAEXT) reports XNA's five stock effects without RTTI.
- `Texture::AsTextureCubeEXT()` and `AsTexture3DEXT()` (CNAEXT) answer what a `dynamic_cast` to
  `TextureCube`/`Texture3D` would, without naming either class; the renderers' effect binding uses
  them, so a program that never makes a cube map or a volume links neither.
- The platform's camera and tray are linked only into a program that asks for them:
  `IPlatform::GetCamera()` and `GetTray()` are no longer virtual and create the service on their
  first call, answering as before (the service when the platform supports it, otherwise null). A
  platform implementation overrides the new protected `GetCameraService()`/`GetTrayService()`
  instead. The new `IPlatform::Supports(PlatformCapability)` answers one capability; the runtime
  asks it instead of reading the whole set, and `Game::GetPlatformCapabilitiesEXT()` reads the set
  on its first call (once, thread-safely) rather than during construction, so an SDL3 game that
  never asks about cameras no longer starts SDL's camera subsystem.
- **`GraphicsDevice::GetShaderDialectEXT()` returns the public `CNA::ShaderDialectEXT`**
  (`CNA/ShaderDialectEXT.hpp`) instead of the renderer-internal
  `CNA::Internal::Renderers::ShaderDialectEXT`, which no longer exists; the enumerators and their
  values (0-7) are unchanged, and the C API's `CNA_SHADER_DIALECT_*` identities still match them.
  Code that spelled the internal name -- and included `IGraphicsRenderer.hpp` to get its values --
  includes the public header and writes `CNA::ShaderDialectEXT` instead.
- **The C API's route boundaries are generated.** 3,202 of the 3,210 `cna_*` routes are one spec
  line each in `modules/c-api/spec/*.toml`, generated into `modules/c-api/src/generated/` by
  `tools/c-api/generate_routes.py`. 2,252 of them -- property reads and writes, commands, text
  setters, static queries, value computations, text size/copy pairs, plain destroys -- do all
  their work in that line; the other 950 call a hand-written helper named after the route, which
  holds the real logic (creation bookkeeping, events, bulk transfers) while the generated half
  applies the boundary rules: the exception barrier, null outputs, cleared handle outputs and
  `CNA_Bool` checks. Only the seven `cna_error_*` queries and `cna_get_abi_version` stay written
  whole by hand. The helpers every route shares live in one support layer. See
  `docs/c-api/GENERATED_ROUTES.md`. No export, signature, struct layout, constant or result code
  changed. The wording of many diagnostic messages did (`docs/c-api/ERRORS.md` declares them
  diagnostic-only), and a route given two faults at once now always reports them in one order --
  a null output, then a non-canonical `CNA_Bool`, then a refused argument, then its handles in
  parameter order -- where families used to differ. In builds without `CNA_CNAEXT`, the DebugDraw
  create route now also sets its handle output to `CNA_INVALID_HANDLE` before answering
  `CNA_RESULT_NOT_SUPPORTED`, as every other compiled-out create route already did.
- **The renderer classifications say "renderer".** `CNA::GraphicsBackendCategory` and
  `CNA::GraphicsBackendMaturity` (`CNA/GraphicsBackendCategory.hpp`,
  `CNA/GraphicsBackendMaturity.hpp`) are `CNA::GraphicsRendererCategory` and
  `CNA::GraphicsRendererMaturity` (`CNA/GraphicsRendererCategory.hpp`,
  `CNA/GraphicsRendererMaturity.hpp`), with `get[Current]GraphicsRendererCategory()` and
  `get[Current]GraphicsRendererMaturity()`; in the C API `CNA_GraphicsBackend*`,
  `CNA_GRAPHICS_BACKEND_*` and `cna_graphics_backend_*` are `CNA_GraphicsRenderer*`,
  `CNA_GRAPHICS_RENDERER_CATEGORY_*`/`_MATURITY_*` and `cna_graphics_renderer_*`, and the
  experimental C ABI goes to `0.49.0` (`docs/c-api/ABI_VERSIONING.md` lists every name). They
  classify renderer identities, and the 2026-08 naming normalization had retired "backend" for
  that. Values, answers and the automatic fallback order are unchanged; no alias of an old name
  is kept.
- **The avatar catalogs are a submodule.** `modules/gamer-services/assets/avatars` is the
  [cna-avatar-assets](https://github.com/libcna/cna-avatar-assets) repository, checked out at the
  same path; an existing clone needs `git submodule update --init` once. Catalog v3 revision 5 is
  byte for byte the same, and so is everything built from it.

### Removed

- **Four more renderer identities**, `VULKAN`, `DIRECTX11`, `METAL` and `SDL_RENDERER`, leaving 10
  public identities over 8 implementation families
  ([`docs/removed-renderers.md`](docs/removed-renderers.md)). None is replaced or aliased: `SDL_GPU`
  is the modern native path (SDL owns its Vulkan, Direct3D 12 and Metal backends) and the default on
  Windows, macOS and iOS, `DIRECTX9` stays on Windows, and `OPENGLES3` is the default elsewhere
  outside the browser. A retired name is refused at configure time and at run time (the
  `CNA_GRAPHICS_RENDERER` environment variable, `GraphicsRendererSelection::SetPreferred`, the C
  API's by-name route). Their C ABI values 1, 8, 14 and 42 are permanently reserved and the
  experimental C ABI goes to `0.48.0`; no surviving identity is renumbered. Not carried over:
  `SDL_GPU` compiles `ShaderEffect` out on Windows, Apple and Emscripten and has no occlusion
  queries. (`RendererFeature::BaseInstanceDrawing` was carried over after all: `SDL_GPU`
  implements it since RRC-043.)
- **The retired modern GPU layer**, C ABI `0.47.0`: compute shaders, indirect draws
  (`GraphicsDevice::Draw[Indexed]PrimitivesIndirectEXT`), storage images, GPU timers and texture
  arrays, with their headers, capabilities, renderer features and limits, and the matching C
  constants, structures and routes ([`docs/c-api/ABI_VERSIONING.md`](docs/c-api/ABI_VERSIONING.md)).
- **`CNA::Runtime`, `RuntimeOptions` and `CNA/Misc.hpp`**, which were declared and never
  implemented.
- **Avatar development catalogs 1 and 2.** Catalog v3 is the only catalog CNA ships, and avatar
  descriptions that need catalog 1 or 2 are refused.

### Fixed

- **`cna_assembly_copy_title_ext` refuses a null destination given a capacity.** It copied the
  title through the pointer without checking it, as the size-then-copy protocol in
  `docs/c-api/STRINGS_AND_BUFFERS.md` requires; it now answers `CNA_RESULT_INVALID_ARGUMENT`.
- **Every C API text argument is read by one reader, under the documented UTF-8 rule.** The
  achievement, gamer, gamer-property, guide and XACT routes accepted malformed UTF-8 (they copied
  the bytes unchecked); they now refuse it with `CNA_RESULT_ENCODING` like every other route, as
  `docs/c-api/STRINGS_AND_BUFFERS.md` always required. Text refusals that returned a result without
  recording an error message (CNB, the string marker, SpriteBatch text) now record one.
- **The GraphicsDeviceManager's device information checks its presentation parameters fully.** It
  accepted non-canonical `CNA_Bool` fields and non-zero reserved bytes that the Display routes refuse;
  both now share one check and refuse them with `CNA_RESULT_INVALID_ARGUMENT`.
- **The C API package works on a machine without wgpu-native.** With the `WEBGPU` renderer
  compiled in, `libcna_c_api.so` needs the wgpu-native runtime (`DT_NEEDED libwgpu_native.so`, the
  official release having no SONAME), but the package shipped only SDL3, so an installed consumer
  linked only where wgpu-native happened to be on the library path; and the static interface named
  it as a bare `wgpu_native` no consumer's linker could find. The package now installs the exact
  pinned runtime beside the C API (`lib/`, or `bin/` for the Windows DLL), under the same rule as
  SDL3, and `CNA::CApiStatic` names that installed copy. `CApi_InstalledConsumer` passes for both
  the shared and the static consumer.
- **A game without a `GraphicsDeviceManager` gets a depth buffer.** CNA lets a game run with no
  `IGraphicsDeviceService` — a C API game owns no manager unless it creates one — and gave that
  implicit device `PresentationParameters`' own default, `DepthFormat::None`, so its
  `Clear(color, depth)` was refused since the SOFTWARE-333 contract. The implicit device now
  carries `Depth24`, the default `PreferredDepthStencilFormat` of an XNA `GraphicsDeviceManager`,
  as every XNA game's device does; `new PresentationParameters()` keeps XNA's `None`. Pinned by
  `CApi_GameDefaultDepthBufferSmoke`. Found by cna-c-template 0.1.0's 3D path.
- **XNA parity found by the ported XNA demos and the XNA museum** (`CNA_DEMOS_REPAIR_MATRIX.json`,
  [`docs/xna-fna-behavior-decisions.md`](docs/xna-fna-behavior-decisions.md)): `Curve`, matrix
  decomposition and perspective, `Quaternion`, bounding-volume containment and intersection,
  `Color`, hashing and `ToString` text; XNB, CNB and intermediate-XML reading bounds; `Song`,
  `SoundEffect`, WaveBank and `VideoPlayer` lifetimes and errors; `SpriteBatch` immediate mode and
  upload ordering on `SDL_GPU` and `WEBGPU`; achievements, leaderboards, System Link joins and host
  migration; `Game.ResetElapsedTime` with a fixed time step; and renderer defects in every family.
- **Crashes at exit and on background threads**: C API objects alive at exit, diagnostics after
  thread-local teardown, audio-mixer races, and a playing `Video` destroyed under its decoder.
- The C API builds for Windows with MinGW-w64.

### Known limitations

- Pre-release quality remains: a minor release may change the public API, renderer coverage is
  uneven by design (`docs/<renderer>-renderer.md`), the known defects and limitations are in
  `handoff.md` §5, and `Content` has no general `.xnb` reader.
- The release gate is Linux x86-64. `WEBGPU`'s Supported class rests on Linux and headless Chrome;
  its Windows, macOS and linux-aarch64 wgpu-native packages are still unverified (`WEBGPU-1`).
  `DIRECTX9`, the Apple, Android and Emscripten builds, and the MinGW C API build are not covered
  by this release's gate.
- The 232 skipped tests are the tests' own renderer and profile conditions
  (`WebGPUCompiledEffect*`, `SdlGpuIndexedDrawRangeTest`, `WebGpuWireFrameContract`, …), as in
  `0.1.0`.

### Verification

- `cmake-build-multi` — `OPENGLES3` as the default renderer plus `OPENGL33`, `WEBGPU`,
  `SDL_GPU`, `FNA3D`, `SOFTWARE`, `HEADLESS` and `STUB` in one binary, Debug, tests on —
  configures with `CNA: version 0.2.0` and one accepted-version line for each of sharp-runtime
  0.2.0, easy-gl 0.1.1 and meta-gl 0.4.1. The incremental build from the tree's previous state
  ran 1,855 steps with 0 errors; its 37 warnings are all in seven test sources
  (`-Wunused-result` in six content and GamerServices test files, `-Wenum-compare` in
  `Fna3dCompiledEffectTests.cpp`), none in production code.
- The complete suite on the private display: **12,593 tests, 12,360 passed, 232 skipped,
  1 failed** in 14.2 minutes at `-j8`. The one failure was `CnaProvenanceGate`: the A-09 fixture
  `fx_struct_defaults.fx` had no `PROVENANCE.json` row. The row was added just before this
  release, and the gate and its self-test then passed. Every failure carried by `0.1.0` passes now,
  the three Wine-based `XnaPipelineGenuineRuntime*` tests included.
- Verified on Debian GNU/Linux 13 with GCC 14.2.0, CMake 3.31.6, Ninja and Mesa 25.0.7, with the
  siblings clean: sharp-runtime at its `v0.2.0` tag, and easy-gl and meta-gl at the commits in
  *Dependencies*.

## [0.1.0] — 2026-10-10

The final 0.1.0 release, cut from `next`. The entries below describe what changed since
`0.1.0-alpha.1`. For the first time the sibling checkouts CNA builds against are **required**
rather than only recorded: see *Dependencies*.

### Dependencies

CNA consumes three separately versioned sibling checkouts with `add_subdirectory`, so checking out
this tag does not select them. This release requires, and was built and tested against:

| Sibling | Required | Verified against |
|---|---|---|
| sharp-runtime (`../sharp-runtime`) | **0.1.0** | [`v0.1.0`](https://github.com/libcna/sharp-runtime/releases/tag/v0.1.0), `c20373bae5be1f02d1fe2fa33c4cbba8ad05e4a0` |
| easy-gl (`../easy-gl`) | **0.1.1** | [`v0.1.1`](https://github.com/libcna/easy-gl/releases/tag/v0.1.1), `f4d6afaf16cdc7e7e73130950bc45e6d865b41bd` |
| meta-gl (easy-gl's `../meta-gl`) | **0.4.1** | [`v0.4.1`](https://github.com/libcna/meta-gl/releases/tag/v0.4.1), `dd3b30055d55fdc42240de7af248259338256992` |

`cmake/DependencyVersions.cmake` now enforces this at configure time. Each sibling generates a
`Version.hpp` from its own single-source version, and CNA reads it right after
`add_subdirectory`. A checkout that declares the required version or a later patch release of the
same minor line is accepted. Anything else — another minor line, a pre-release of the required
version, or a checkout too old to declare a version — stops configuration with the `git checkout`
that fixes it. `-DCNA_CHECK_DEPENDENCY_VERSIONS=OFF` downgrades the error to a warning. The check
compares versions, not commits; the commits above record exactly what was verified.

Every other dependency is still pinned by this repository through submodule gitlinks and the
`GIT_TAG` values in `cmake/ThirdParty*.cmake` and `cmake/RendererSelection.cmake`.

### Added

- GamerServices and Net against CNA's own account service (`cna-gamer-services-server`; not Xbox
  LIVE compatible): accounts and a console-style Guide, profiles, friends, presence, messages,
  reviews, achievements, leaderboards with Ranked arbitration, PlayerMatch/Ranked sessions with
  invitations, host migration, `AddLocalGamer` and parties over an authenticated TLS/WSS relay, push
  hints, network voice (optional libopus), and the standard avatar API drawn from original CNA
  catalogs. See `docs/gamer-services-server.md`, `docs/avatars.md` and, for what remains,
  `docs/gamer-services-known-limitations.md`.
- The `RLGL` renderer identity and standalone raylib 6.0 `rlgl.h` dependency baseline, pinned to
  an immutable commit without building or initializing the raylib application framework. The
  GraphicsDevice clear/readback/present/resize slice, all 20 classic Texture2D formats (including
  exact packed, signed-normalized, half/full-float, XNA channel expansion, and DXT1/3/5 with
  native-S3TC or software-decoded storage), and independent XNA sampler objects are
  runtime-validated on Linux. Complete blend, depth/stencil, rasterizer, write-mask, sample-mask,
  scissor, and depth-bias state is also pixel- and native-state-validated. A CNA-owned low-level
  rlgl SpriteBatch path now covers built-in texture and SpriteFont drawing, transforms, sorting,
  blending, clipping, and sampler forwarding. Fixed-capacity rlgl VBO/EBO resources now cover
  static/dynamic vertex data, every classic vertex declaration format, 16/32-bit indices, and
  `None`/`Discard`/`NoOverwrite` updates. Primitive submission covers every indexed/non-indexed
  XNA topology, public bound/user draw routes, semantic-driven declarations, vertex/index/base
  offsets, and WVP transforms. The first shared stock shader path covers unlit BasicEffect and
  AlphaTestEffect with texture/default-white sampling, vertex/material color, all alpha compares,
  fog, and the XNA pixel-center convention; lighting and the remaining effect/resource families
  are under active development in `plans/plan_rlgl.md`. The append-only C
  renderer identity advances the experimental C ABI to 0.27.0.
  **Retired before this release** with the renderer-set curation below; the entry is kept because
  the C ABI passed through 0.27.0 and value 51 is permanently reserved.
- A native Win32 platform backend, selected with `CNA_PLATFORM=WIN32` on Windows targets and
  refused loudly everywhere else. It is built directly on user32/gdi32/opengl32/ole32/shell32 and
  uses no SDL for windowing, events, keyboard, mouse, text input, timing, clipboard, displays or
  dialogs, so `CNA_PLATFORM=WIN32 + CNA_AUDIO_PLATFORM=NULL + CNA_GRAPHICS_RENDERER=DIRECTX11`
  (or `DIRECTX12`) is a supported configuration with no SDL in it at all. The platform and
  renderer axes stay independent: DirectX receives the window through the existing generic
  `NativeWindowHandle`, and no renderer changed. See
  [`plans/plan_win32.md`](plans/plan_win32.md) for the task log.
  **Retired before this release**, with CNA's direct X11 and Wayland platforms (see Removed); the
  entry is kept as the record of what `next` carried in between.

### Removed

- **Twenty-five public renderer identities**, leaving a curated set of 25 over 21 implementation
  families: `BGFX`, `MAGNUM`, `BLEND2D`, `DIRECTX1`, `DIRECTX2`, `DIRECTX3`, `DIRECTX5`,
  `DIRECTX6`, `DIRECTX7`, `DIRECTX8`, `DIRECTX10`, `OPENGLES1`, `OPENGL1`, `OPENGL2`, `WICKED`,
  `SOKOL`, `DILIGENT`, `GLIDE`, `LLGL`, `OPENVG`, `TINYGL`, `IGL`, `PIXIJS`, `NANOVG` and `RLGL`,
  with their implementations, dependencies, patches, CI jobs and documentation
  (`RRC-002`/`RRC-003`, [`docs/removed-renderers.md`](docs/removed-renderers.md)).
  Selecting one is now a configure error naming the identity and its reserved C ABI value, never a
  silent fallback to another renderer. No surviving identity is renumbered: their C ABI values are
  unchanged, the retired values are permanently reserved, and the C ABI is `0.28.0` with
  `CNA_GRAPHICS_RENDERER_MAXIMUM` at 46 (`PORTABLEGL`).
- **The SpriteBatch 2D triangle-mesh entry point**, which the curation above left with no
  implementer: `SpriteBatch::DrawMeshEXT` (a CNAEXT extension, never part of XNA 4.0),
  `ISpriteBatchRenderer::DrawMeshEXT`, and the C route `cna_sprite_batch_draw_mesh_ext` with its
  `CNA_SpriteMeshEXT` structure. It existed for the retired Skia renderer's `SkVertices`/SkSL mesh
  ABI, and after the curation every renderer refused it — the C route could be called but never
  succeeded on any supported renderer. Deleted rather than left as a permanently dead ABI branch
  while the ABI is still experimental; there is no replacement and no compatibility stub. This
  advances the experimental C ABI to `0.29.0`, with exported symbols 4,056 → 4,055 and recorded
  struct layouts 222 → 221 (`RRC-009`).
- **Three more renderer identities**, `DIRECT2D`, `FREEDIRECT` and `PORTABLEGL`, leaving 22 public
  identities over 18 implementation families: their implementation families, the PortableGL
  FetchContent pin, the `../free-direct` sibling checkout, the Direct2D Wine/Proton runners,
  debug-log gates and Windows CI leg, their tests and their documentation
  (`RRC-012`, [`docs/removed-renderers.md`](docs/removed-renderers.md)).
  They are refused by name at configure time like the earlier retirements. `GDI` and `SOFTWARE` are
  unchanged. Their C ABI values 16, 21 and 46 are permanently reserved,
  `CNA_GRAPHICS_RENDERER_MAXIMUM` moves to 44 (`SVG_DOM`), and the experimental C ABI goes to
  `0.31.0`; no surviving identity is renumbered.
- **Four more renderer identities**, `GDI`, `HTML_DOM`, `SVG_DOM` and `OPENGL4`, leaving 18 public
  identities over 14 implementation families: their implementation families, examples and tests,
  the GDI Windows and HTML DOM browser CI jobs, the DOM browser scripts, the OpenGL4 GL-error test
  gate, the GDI-only reduced Software build (`CNA_SOFTWARE_2D_ONLY`) and their documentation
  ([`docs/removed-renderers.md`](docs/removed-renderers.md)). None is replaced: `OPENGL33` stays
  CNA's desktop OpenGL identity with its 3.3 contract, and there is no EasyGL OpenGL 4 profile.
  `SOFTWARE` keeps its full CPU renderer; the Software contracts only GDI's suite had tested moved
  into the Software suite. `ColorMatrixEffect` is now documented as a `SOFTWARE` extension. Their C
  ABI values 18, 33, 40 and 44 are permanently reserved, `CNA_GRAPHICS_RENDERER_MAXIMUM` moves to
  43 (`FNA3D`), and the experimental C ABI goes to `0.33.0`; no surviving identity is renumbered.
- **CNA's direct `WIN32`, `X11` and `WAYLAND` platform implementations**, with the code only they
  used (XKB key tables, the freedesktop.org D-Bus/portal services, Linux evdev controllers, POSIX
  helpers), their tests, desktop-validation harnesses, the standalone platform harness (broken since
  2026-09-28 and used only by the Win32 lanes), Windows validation scripts, CMake discovery
  modules (`cmake/PlatformX11.cmake`, `cmake/PlatformWayland.cmake`), CI cells, spikes and
  capability documents (PLAT-142). **Windows, X11 and Wayland remain
  supported, through SDL3**: SDL3 is CNA's one graphical platform, reaching each window system
  through SDL's own `windows`, `x11` and `wayland` video drivers, and `IPlatformWindow::
  GetNativeHandle()` still hands renderers the `HWND`, `Display*` + XID or `wl_display*` +
  `wl_surface*` (`NativeWindowSystem::Win32/X11/Wayland`, `TryGetWin32/X11/Wayland` unchanged). No
  renderer and no contract header changed. `CNA_PLATFORM=WIN32|X11|WAYLAND` is now a configure
  error that names `SDL3` as the replacement; there is no alias. `CNA_ENABLE_SDL=OFF` remains, as a
  windowless configuration (`HEADLESS`/`TERMINAL`, `NULL` or `ALSA` audio). New `CnaPlatformSdl3X11Tests`
  and `CnaPlatformSdl3WaylandTests` run SDL3 on a private Xvfb and a private headless Weston, and
  the Direct3D 11 window stress fixtures now drive the `HWND` of an SDL3 window.
- **Four more renderer identities**, `DIRECTX12`, `CANVAS`, `OPENGLES2` and `WEBGL1`, leaving 14
  public identities over 12 implementation families (`RRC-018`,
  [`docs/removed-renderers.md`](docs/removed-renderers.md)). None is replaced or aliased:
  `DIRECTX11` and `DIRECTX9` cover Windows, `WEBGL2` and `WEBGPU` the browser, and EasyGL keeps
  `OPENGLES3`, `OPENGL33` and `WEBGL2` without its ES 2.0 profiles and GLSL ES 1.00 lowering. Their
  C ABI values 2, 5, 15 and 17 are permanently reserved and the experimental C ABI goes to
  `0.45.0`; no surviving identity is renumbered.

### Changed

- The root configure no longer builds vendored SDL3 for a configuration that does not use it. The
  gate is conservative — every unset or SDL3-valued axis keeps SDL3, so the default build is
  unaffected — and applies only when the platform, audio and renderer selections have all
  explicitly said otherwise with tests and examples off.

- Capability-gated CNAEXT base-instance drawing through
  `GraphicsDevice::DrawInstancedPrimitivesBaseInstanceEXT`, with the append-only
  `RendererFeature::BaseInstanceDrawing` / C ABI 0.26.0 feature identity and a Vulkan
  implementation verified on RADV and llvmpipe.
- Device-gated Vulkan indirect drawing through both canonical command layouts, including
  compute-generated arguments, non-zero command/geometry/base-instance offsets, automatic command
  visibility and fence-safe deferred argument-buffer lifetime, verified on RADV and llvmpipe.
- Vulkan GPU timing through recycled timestamp query pools, device-derived timestamp periods and
  nonblocking result polling, plus debug-utils pass regions and one-copy structured logger output,
  verified on RADV and llvmpipe.

### Fixed

- **A game can run in a terminal again.** The renderer-set curation retired `BLEND2D`, the only
  renderer that presented CPU frames through `IPlatformSurfacePresenter`, which left
  `CNA_PLATFORM=TERMINAL` with a complete, tested presenter and nothing feeding it — the
  configuration built and displayed nothing. `SOFTWARE` now requests a presenter and hands it each
  finished frame; its framebuffer is already RGBA8, row-major, top row first and tightly packed, so
  the handover costs no conversion and no copy. Whether a CPU renderer is given a window is the
  platform's answer rather than the renderer's — it gets one where the platform reports surface
  presentation and no native window handle, which is a terminal and nothing else today — so
  `SOFTWARE` on a windowing platform, under `HEADLESS`, or with its output piped behaves exactly as
  before. The end-to-end pseudo-TTY test disabled by the curation is enabled and passing
  (`TCR-1`..`TCR-8`).
- **`-DCNA_BUILD_C_API=ON` builds again**, at three independent root causes rather than by
  suppression: the effect collections' `operator[](int)` returns `T*` since XNA's null-index
  semantics were restored, and `CnaCApiEffects.cpp` still took its address; the C API links
  `CNA_GamerServices` unconditionally while `CNA_ENABLE_NET=OFF` does not build it, which is now a
  configure-time refusal naming the option instead of a missing-include failure in every
  translation unit; and both ABI walls still asserted `0.27.0` against a header declaring `0.29.0`.
  With the library buildable, the ABI baseline is measured by its own generator against the real
  `.so` for the first time since the break — 221 struct layouts and 4,055 exported symbols,
  identical to the values recorded by hand, with the ABI unchanged at `0.29.0` — and
  `docs/c-api/RELEASE_GATE.md` regenerates normally instead of publishing a traceback.

### Known limitations

- Pre-release quality remains: a minor release may change the public API, renderer coverage is
  uneven by design (`docs/<renderer>-renderer.md`), the known per-renderer bugs are in `NEXT.md`
  §5, and `Content` has no general `.xnb` reader.
- Three tests fail in the release gate's tree and failed in the same tree before this release
  (its 2026-10-07 run): `CnaInputTests`
  (`KeyboardOrientationTest.LateEnableHonorsDefaultLandscapeAndPreparingParameters`),
  `EasyGL_PresentationMsaaContract`, and `CApi_InstalledConsumer`, which cannot link an installed
  consumer against an external `wgpu_native` (`NEXT.md` §5, item 6). They are carried, not fixed,
  by this release.
- The three `XnaPipelineGenuineRuntime*` tests run the genuine XNA 4.0 runtime under Wine and
  need an X11 display Wine can drive; inside the private Weston/Xwayland runner of
  `tools/platform/run_gpu_tests_private.sh` Wine loads no window driver and they fail. They pass
  under Xvfb.

### Verification

- `cmake-build-multi` — `OPENGLES3` as the default renderer plus `OPENGL33`, `VULKAN`, `WEBGPU`,
  `SDL_GPU`, `FNA3D`, `SDL_RENDERER`, `SOFTWARE`, `HEADLESS` and `STUB` in one binary, Debug,
  tests on — configures with `CNA: version 0.1.0` and one accepted-version line for each of
  sharp-runtime 0.1.0, easy-gl 0.1.1 and meta-gl 0.4.1, and builds its 2,771 steps with 0 errors.
  The log carries 9 warnings, all `ignoring return value` in two test sources
  (`XnaMaterialContentTests.cpp`, `GamerServicesGamerTests.cpp`), present before this release.
- The complete suite on the private display: **11,509 tests, 11,310 passed, 192 skipped,
  7 failed** in 24.5 minutes at `-j8`. The 192 skips are the tests' own renderer and profile
  conditions (`Fna3dCompiledEffect*`, `SdlGpuIndexedDrawRangeTest`, `WebGpuWireFrameContract`, …).
  Of the 7 failures, 3 are the pre-existing ones above, 3 are the Wine display limitation above
  (all three pass under Xvfb, 22–24 s each), and
  `KeyboardGamepadPumpTest.GamePublishesBeforeUpdateAndClearsWhenKeyboardServiceDisappears`
  passed 3 of 3 re-runs in isolation — it is timing-sensitive under the parallel run. No failure
  is new to this release.
- The dependency check was exercised on purpose: a checkout declaring a later patch release is
  accepted; a pre-release of the required version, another minor line, an older patch release and
  a checkout with no version are refused with the `git checkout` that fixes it, and
  `-DCNA_CHECK_DEPENDENCY_VERSIONS=OFF` turns the refusal into a warning.
- Verified on Debian GNU/Linux 13 with GCC 14.2.0, CMake 3.31.6, Ninja and Mesa 25.0.7, with the
  siblings checked out clean at their tags: sharp-runtime `v0.1.0`, easy-gl `v0.1.1`,
  meta-gl `v0.4.1`.

## [0.1.0-alpha.1] — 2026-08-20

First tagged release. CNA has been developed continuously since 2025-02-22; this tag names a
state of `develop` rather than introducing new work, so the entries below describe what the
release contains, not what changed since a previous tag.

### Added

- **XNA 4.0 API surface.** 227 of the 245 public FNA types are present (`Graphics`, `Audio`,
  `Input`/`Touch` and `Storage` complete); `Content` and `Media` are the known gaps. See
  `docs/xna-4-api-coverage.md` at that tag.
- **Graphics.** Every one of the ~26 major `Microsoft::Xna::Framework::Graphics` classes is
  implemented and test-covered, at a qualified ~90% XNA/FNA behavioural compatibility.
- **49 renderer identities** selected at compile time through `CNA_GRAPHICS_RENDERER`, plus the
  opt-in multi-renderer build that chooses between several at runtime
  ([`docs/runtime-renderer-selection.md`](docs/runtime-renderer-selection.md)). `VULKAN`,
  `BGFX`, `FNA3D`, `OPENGL4` and the EasyGL-backed GL family (`OPENGLES3`, `OPENGL33`, `WEBGL1`,
  `WEBGL2`, `OPENGLES2`) are the mature ones; `WEBGPU`, `SKIA`, `SOKOL`, `DILIGENT`, `IGL`,
  `PIXIJS` and the legacy DirectX identities carry documented, narrower capability boundaries.
  (That was this release's set; 25 of those identities were retired after it — see the Unreleased
  section above.)
- **Platform abstraction.** `CNA::Platform::IPlatform` with `SDL3`, `SDL2`, `HEADLESS` and
  `TERMINAL` implementations, on independent CMake axes from the renderer and audio choices
  ([`docs/platform-abstraction.md`](docs/platform-abstraction.md)).
- **Audio, input, media, storage, networking and device extensions** as physical modules under
  `modules/` ([`docs/physical-modules.md`](docs/physical-modules.md)).
- **Compiled XNA effects** — XNB `EffectReader` and Direct3D 9 Effect Framework bytecode
  execution on the `FNA3D` renderer.
- **Experimental native C ABI** (`CNA_BUILD_C_API`, ABI version 0.7.0), versioned independently
  of this product version.
- **`CNA/Version.hpp`** — the release identity generated from the build's single source of
  truth, exposing `CNA::getVersionString()` and the `CNA_VERSION_*` macros.

### Dependency pins

Third-party dependencies are pinned by this repository: the submodules
(`third_party/SDL` `cbe3fbe9f`, `third_party/SDL_image` `fcb9d0b15`, `third_party/SDL_mixer`
`3075d3eda`, `third_party/draco` `8786740086`, `vendor/googletest` `7e2c425db`) through their
gitlinks, and the FetchContent dependencies through the `GIT_TAG` values in
`cmake/ThirdParty*.cmake` and `cmake/RendererSelection.cmake`. Checking out this tag therefore
selects them.

**`sharp-runtime` is the exception and is not pinned by the build.** It is a sibling checkout
consumed with `add_subdirectory` from `../sharp-runtime` (overridable with
`-DCNA_SHARP_RUNTIME_ROOT`), so a build takes whatever revision that checkout happens to be on.
This release was developed and verified against:

    sharp-runtime  625476d5b5fff5fa89f392c3c9af8638ff237692  (develop, 2026-08-19)
    https://github.com/openeggbert/sharp-runtime

Recording the revision here is a stopgap — it documents the pin without enforcing it. A real
mechanism (a configure-time check against a recorded pin, or a submodule) is planned.

### Known limitations

- Pre-release quality: interfaces are expected to change before 1.0, and renderer coverage is
  uneven by design — each renderer's boundary is documented in `docs/<renderer>-renderer.md`.
- Per-renderer bugs and gaps that are known and tracked are listed in `NEXT.md` §5.
- `Content` has no general `.xnb` reader by design, and 14 of the `Media` types are shells.

[Unreleased]: https://github.com/libcna/cna/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/libcna/cna/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/libcna/cna/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/libcna/cna/compare/v0.1.0-alpha.1...v0.1.0
[0.1.0-alpha.1]: https://github.com/libcna/cna/releases/tag/v0.1.0-alpha.1
