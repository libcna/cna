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
| cna-gamer-services-server | Rerun `service_cna_session_migration`, `service_cna_session_host_crash` and `service_cna_session_add_gamer` against a rebuilt `cna_service_session_client_harness`. The harness now expects `HostChanged {"", "Bob"}`. The server currently has no build tree. |

## Content pipeline processor version 5 (TQ-038, `8401ac7e1`)

The canonical texture route now follows XNA's step order and resize kernel, so its output bytes differ
and its processor version is 5. Rebuild any committed or cached canonical-route output. Incremental
builds rebuild it on their own, because the processor's identity carries the version
(`TextureProcessor::Identity`).
