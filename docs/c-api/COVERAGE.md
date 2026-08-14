# CNA C API Coverage Matrix

## Coverage rule

The final C API covers every public CNA symbol. This matrix is the authoritative evidence for that
claim. A row identifies the source C++ type/member/constant/event, its C-native mapping, owning C
header, implementation/test locations, capability limitations and status.

The matrix began empty at Phase B0. The initial adapter evidence below is deliberately a small
implemented slice, not a substitute for the complete generated inventory required by `CBIND-033`.
Any public symbol without a later inventory row remains **unimplemented**, never implicitly
unsupported or complete.

[`FEATURE_MATRIX.md`](FEATURE_MATRIX.md) summarizes this initial slice for C consumers. This file
remains authoritative for per-symbol implementation evidence.

## Implemented initial adapter slice

| Source header and symbol | C mapping | C header/declaration | Ownership and thread rule | C-only evidence | Status |
|---|---|---|---|---|---|
| `Game` constructor, `RunOneFrame`, `Run`, `Exit`, `Dispose` | Owned generation-checked game handle | `runtime.h`: `cna_game_create`, `cna_game_run_one_frame`, `cna_game_run`, `cna_game_request_exit`, `cna_game_destroy` | One active game; creation-thread calls; destroy is non-reentrant | `LifecycleSmoke.c` | Implemented slice |
| `Game` lifecycle hooks | Copied C callback table and context | `runtime.h`: `CNA_GameCallbacks`, `CNA_CallbackError` | Callback-scoped handle; callback error copied before return | `LifecycleSmoke.c` | Implemented slice |
| `GameTime` getters | Fixed-layout value snapshot | `runtime.h`: `CNA_GameTime` | Borrowed callback value | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `GameWindow::setTitleProperty` | UTF-8 string view | `runtime.h`: `cna_game_set_window_title` | Creation thread; copies text | `LifecycleSmoke.c` | Implemented slice |
| `GraphicsDevice::Clear(Color)` | `CNA_Color` POD + game operation | `core.h`, `runtime.h`: `CNA_Color`, `cna_game_clear` | Creation thread; callback-safe | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `Game::getGraphicsDeviceProperty` | Callback-scoped borrowed handle | `graphics.h`: `cna_game_get_graphics_device` | Valid only during the active lifecycle callback; never caller-released | `LifecycleSmoke.c` | Implemented slice |
| `GraphicsRendererType`, `GraphicsDevice::GetGraphicsRendererType`, `GetGraphicsRendererName`, `GetMaxTextureDimension`, `SupportsCapability` | Stable renderer identity, versioned renderer POD, UTF-8 count/copy and fixed capability query | `graphics.h`: `CNA_GraphicsRendererType`, `CNA_RendererInfo`, `cna_graphics_device_get_renderer_info`, `cna_graphics_device_get_renderer_name_size`, `cna_graphics_device_copy_renderer_name`, `cna_graphics_device_supports_capability` | Callback-scoped borrowed device; creation thread | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `PresentationParameters` backbuffer width/height/format getters, `GraphicsDevice::GetBackBufferData(Color*)` | Versioned logical-backbuffer POD and full RGBA8 count/copy readback | `graphics.h`: `CNA_BackBufferInfo`, `cna_graphics_device_get_backbuffer_info`, `cna_graphics_device_get_backbuffer_data_rgba8` | Callback-scoped borrowed device during draw; creation thread; no partial output | `LifecycleSmoke.c`: HEADLESS honest refusal and SDL_RENDERER exact texture/clear pixels | Getter/readback slice implemented; remaining presentation members/overloads planned |
| `SurfaceFormat` | Stable fixed-width identities for every current canonical enum value | `graphics.h`: `CNA_SurfaceFormat`, `CNA_SURFACE_FORMAT_*` | POD value; only Color transfer is enabled in this slice | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Enum mapped; per-format transfers remain planned |
| `Texture2D(GraphicsDevice&, int, int[, bool, SurfaceFormat])`, width/height/bounds, inherited format/level count | Owned child handle plus versioned create/info PODs | `graphics.h`: `CNA_Texture2DCreateInfo`, `CNA_Texture2DInfo`, `cna_texture2d_create`, `cna_texture2d_get_info` | Create during a game callback; owned texture survives callback and must be destroyed before game | `LifecycleSmoke.c` under HEADLESS and SDL_RENDERER | Color-format slice implemented |
| `Texture2D::SetData(Color*)`, `GetData(Color*)`, `Dispose` | Bulk RGBA8 upload/readback and explicit destroy | `graphics.h`: `cna_texture2d_set_data_rgba8`, `cna_texture2d_get_data_rgba8`, `cna_texture2d_destroy` | Creation-thread calls; exact level-zero count; no partial readback; destroy once | `LifecycleSmoke.c` round-trip, capacity, stale/double-destroy and parent-order checks | Color-format slice implemented |
| `Vector2` fields, `Rectangle` fields | Fixed-layout C value representations | `core.h`: `CNA_Vector2`, `CNA_Rectangle` | POD values copied at calls; methods/operators remain for complete-surface work | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Field layout implemented; methods/operators planned |
| `SpriteSortMode`, `SpriteEffects` | Stable fixed-width identities and effect bits | `graphics.h`: `CNA_SpriteSortMode`, `CNA_SPRITE_SORT_MODE_*`, `CNA_SpriteEffects`, `CNA_SPRITE_EFFECT_*` | POD values validated before native calls | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Complete enum mapping |
| `SpriteBatch(GraphicsDevice&)`, `Begin`, texture `Draw` rectangle overload, `End`, `Dispose` | Owned child handle with versioned begin POD and one bulk POD-command-array call | `graphics.h`: `CNA_SpriteBatchBeginInfo`, `CNA_SpriteCommand`, `cna_sprite_batch_create`, `cna_sprite_batch_begin`, `cna_sprite_batch_submit_many`, `cna_sprite_batch_end`, `cna_sprite_batch_destroy` | Same-game textures retained through active interval; creation-thread calls; active destroy cancels without deferred flush | `LifecycleSmoke.c` under HEADLESS and SDL_RENDERER covers state, validation, lifetime and stale handles | Texture-quad batching slice implemented; custom states/effects/text remain planned |
| `Keys` | Stable fixed-width identities for all 160 canonical enum members | `input.h`: `CNA_Key`, `CNA_KEY_*` | POD values; numeric slots 0–255 accepted by snapshot queries | `CheckKeyIdentities.cmake`, `AbiHeaderC.c`, `AbiHeaderCpp.cpp` | Complete enum mapping |
| `Keyboard::GetState()` | Fresh caller-owned 256-bit snapshot scoped by active game handle | `input.h`: `CNA_KeyboardState`, `cna_keyboard_get_state` | Capture on game creation thread; no per-frame cache; POD survives callback/game | `LifecycleSmoke.c` under HEADLESS and SDL_RENDERER, including wrong-thread refusal | Non-player overload implemented; player overload/extensions planned |
| `KeyboardState::getItem`, `operator[]`, `IsKeyDown`, `IsKeyUp`, `GetPressedKeys` | Local key tests plus ascending count/copy | `input.h`: `cna_keyboard_state_is_key_down`, `cna_keyboard_state_is_key_up`, `cna_keyboard_state_get_pressed_key_count`, `cna_keyboard_state_copy_pressed_keys` | Pure immutable POD queries allowed on any thread; no partial copy | `LifecycleSmoke.c` synthetic multiword snapshot, invalid key and capacity checks | Query slice implemented; equality/hash/string and constructor inventory remain planned |
| `Mouse::GetState()`, `MouseState` readable properties including horizontal wheel extension | Fresh fixed-layout position/wheel/button snapshot | `input.h`: `CNA_MouseButtonFlags`, `CNA_MouseState`, `cna_mouse_get_state` | Capture on game creation thread; copied POD survives callback/game | `InputSnapshotsSmoke.c` under HEADLESS and SDL_RENDERER, including output validation and wrong-thread refusal | Read-only snapshot implemented; cursor mutation/events/extensions planned |
| `PlayerIndex`, `GamePadDeadZone`, `Buttons` | Stable fixed-width identities, including all 31 current native button bits | `input.h`: `CNA_PlayerIndex`, `CNA_GamePadDeadZone`, `CNA_GamePadButtonFlags`, constants | POD values validated before capture/local queries | `CheckGamePadButtonIdentities.cmake`, `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `InputSnapshotsSmoke.c` | Enum/bit mapping implemented |
| `GamePad::GetState(PlayerIndex)` and `GetState(PlayerIndex, GamePadDeadZone)` | Fresh connection/packet/buttons/sticks/triggers snapshot | `input.h`: `CNA_GamePadState`, `cna_gamepad_get_state`, `cna_gamepad_get_state_with_dead_zone` | Capture on game creation thread; disconnected player is successful rest state | `InputSnapshotsSmoke.c` all four players/all three modes under HEADLESS and SDL_RENDERER, including wrong-thread/invalid identity checks | State overloads implemented; capabilities/control/extensions planned |
| `GamePad` dead-zone constants, `ExcludeAxisDeadZone`, thumbstick/trigger normalization behavior | Named thresholds and pure full-analog transform | `input.h`: `CNA_GamePadAnalogState`, threshold constants, `cna_gamepad_apply_dead_zone` | Pure copied-POD operation allowed on any thread; failure leaves output unchanged | `InputSnapshotsSmoke.c` exact none/independent/circular synthetic vectors, clamping and non-finite rejection | Dead-zone/normalization slice implemented |
| `GamePadState::IsButtonDown`, `IsButtonUp` | Local combined-bit snapshot queries | `input.h`: `cna_gamepad_state_is_button_down`, `cna_gamepad_state_is_button_up` | Pure copied-POD queries allowed on any thread | `InputSnapshotsSmoke.c` single/combined/empty/invalid masks | Query slice implemented; remaining state members planned |
| `TouchPanel::GetCapabilities`, `TouchPanel::GetState`, `TouchPanelCapabilities`, `TouchCollection` connection/count/indexer | Versioned capability POD and fixed-capacity eight-location snapshot | `input.h`: `CNA_TouchCapabilities`, `CNA_TouchState`, `cna_touch_get_capabilities`, `cna_touch_get_state` | Capture on game creation thread; platform absence succeeds as disconnected/empty | `InputSnapshotsSmoke.c` under HEADLESS and SDL_RENDERER, including range and wrong-thread checks | Capability/state snapshot implemented; display/gesture/event surfaces planned |
| `TouchCollection::FindById`, `TouchLocation` getters/pressure extension/`TryGetPreviousLocation` | Fixed-layout current/previous location plus pure local helpers | `input.h`: `CNA_TouchLocation`, `cna_touch_state_find_by_id`, `cna_touch_location_try_get_previous` | Pure copied-POD queries allowed on any thread; absent lookup writes invalid sentinel | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `InputSnapshotsSmoke.c` found/absent/previous/invalid cases | Read/query slice implemented; equality/hash/string/constructors planned |
| `ContentManager` constructors, `RootDirectory`, `Load<Texture2D>`, `Unload`, `Dispose` | Owned game-child manager, UTF-8 root count/copy/set, typed Color Texture2D load, explicit cache unload/destroy | `content.h`: `CNA_ContentManagerCreateInfo`, `cna_content_manager_*` | Create from callback-borrowed device; manager and returned independent texture handles survive callbacks and must be destroyed before game | `ContentSmoke.c` under HEADLESS and SDL_RENDERER covers UTF-8/capacity/thread/parent order, cache hit/unload, exact pixels and missing-file IO | Initial Color Texture2D content slice implemented; remaining types/members planned |
| `AudioChannels`, `SoundState` | Stable fixed-width channel and playback-state identities | `audio.h`: `CNA_AudioChannels`, `CNA_SoundState`, constants | POD values validated at calls/snapshots | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `AudioSmoke.c` | Complete enum mapping |
| `SoundEffect` raw-buffer constructor, `Duration`, `CreateInstance`, `Dispose` | Owned game-child PCM16LE handle, tick duration and owned instance child | `audio.h`: `CNA_SoundEffectCreateInfo`, `cna_sound_effect_create_pcm16`, `cna_sound_effect_get_duration_ticks`, `cna_sound_effect_create_instance`, `cna_sound_effect_destroy` | Creation thread; bytes copied; instances before effect before game; no device maps to NotSupported | `AudioSmoke.c` with SDL dummy audio under HEADLESS and SDL_RENDERER | Minimal raw-PCM resource slice implemented; file/content/static/global/3D members planned |
| `SoundEffectInstance` play/stop/pause/resume, state, volume, pitch, pan, loop and `Dispose` | Owned instance handle, versioned info POD and explicit controls | `audio.h`: `CNA_SoundEffectInstanceInfo`, `cna_sound_effect_instance_*` | Creation thread; native mixer thread retains no C callback/context; destroy invalidates handle after native detach | `AudioSmoke.c` transitions, clamp/ranges, lifetime, stale handle and wrong-thread checks | Minimal control/property slice implemented; Apply3D and advanced audio planned |

## Source inventory boundary

`CBIND-033` will generate the complete inventory from public framework headers under:

```text
modules/*/include/Microsoft/**
modules/*/include/CNA/**
```

The scanner excludes `CNA/Internal/**`, generated/test-only artifacts and the C API's own headers.
Any header exposed to a normal CNA application that is not matched by this rule must be added to the
generator input deliberately. The review output records the exact source commit and header/member
count.

For each inventory entry, one of these C mappings is mandatory:

| Mapping status | Required evidence |
|---|---|
| Implemented | C declaration, adapter implementation, C-only positive/negative tests and ABI layout/export tests where applicable. |
| Planned | Target C header/function family, required mapping form and blocking predecessor. |
| Native limitation | A callable C API reports the exact existing CNA renderer/platform limitation; it has a test and owner-approved documentation. |

`Not applicable` is not a valid status for a public CNA symbol. C++ syntax differences are solved by
a C mapping, not by dropping the symbol.

## Required row fields

```text
Source header and symbol
Public family
C mapping (POD / handle / function set / callback / count-copy)
C header and declaration(s)
Ownership and thread rule
Error/capability behavior
C-only test(s)
ABI test(s)
Status
```

`CBIND-043` will make the inventory comparison a required build/CI gate. Adding a new public CNA
symbol must add its coverage row and matching C API work in the same task; deleting or changing a
public symbol must update the mapping intentionally.
