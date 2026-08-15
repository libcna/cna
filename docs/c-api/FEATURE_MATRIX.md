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
| Graphics resources | Generic device/disposed queries, exact UTF-8 Name/ToString count-copy, C-owned 64-bit tag, explicit disposal and synchronous disposing subscription for supported resource handles | Supports Texture2D, Texture3D, TextureCube, RenderTarget2D, RenderTargetCube, VertexDeclaration, VertexBuffer, IndexBuffer and Effect; tags do not expose native `System::Object*`; creation-thread only |
| Graphics states | Complete BlendState, DepthStencilState, RasterizerState and SamplerState POD descriptors; native presets; device get/set; 16 pixel and vertex sampler slots | Applying an otherwise valid state may fail when the compiled backend cannot represent it; Effect and transform-matrix SpriteBatch variants remain planned |
| Display and presentation | DisplayMode initialization/equality; adapter metadata, UTF-8 strings, modes, preferences, profile/format queries; PresentationParameters init/clone/bounds/device round-trip | Adapter indices are point-in-time; native monitor/window handles are deliberately not disclosed; active-device adapter refresh returns `NOT_SUPPORTED` |
| Backbuffer | Query logical width, height and format; count/query then copy the complete RGBA8 backbuffer | Draw-time use; HEADLESS honestly returns `CNA_RESULT_NOT_SUPPORTED`; no region or non-Color readback |
| Surface formats | Stable identities and Texture block/byte-size/alignment helpers for all 27 currently canonical `SurfaceFormat` values | HEADLESS and SDL_RENDERER create Color Texture2D resources; the larger Skia-native promoted set is exposed but not yet C-runtime-tested |
| Texture / Texture2D | Standalone and game-owned default/device/file/RGBA8/CPU-only/encoded-memory creation; common and 2D properties; all 18 native typed full/mip/rectangle transfer representations; exact type/storage queries; PNG/JPEG count/copy and file output; destroy | Native format/backend gates remain authoritative; SDL_RENDERER rejects compatible mip uploads above level zero as `NOT_SUPPORTED`; streams and renderer/weak pointers do not cross the ABI |
| Texture3D / TextureCube | Owned Color resource creation; dimensions/common state; complete Color full/mip/box or six-face/rectangle transfer descriptors; raw Texture3D byte upload; exact type queries; DDS memory cube factory; RenderTargetCube inheritance; destroy | HEADLESS and SDL_RENDERER truthfully reject Texture3D creation and cube transfer/storage as `NOT_SUPPORTED`; cube construction itself may succeed without storage as in native CNA; renderer pointers and streams do not cross the ABI |
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
| Values and 3D identities | ABI layouts for Color, Point, Vector2/3/4, Quaternion, Matrix, Plane, Ray, bounding volumes, CurveKey, all 17 PackedVector raw values, VertexElement and all seven built-in `VertexPosition*` values; all MathHelper constants/scalar operations; complete Point/Rectangle, Vector2/3/4, Quaternion, Matrix, Plane, Ray, bounding-volume, CurveKey, CurveKeyCollection, Curve, Color, PackedVector, built-in vertex equality/hash/text and VertexElement operations; all 141 named colors; all three half-conversion routes; canonical built-in GPU stride/element queries; curve loops/evaluation/tangents; transforms/factories/decomposition, caller-capacity corners and explicit optional intersections; stable containment, plane, curve, packed-format, buffer, index, primitive, SetData, vertex and effect-parameter class/type identities | The canonical BoundingFrustum boundary-origin ray branch is callable but returns `NOT_SUPPORTED`; remaining 3D/effect resource and draw behavior remains planned |
| Vertex declarations, buffers and bindings | Own empty, computed-stride or explicit-stride declarations; own static/dynamic buffers; query metadata/type/declaration; complete seven-type count/window SetData/GetData, raw upload, four dynamic option overloads, ContentLost registration and fixed default/initialized binding descriptors | Buffers are game children and creation-thread affine; WriteOnly readback is `NOT_SUPPORTED`; ContentLost is currently never raised; binding tokens are generation/type validated when later consumed by graphics-device draw state; no draw submission yet |
| Index buffers | Own static/dynamic 16- or 32-bit buffers; query metadata/type; complete count/window SetData/GetData; all dynamic streaming options; ContentLost registration | Buffers are game children and creation-thread affine; width conversion is rejected; WriteOnly readback is `NOT_SUPPORTED`; ContentLost is currently never raised; no indexed draw submission yet |
| Effects, metadata, parameters, techniques and passes | Own base-adapter, EffectMaterial, ShaderEffect, SpriteEffect, BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, ColorMatrixEffect, PbrEffect and SkinnedPbrEffect game children; same-type clone/dispose/apply; reusable matrix/fog/light interfaces; stable directional lights; complete concrete material, color-transform, five-slot PBR and 72-bone skinning state; current parameter/technique/pass views; exact type/source strings; shader validity/uniforms/textures; fixed parameter identities and complete metadata/value/collection operations | Compiled XNA `.fx` bytecode is explicit `NOT_SUPPORTED`; custom-shader renderer availability is queried; renderer pointers stay private; descendants retain the native effect/game/texture lifetime after parent-handle destruction |
| Model bones | Own default/named bones; exact UTF-8 names, signed indices and copied transforms; add retained child relationships; query optional parent and live children; create empty collections; count/index/find/contains | Standalone and hierarchy views are creation-thread affine; self/ancestor cycles are rejected; mesh, model and animation aggregation remains planned |

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
| Complete static/dynamic vertex-buffer contract | All typed/raw transfers, metadata, WriteOnly, disposal/events and lifetime tested | Native no-3D capability and atomic creation refusal tested with SDL dummy video | Successful buffer behavior remains native-backend-specific beyond HEADLESS |
| Complete static/dynamic index-buffer contract | Both widths, count/window transfers, all streaming options, metadata, WriteOnly, disposal/events and lifetime tested | Native no-3D capability and atomic creation refusal tested with SDL dummy video | Successful buffer behavior remains native-backend-specific beyond HEADLESS |
| Effect annotation values and collections | All typed/default values, metadata, strings, collection snapshots, ownership and thread/error paths tested | Same renderer-independent strict-C suite tested | Renderer-independent copied metadata operations |
| Effect parameter values and collections | All scalar/array/string/matrix-transpose overloads, defaults, metadata, nesting, stable collection aliases, texture dispatch/retention and thread/error paths tested | Same renderer-independent strict-C suite tested | Parameter behavior is renderer-independent; non-null retention is proven with standalone Texture2D |
| Effect techniques, passes and collections | Default/named construction, canonical P0, identities, Apply dispatch, nesting, stable aliases and thread/error paths tested | Same renderer-independent strict-C suite tested | Ownerless Apply is a native no-op; effect-owned current-technique validation is tested by `EffectSmoke.c` |
| Effect lifecycle, material, source shader and stock sprite | Construction/cloning/disposal, current technique/pass validation, exact type/source strings, all shader uniform/matrix calls, texture retention and descendant lifetime tested | Same strict-C suite with SDL dummy video | HEADLESS/SDL have no valid custom shader program; creation and explicit renderer-state queries remain deterministic |
| BasicEffect, DirectionalLight and effect interfaces | Exact defaults, every matrix/fog/light/material/texture operation, all three shared lights, default lighting, cloning, retention and descendant lifetime tested | Same strict-C suite with SDL dummy video | Property behavior is renderer-independent; GpuDrawParams stays behind Apply/draw paths |
| AlphaTest, DualTexture and EnvironmentMap effects | Exact defaults/types, all generic and concrete properties, unclamped reference alpha, enum/bool/index errors, always-on lighting, Texture2D/TextureCube cloning and retention, Apply and nested-light lifetime tested | Same strict-C suite with SDL dummy video | Property behavior is renderer-independent; renderer-only draw parameters stay behind Apply/draw paths |
| SkinnedEffect | All 72 identity defaults, bounded palette transfer/capacity, weights, complete generic/material/extension state, always-on lighting, texture clone retention, Apply and nested-light lifetime tested | Same strict-C suite with SDL dummy video | Property and palette behavior is renderer-independent; renderer-only bone upload stays behind Apply/draw paths |
| ColorMatrix and PBR extension effects | Identity/custom/grayscale/reset color transforms, non-finite validation, exact PBR defaults, every generic/material property, all five texture slots, clone retention, 72-bone SkinnedPbr palette, Apply and nested-light lifetime tested | Same strict-C suite with SDL dummy video | Extension-only APIs are identified as CNA features; renderer-specific draw parameters remain behind Apply/draw paths |
| ModelBone and ModelBoneCollection | Both constructors, UTF-8/capacity, indices/transforms, live parent/children aliases, count/index/name/contains, hierarchy lifetime, cycle and handle/thread errors tested | Same renderer-independent strict-C suite tested | Standalone hierarchy behavior is renderer-independent |
| Common graphics-resource name/tag/device/disposal/event contract | Tested | Tested | Generic contract is renderer-independent; device identity is callback-scoped |
| Complete Texture/Texture2D contract | All constructors, Color full/rectangle/mip transfer, image round-trips and error/lifetime paths tested | Same strict-C suite; level-zero transfer succeeds and native mip-upload limitation is explicit | Non-Color successful transfer needs renderer-specific C evidence; Skia has the broadest current native format gate |
| Texture3D/TextureCube complete adapter contract | Capability refusal, all six cube faces, full/rectangle/mip validation, DDS refusal, inheritance and lifetime tested | Same strict-C suite with SDL dummy video; missing storage is atomic `NOT_SUPPORTED` | Successful Texture3D and cube storage/readback need renderer-specific C evidence |
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
| Texture2D | Owned standalone or game child that survives callbacks; game children are destroyed before their game |
| Texture3D / TextureCube | Owned game child that survives callbacks; destroy before its game; RenderTargetCube uses its own typed destroy route |
| VertexBuffer / DynamicVertexBuffer | Owned game child; copied declaration; destroy before game; ContentLost registration may be released after buffer destruction |
| IndexBuffer / DynamicIndexBuffer | Owned game child; fixed 16/32-bit width; destroy before game; ContentLost registration may be released after buffer destruction |
| EffectAnnotation / EffectAnnotationCollection | Owned creation-thread-affine metadata handles; collection insertion and lookup copy values; returned annotations outlive their source collection |
| EffectParameter / EffectParameterCollection | Owned creation-thread-affine mutable handles; element aliases and nested views retain stable native storage; each assigned texture-overload slot blocks that resource's disposal/destruction until cleared or released |
| EffectTechnique / EffectPass and collections | Owned creation-thread-affine handles or stable element aliases; nested pass/annotation views and returned elements retain native storage across collection growth/destruction |
| Effect / EffectMaterial / ShaderEffect / SpriteEffect | Owned game child; destroy before game unless live descendant views retain it; descendants transitively retain the effect/game ownership; bound shader textures remain retained per sampler unit until replacement or final descendant release |
| BasicEffect / DirectionalLight | BasicEffect is an owned game child; assigned Texture2D handles are retained independently by each clone; standalone lights are ordinary owned handles and nested light views transitively retain the parent effect/game |
| AlphaTestEffect / DualTextureEffect / EnvironmentMapEffect | Owned game children; each same-device Texture2D or TextureCube slot retains its C resource independently across clones; EnvironmentMapEffect directional-light views transitively retain their parent effect/game |
| SkinnedEffect | Owned game child with copied 72-matrix palette; its same-device Texture2D is retained independently across clones and nested light views transitively retain the parent effect/game |
| ColorMatrixEffect / PbrEffect / SkinnedPbrEffect | Owned game children; each PBR same-device Texture2D slot is retained independently across clones, SkinnedPbr owns a copied 72-matrix palette, and nested light views transitively retain the parent effect/game |
| ModelBone / ModelBoneCollection | Owned creation-thread-affine stable nodes and live collection views; parents retain children, returned aliases share mutation, and weak parent metadata avoids dangling parents after hierarchy release |
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
- remaining 3D resources and draws, consumption of the completed vertex/index-buffer and binding
  values, models, meshes and stock effects beyond the implemented base/material/source-shader/
  sprite family;
- occlusion queries and remaining graphics-device operations;
- Texture3D, TextureCube and renderer-specific successful non-Color transfer evidence;
- SpriteBatch matrices, effects and text drawing;
- advanced and renderer-specific CNA extensions not listed in the implemented table.

Until the generated inventory and completion gates in `plan_binding.md` are finished, any public
CNA symbol not explicitly represented in [`COVERAGE.md`](COVERAGE.md) is unimplemented.
