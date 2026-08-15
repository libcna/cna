# CNA C API 0.1 Feature Matrix

## Scope

This matrix describes the implemented experimental `0.1.0` C ABI. It is a usable initial 2D
vertical slice, not complete CNA or XNA coverage. [`COVERAGE.md`](COVERAGE.md) remains the
source-symbol mapping record and will grow into the complete public-surface inventory.

All declarations are available through the C17 umbrella header `CNA/C/cna.h`. Public structures
use fixed-width C fields and versioned structure prefixes where evolution is expected. All
fallible calls return `CNA_Result`; no C++ or Sharp Runtime type or exception crosses the ABI.

## Implemented surface

| Family | Implemented operations | Current boundary |
|---|---|---|
| ABI | Encoded ABI query through `cna_get_abi_version` | Experimental version `0.1.0`; no stability promise before the release gate |
| Errors | Per-thread result, category and UTF-8 diagnostic count/copy | Diagnostics are replaced by the next failed ordinary call; programs branch on `CNA_Result` |
| Game | Create one active game, run one frame, run until exit, request exit, clear, set UTF-8 window title and destroy | Creation-thread only; run and destroy are non-reentrant; a second active C-owned game is refused |
| Lifecycle | Load, update, draw, unload and exit callbacks with copied callback table, context and optional copied UTF-8 failure message | Callbacks are synchronous; a callback failure becomes `CNA_RESULT_CALLBACK` |
| Timing | `CNA_GameTime` snapshots with total/elapsed 100-nanosecond ticks and running-slowly flag | Present only for update and draw callbacks |
| Graphics discovery | Borrow callback-scoped device; query renderer identity/name, maximum 2D texture dimension and 13 capability flags | Device handle expires when the callback returns; renderer selection remains compile-time |
| Graphics resources | Generic device/disposed queries, exact UTF-8 Name/ToString count-copy, C-owned 64-bit tag, explicit disposal and synchronous disposing subscription for supported resource handles | Supports Texture2D, RenderTarget2D, RenderTargetCube and VertexDeclaration; tags do not expose native `System::Object*`; creation-thread only |
| Graphics states | Complete BlendState, DepthStencilState, RasterizerState and SamplerState POD descriptors; native presets; device get/set; 16 pixel and vertex sampler slots | Applying an otherwise valid state may fail when the compiled backend cannot represent it; Effect and transform-matrix SpriteBatch variants remain planned |
| Display and presentation | DisplayMode initialization/equality; adapter metadata, UTF-8 strings, modes, preferences, profile/format queries; PresentationParameters init/clone/bounds/device round-trip | Adapter indices are point-in-time; native monitor/window handles are deliberately not disclosed; active-device adapter refresh returns `NOT_SUPPORTED` |
| Backbuffer | Query logical width, height and format; count/query then copy the complete RGBA8 backbuffer | Draw-time use; HEADLESS honestly returns `CNA_RESULT_NOT_SUPPORTED`; no region or non-Color readback |
| Surface formats | Stable identities for all 27 currently canonical `SurfaceFormat` values | Initial texture transfer accepts only `CNA_SURFACE_FORMAT_COLOR` |
| Texture2D | Create owned Color textures, query width/height/level count/format, upload and read the exact complete level-zero RGBA8 array, destroy | No subrectangle, arbitrary element type or per-mip transfer; mipmapped creation is represented but Color transfer remains level zero |
| Render targets | Create/query/destroy owned 2D and cube targets; singular/MRT binding; active binding count/copy; RenderTarget2D accepted by Texture2D routes | Backend storage availability is explicit; HEADLESS binding returns `NOT_SUPPORTED`; bound targets cannot be destroyed; current ContentLost invariant is false |
| SpriteBatch | Create/destroy, begin with all five sort modes or explicit blend/sampler/depth/rasterizer state, submit an array of rectangle-based textured commands, end | No transform, Effect or text-draw submission yet |
| Sprite commands | Destination/source rectangles, RGBA tint, finite rotation/origin/depth and both flip bits | Every command is validated before native submission; every texture must belong to the same game |
| SpriteFont | Build from a retained Texture2D/RenderTarget2D plus copied UTF-16 glyph/rectangle/kerning table; properties; character copy; UTF-8 measure; destroy | Caller creates the atlas/glyph table; no SpriteBatch text-draw command yet; source texture must outlive the font |
| Keyboard | Fresh 256-key snapshot; all 160 canonical `Keys` names; local down/up, pressed count and ascending count/copy helpers | Non-player `Keyboard::GetState()` only; snapshot capture is creation-thread only, copied POD queries are thread-independent |
| Mouse | Fresh logical position, vertical/horizontal wheel and five-button snapshot | Read-only capture; cursor positioning/capture/events remain planned |
| Gamepad | Four player slots; default and explicit three-mode dead-zone capture; connection/packet, all 31 button bits, sticks/triggers; local combined-button and pure normalization helpers | Disconnected slots succeed with rest snapshots; capabilities, vibration and extensions remain planned |
| Touch | Current capabilities and fixed-capacity eight-location collection with previous location and pressure; local find/previous helpers | Platform absence succeeds as disconnected/empty; display, gestures and events remain planned |
| Content | Own a content manager; UTF-8 root count/copy/set; unload cache; load owned Color Texture2D handles; destroy | Create from a callback-scoped device; returned textures outlive manager unload/destruction; no other asset type yet |
| Audio | Probe real playback availability; create owned mono/stereo PCM16LE effects; duration; owned instances; play/pause/resume/immediate or release-tail stop; volume/pitch/pan/loop/state; destroy | Availability is a successful versioned snapshot; creation-thread control; instances before effect before game; no device maps resource creation to `NOT_SUPPORTED`; no file/content, streaming, microphone, XACT or 3D route yet |
| Values and 3D identities | ABI layouts for Color, Point, Vector2/3/4, Quaternion, Matrix, Plane, Ray, bounding volumes, CurveKey, all 17 PackedVector raw values, VertexElement and all seven built-in `VertexPosition*` values; all MathHelper constants/scalar operations; complete Point/Rectangle, Vector2/3/4, Quaternion, Matrix, Plane, Ray, bounding-volume, CurveKey, CurveKeyCollection, Curve, Color, PackedVector, built-in vertex equality/hash/text and VertexElement operations; all 141 named colors; all three half-conversion routes; canonical built-in GPU stride/element queries; curve loops/evaluation/tangents; transforms/factories/decomposition, caller-capacity corners and explicit optional intersections; stable containment, plane, curve, packed-format, buffer, index, primitive, SetData and vertex identities | The canonical BoundingFrustum boundary-origin ray branch is callable but returns `NOT_SUPPORTED`; vertex/index resources and 3D draw behavior remain planned |
| Vertex declarations and bindings | Own empty, computed-stride or explicit-stride declarations; query exact type name/stride and count/copy elements; fixed default/initialized vertex-buffer binding descriptor | Declarations are standalone creation-thread handles; buffer binding tokens become type/generation validated when consumed after vertex buffers land; no draw submission yet |

