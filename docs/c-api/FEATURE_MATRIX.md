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
| Backbuffer | Query logical width, height and format; count/query then copy the complete RGBA8 backbuffer | Draw-time use; HEADLESS honestly returns `CNA_RESULT_NOT_SUPPORTED`; no region or non-Color readback |
| Surface formats | Stable identities for all 27 currently canonical `SurfaceFormat` values | Initial texture transfer accepts only `CNA_SURFACE_FORMAT_COLOR` |
| Texture2D | Create owned Color textures, query width/height/level count/format, upload and read the exact complete level-zero RGBA8 array, destroy | No subrectangle, arbitrary element type or per-mip transfer; mipmapped creation is represented but Color transfer remains level zero |
| SpriteBatch | Create/destroy, begin with all five sort modes, submit an array of rectangle-based textured commands, end | Fixed XNA default blend/sampler/depth/rasterizer state; no custom state, transform, effect or text draw |
| Sprite commands | Destination/source rectangles, RGBA tint, finite rotation/origin/depth and both flip bits | Every command is validated before native submission; every texture must belong to the same game |
| Keyboard | Fresh 256-key snapshot; all 160 canonical `Keys` names; local down/up, pressed count and ascending count/copy helpers | Non-player `Keyboard::GetState()` only; snapshot capture is creation-thread only, copied POD queries are thread-independent |
| Mouse | Fresh logical position, vertical/horizontal wheel and five-button snapshot | Read-only capture; cursor positioning/capture/events remain planned |
| Gamepad | Four player slots; default and explicit three-mode dead-zone capture; connection/packet, all 31 button bits, sticks/triggers; local combined-button and pure normalization helpers | Disconnected slots succeed with rest snapshots; capabilities, vibration and extensions remain planned |
| Touch | Current capabilities and fixed-capacity eight-location collection with previous location and pressure; local find/previous helpers | Platform absence succeeds as disconnected/empty; display, gestures and events remain planned |
| Content | Own a content manager; UTF-8 root count/copy/set; unload cache; load owned Color Texture2D handles; destroy | Create from a callback-scoped device; returned textures outlive manager unload/destruction; no other asset type yet |
| Audio | Create owned mono/stereo PCM16LE effects; duration; owned instances; play/pause/resume/immediate or release-tail stop; volume/pitch/pan/loop/state; destroy | Creation-thread control; instances before effect before game; no device maps to `NOT_SUPPORTED`; no file/content, streaming, microphone, XACT or 3D route yet |
| Values | ABI layouts for color, `Vector2` fields and `Rectangle` fields | Vector/rectangle methods, operators and named members are not yet mapped |

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
| PCM sound creation, mixer state transitions, threading and parent order | SDL dummy audio tested | SDL dummy audio/video tested | Audio behavior is renderer-independent; physical devices not C-tested |
| SpriteBatch validation, state and lifetime | Tested | Tested | Not yet C-tested |
| Observable SpriteBatch pixels | No raster backbuffer | Exact uploaded red/green/blue texels and clear pixel tested | No initial C evidence |
| Full RGBA8 backbuffer readback | `CNA_RESULT_NOT_SUPPORTED`, destination unchanged | Tested before presentation | Depends on the selected native backend; not yet C-tested |

An enumerated renderer identity is not a support claim. Applications must query capabilities and
handle `CNA_RESULT_NOT_SUPPORTED`; future renderer work must add renderer-appropriate C evidence
before this table claims support.

## Ownership and call context

| Object/value | Lifetime rule |
|---|---|
| Game | Owned handle; destroy exactly once on its creation thread |
| Callback game | Borrowed for the active callback; callback code must not destroy it |
| Graphics device | Borrowed from the callback game and invalid immediately after that callback |
| Texture2D | Owned child that survives callbacks; destroy before its game |
| SpriteBatch | Owned child that survives callbacks; destroy before its game |
| Submitted texture | Retained by an active batch until successful `End` or batch destruction; destruction while retained is refused |
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
- complete math/value APIs, operators, constants and string conversions;
- remaining window, platform, service, event and runtime APIs;
- player-indexed keyboard capture, input mutation/events, gamepad control/capabilities/extensions
  and touch display/gesture/event APIs;
- 3D resources and draws, vertex/index buffers, models, meshes, effects and shaders;
- render targets, occlusion queries and remaining graphics-device/presentation operations;
- non-Color texture transfers, texture regions, mip-level transfer and additional texture types;
- custom SpriteBatch states, matrices, effects, `SpriteFont` and text drawing;
- advanced and renderer-specific CNA extensions not listed in the implemented table.

Until the generated inventory and completion gates in `plan_binding.md` are finished, any public
CNA symbol not explicitly represented in [`COVERAGE.md`](COVERAGE.md) is unimplemented.
