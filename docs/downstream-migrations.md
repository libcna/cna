# Downstream migrations

What other repositories must change because of a CNA change. CNA does not edit these repositories itself
(CLAUDE.md: "If old game code breaks after the API is corrected, fix the game code separately").
Remove an entry when its migration has landed. Retired code (`cna-cemetery`) and vendored CNA copies
(`openeggbert/mobile-eggbertlite/cna`) are not migrated.

## `RunOneFrame` only ticks (TQ-037, `3558f22cd`)

XNA's `Game.RunOneFrame` ticks once and never initializes (`WindowsGameHost.RunOneFrame` IL_0000-0028).
CNA used to initialize on the first call, as FNA does. A caller that relies on that now gets `Update`
and `Draw` without `Initialize`, `LoadContent` or `BeginRun`.

Migration: use `Game::RunFrameEXT()` / `cna_game_run_frame_ext(game, &running)` for the first frame,
or for every frame. It initializes and begins the run on its first call and ends the run after `Exit`.
CNA's own C smokes use `modules/c-api/tests/pure_c/CnaTestRunFrame.h`.

| Repository | Call sites |
|---|---|
| cna-demos | `standalone/*/main.c` of these 32 demos: `c_api.content.{additional_loaders,manager_inspection,object_dictionary}`, `c_api.core.error_handling`, `c_api.devices.{camera_frames,dialogs,host_info,sensor_lifecycle,system_tray,vibration}`, `c_api.game.{component_collection,component_members,graphics_manager_lifecycle,graphics_preferences,runtime_controls}`, `c_api.gamer.guide_dispatcher`, `c_api.graphics.{debug_draw,image_based_light_values,model_animations,model_scene_metadata,morph_targets,retro_effects,skinned_models,sprite_font_state,texture2d_io,texture_volume_data}`, `c_api.input.{clipboard,devices,haptics,joystick,text_input}`, `c_api.media.video_asset`. Their `metadata.cpp` route lists name `cna_game_run_one_frame` and need the new route too. |
| cna-demos | `demos/game/lifecycle/run_one_frame.cpp`: its update count and its comment ("used by RunOneFrame after initialization") describe the old behaviour. |
| cna-dotnet | `src/CNA.Framework/Game.cs` (`RunOneFrame` -> `cna_game_run_one_frame`) and `src/CNA.XnaCompat/.../Game.cs:161` keep XNA's name, so XNA semantics are right. But every test and tool that drives a game with `RunOneFrame()` and expects initialization must switch to the host-driven route: `tests/CNA.Integration.Tests/*` (NativeGameFixture, GameLifecycleTests, GameComponentTests and the Compat* tests), `tests/CNA.XnaCompat.RuntimeProbe`, `tests/CNA.XnaCompat.GraphicsProbe`, `tests/CNA.OwnershipStress`, `tests/CNA.GamerServices.IntegrationTests/GamerServicesGameFixture.cs` and `tools/content-survey`. `BrowserGameLoop.cs` has its own `RunOneFrame` helper; check which native route it calls. |
| cna-engine | `examples/quick_start/main.cpp:397` |
| cna-extended | `include/CNA/Extended/ECS/WorldBuilder.hpp:44`: a doc comment that names `RunOneFrame`; check what it promises. |

## Render-target ContentLost survives binding (TQ-035, `b6b8ab844`)

XNA clears ContentLost only on a draw, a successful `Clear` or `SetData`, never on `SetRenderTarget(s)`.

| Repository | Call sites |
|---|---|
| cna-demos | `standalone/c_api.graphics.render_target_bindings/main.c:256-275` asserts "rebinding only the 2D target clears only its lost-content flag" and the cube equivalent. Clear or draw into the target after binding it, and expect the flag to clear then. |

## TouchCollection is read-only (TQ-036, `713886f49`)

`cna_touch_state_add/insert/remove/remove_at/clear` now return `CNA_RESULT_NOT_SUPPORTED` and write
nothing, as XNA's mutators throw `NotSupportedException`. The C++ indexer is const, and the setter is
`setItemProperty`, which throws.

| Repository | Call sites |
|---|---|
| cna-demos | `standalone/c_api.input.touch_state/main.c:422-447` builds a collection with the mutators. Build it with `cna_touch_state_init_from_locations`, and show the mutators' refusal. |
| cna-dotnet | No change: its `TouchCollection` already throws `NotSupportedException` and stores `IsConnected`. |

## Net (TQ-039, `60e236c92`)