The complete exported-function list is mechanically checked so the shared library exposes only
`cna_*` symbols. Pure C and C++ header translation tests freeze the implemented value layouts and
numeric identities.

## Renderer evidence

The C API reports the backend compiled into CNA; it does not choose or emulate a renderer at run
time. The initial slice currently has this automated evidence:

| Behavior | HEADLESS | SDL_RENDERER | Other renderer identities |
|---|---|---|---|
| Game lifecycle, errors, handles and threading | Tested | Tested | Not yet C-tested |
| Renderer identity/name/capability discovery | Tested | Tested | Enumerated, not yet C-tested |
| Color Texture2D upload and exact readback | Tested | Tested | Not yet C-tested |
| Content Texture2D load, cache/unload lifetime and exact decoded pixel | Tested | Tested | Not yet C-tested |
| Keyboard, mouse, gamepad and touch capture/thread rules | Tested | Tested with SDL dummy video | Other platform/device combinations not yet C-tested |
| Pure gamepad dead-zone/button and touch-location helpers | Tested | Tested | Renderer-independent copied POD operations |
| Audio availability, PCM creation, mixer transitions, threading and parent order | Available snapshot plus SDL dummy audio tested | Available snapshot plus SDL dummy audio/video tested | Audio behavior is renderer-independent; physical devices not C-tested |
| Unavailable audio device and shutdown after repeated probes/creation failures | Successful unavailable snapshots in isolated invalid-driver process | Successful unavailable snapshots in isolated invalid-audio/dummy-video process | Exact driver availability is platform-specific |
| SpriteBatch validation, state and lifetime | Tested | Tested | Not yet C-tested |
| Graphics state values, device state/sampler round-trip and explicit-state SpriteBatch begin | Tested | Tested with supported opaque blend state; unsupported backend state paths preserved | Other renderer identities not yet C-tested |
| Display modes, adapter snapshots/queries and PresentationParameters | Tested | Tested with SDL dummy video | Native handle disclosure and unsafe live refresh are explicit callable limitations |
| RenderTarget2D/RenderTargetCube creation and binding contract | Creation/property/lifetime tested; bind returns `NOT_SUPPORTED` | Real RenderTarget2D binding and unavailable cube path tested | Capability and native allocation remain backend-specific |
| SpriteFont glyph/properties/UTF-8 measurement and source-texture lifetime | Tested | Tested | Renderer-independent layout/measurement over a game-owned texture |
| Built-in vertex values, strings and packed GPU declarations | Tested | Tested | Renderer-independent copied POD and declaration operations |
| Owned vertex declarations and binding descriptors | Tested | Tested | Renderer-independent copied arrays and standalone handle lifetime |
| Common graphics-resource name/tag/device/disposal/event contract | Tested | Tested | Generic contract is renderer-independent; device identity is callback-scoped |
| Observable SpriteBatch pixels | No raster backbuffer | Exact uploaded red/green/blue texels and clear pixel tested | No initial C evidence |
| Full RGBA8 backbuffer readback | `CNA_RESULT_NOT_SUPPORTED`, destination unchanged | Tested before presentation | Depends on the selected native backend; not yet C-tested |

An enumerated renderer identity is not a support claim. Applications must query graphics, touch
and audio capabilities and handle `CNA_RESULT_NOT_SUPPORTED`; future platform work must add
appropriate C evidence before this table claims support.

## Ownership and call context

| Object/value | Lifetime rule |
|---|---|
| Game | Owned handle; destroy exactly once on its creation thread |
| Callback game | Borrowed for the active callback; callback code must not destroy it |
| Graphics device | Borrowed from the callback game and invalid immediately after that callback |
| Graphics-resource event registration | Owned handle; callback/context remain caller-owned until unsubscription; may be unsubscribed after resource destruction |
| Texture2D | Owned child that survives callbacks; destroy before its game |
| SpriteBatch | Owned child that survives callbacks; destroy before its game |
| Submitted texture | Retained by an active batch until successful `End` or batch destruction; destruction while retained is refused |
| RenderTarget2D / RenderTargetCube | Owned game child; unbind and destroy before game; RenderTarget2D also satisfies Texture2D C operations |
| SpriteFont | Owned game child; destroy before game and before its retained source Texture2D/RenderTarget2D |
| Game callbacks/context | Table is copied; function pointers and context remain caller-owned and valid through game destruction |
| POD values and output buffers | Caller-owned; inputs are copied for the call and no output API writes a partial array |
| Keyboard snapshot | Independent copied POD; it has no handle or game lifetime after capture |
| Mouse/gamepad/touch snapshot | Independent copied POD; it has no handle or game lifetime after capture |
| ContentManager | Owned game child; destroy before game; unload/destroy does not destroy issued C resources |
| Content-loaded Texture2D | New owned game child per successful load; remains valid after manager unload/destruction |
| SoundEffect | Owned game child; copied PCM survives input buffer; destroy after all instances and before game |
| SoundEffectInstance | Owned effect/game child; destroy before effect; no C callback/context is retained by mixer |

Game, graphics, texture, batch, content and snapshot-capture calls use the game creation/graphics
thread. Pure keyboard/gamepad/touch snapshot and normalization helpers are documented for any
thread.

## Error and capacity behavior

- Null pointers, invalid structure prefixes, reserved fields, enum values and numeric ranges return
  `CNA_RESULT_INVALID_ARGUMENT`.
- Null, stale, wrong-kind and expired borrowed handles return `CNA_RESULT_INVALID_HANDLE` or the
  documented state failure for an out-of-context borrow.
- Wrong-thread native operations return `CNA_RESULT_THREAD` before touching thread-affine state.
- Unsupported renderer or initial-slice functionality returns `CNA_RESULT_NOT_SUPPORTED` when a
  callable operation reaches that native limitation.
- Count/copy APIs always report the required count. Insufficient capacity returns
  `CNA_RESULT_BUFFER_TOO_SMALL` and does not write a prefix.
- Native exceptions are translated at the boundary. Unexpected native failures become
  `CNA_RESULT_INTERNAL`; no exception crosses into C.

## Intentionally unavailable in 0.1

The following families are planned work, not implicitly supported and not permanent exclusions:

- remaining content asset types, custom readers, manifests and content extensions;
- remaining audio (file/content loading, fire-and-forget, globals/3D, streaming, microphone and
  XACT), plus media and video;
- remaining window, platform, service, event and runtime APIs;
- player-indexed keyboard capture, input mutation/events, gamepad control/capabilities/extensions
  and touch display/gesture/event APIs;
- 3D resources and draws, vertex/index buffers and consumption of the completed declaration/
  binding values, models, meshes, effects and shaders;
- occlusion queries and remaining graphics-device operations;
- non-Color texture transfers, texture regions, mip-level transfer and additional texture types;
- SpriteBatch matrices, effects and text drawing;
- advanced and renderer-specific CNA extensions not listed in the implemented table.

Until the generated inventory and completion gates in `plan_binding.md` are finished, any public
CNA symbol not explicitly represented in [`COVERAGE.md`](COVERAGE.md) is unimplemented.