| Repository | What |
|---|---|
| cna-dotnet | `LocalNetworkGamer.SendData(PacketWriter, …)` sends `data.ByteArray` through `cna_local_network_gamer_send_data_to`. For an emptied writer, XNA refuses with `ArgumentOutOfRangeException("count")`, while cna-dotnet reports `offset`. Use the packet-writer route, or check emptiness managed-side with XNA's parameter name. The null-recipient check already matches XNA. |
| cna-dotnet | `Host` is now null after the host leaves until a host change, and `HostChanged` then reports a null `OldHost`. `src/CNA.XnaCompat/.../Net/NetworkSession.cs:118-122` (`Host`) and `:771-774` (`Resolve(oldHost)!`) must accept an invalid host handle. `AddLocalGamer` (`:451`) now joins at the next `Update`. |
| cna-demos | `standalone/net.network_session.host_leaves/main.cpp:308-313` dereferences `args.getOldHostProperty()`, which is null when the host left. Check it before use. |
| cna-gamer-services-server | Rerun `service_cna_session_migration`, `service_cna_session_host_crash` and `service_cna_session_add_gamer` against a rebuilt `cna_service_session_client_harness`. The harness now expects `HostChanged {"", "Bob"}`. The server currently has no build tree. |

## Net host bits, local leaves and packet capacities (TQ-043)

`NetworkGamer.IsHost` is now the gamer's own bit (only the host gamer reports it), and `NetworkSession.IsHost` is the
session's own flag. A host change takes effect when it arrives, and `Update` raises `HostChanged` after the old host's
`GamerLeft`. Removing a local gamer raises `GamerLeft`; only the machine's last local gamer ends the session.
`PacketWriter(n)`/`PacketReader(n)` allocate `n` bytes, and `ReceiveData(PacketReader)` on an empty queue refuses a reader
that never had a buffer (`ArgumentOutOfRangeException("offset")` / `CNA_RESULT_INVALID_ARGUMENT`).

| Repository | Call sites |
|---|---|
| cna-dotnet | **Breaks:** `src/CNA.XnaCompat/.../Net/NetworkSession.cs:514-523` creates its scratch reader with `cna_packet_reader_create(0, ...)`, so `LocalNetworkGamer.cs:147-166` `ReceiveData(PacketReader)` now fails on an empty queue. Create the scratch reader with a positive capacity and apply XNA's offset check to the managed reader. `NetworkSession.cs:750-776`: `OnGamerLeft` now also fires for local gamers (`Resolve` must find a departed `LocalNetworkGamer`), and `OnHostChanged` comes only from `Update`. Tests that expect every host-machine gamer to report `IsHost`, or use `SetIsHost` to move the session's host role, must change. |
| cna-examples | `src/Demos/Net/NetworkSession/LocalSessionLifecycleScreen.hpp:60` still ends its session, now after a `GamerLeft`; the `IsHost` displays (`SystemLinkHostScreen.hpp:60`, `DiscoverAndJoinScreen.hpp:80`, `GamerRosterScreen.hpp:49`) show only the host gamer. No code change. |
| cna-samples | The ported XNA network samples now show XNA's single host marker; rerun the split-screen host case. No code change. |

## Stock effects follow XNA's tables, names and clones (TQ-041)

SkinnedEffect, AlphaTestEffect, DualTextureEffect and EnvironmentMapEffect now name their technique after the effect
type (XNA's embedded names, not "Default"), and every stock technique has one pass named the empty string (not
"P0"). BasicEffect keeps derived parameters on an unchanged Apply (dirty flags), its lights write through to their
parameters, and stock clones keep raw parameter writes. A bare Effect keeps "Default"/"P0".

| Repository | Call sites |
|---|---|
| cna-demos | `demos/graphics/effect/parameters_and_techniques.cpp:69-70` expects BasicEffect's first pass to be named "P0" and found by `passes["P0"]`; use index 0 or the empty name. (`custom_apply_lifecycle.cpp` uses a bare Effect and is unaffected.) |

## Model.Draw no longer falls back to bone 0 (TQ-046)

XNA's `Model.Draw` dereferences each mesh's `ParentBone` (Refresh IL_0055..0060), so a mesh without a parent bone is
an error; CNA used bone 0 silently.

| Repository | Call sites |
|---|---|
| cna-examples | `src/Demos/Graphics3D/ModelGroup/ProceduralModelScreen.hpp:137` builds a one-mesh, one-bone Model without setting the mesh's `ParentBone`, then draws it (`:177`). Set the parent bone (or use the Model constructor with the parent-bone vector). cna-demos `demos/graphics/model/procedural_construction.cpp` already passes its parent vector. |

## Content pipeline processor version 6 (TQ-038 `8401ac7e1`, TQ-050)

The canonical texture route now follows XNA's step order and resize kernel (TQ-038, version 5) and keeps 16-bit PNGs
as Rgba64 (TQ-050, version 6), so its output bytes differ. Rebuild any committed or cached canonical-route output. Incremental
builds rebuild it on their own, because the processor's identity carries the version
(`TextureProcessor::Identity`).
