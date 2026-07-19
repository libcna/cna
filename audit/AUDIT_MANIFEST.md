# AUDIT_MANIFEST.md — Master Work-Queue Index

This is the authoritative index of every manifest shard. Each shard file under `manifest/<shard>.md` lists every eligible first-party source-like file in that subsystem with a per-file audit status (`PENDING` / `IN_PROGRESS` / `AUDITED` / `BLOCKED`). See `AUDIT_SCOPE.md` for the classification rules that produced this split between AUDIT and EXEMPT, and `AUDIT_PROGRESS.md` for the live rollup and resume point.

Manifest shards are an organizational device only (grouped roughly by subsystem/backend so progress can be tracked and committed in logical batches) — they are not called out by name in the audit prompt itself, but every eligible file from the full repository inventory appears in exactly one shard below.

**Totals: 2297 AUDIT-eligible files across 105 shards, 337 EXEMPT files across 8 reason-categories, 2634 tracked files overall.**

**Note (as of the `cna-internal-core` shard closing out Task #3):** the per-shard rollup counts below are
refreshed opportunistically, not automatically, as each shard's own manifest file is updated via
`mark_audited.py` — several shards below may show a stale `PENDING` count even though their own
`manifest/<shard>.md` file is actually fully `AUDITED` (confirmed directly: all 16 graphics-backend shards
and all 5 `cna-*` shards are fully complete as of this note, and this table has been refreshed for those 21
shards specifically). Treat each shard's own `manifest/<shard>.md` file, not this rollup, as the source of
truth; a full resync of this table across all 105 shards is deferred to Pass 7 (completeness verification).

## Graphics backends (src+include, mirrored) (271 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`backend-ascii`](manifest/backend-ascii.md) | 6 | AUDITED: 6 / PENDING: 0 (complete) |
| [`backend-bgfx`](manifest/backend-bgfx.md) | 34 | AUDITED: 34 / PENDING: 0 (complete) |
| [`backend-canvas`](manifest/backend-canvas.md) | 8 | AUDITED: 8 / PENDING: 0 (complete) |
| [`backend-common`](manifest/backend-common.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`backend-d3d11`](manifest/backend-d3d11.md) | 20 | AUDITED: 20 / PENDING: 0 (complete) |
| [`backend-d3d12`](manifest/backend-d3d12.md) | 26 | AUDITED: 26 / PENDING: 0 (complete) |
| [`backend-d3d9`](manifest/backend-d3d9.md) | 50 | AUDITED: 50 / PENDING: 0 (complete) |
| [`backend-d3dcommon`](manifest/backend-d3dcommon.md) | 46 | AUDITED: 46 / PENDING: 0 (complete) |
| [`backend-dx3`](manifest/backend-dx3.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`backend-easygl`](manifest/backend-easygl.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`backend-headless`](manifest/backend-headless.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`backend-sdlgpu`](manifest/backend-sdlgpu.md) | 27 | AUDITED: 27 / PENDING: 0 (complete) |
| [`backend-sdlrenderer`](manifest/backend-sdlrenderer.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`backend-software`](manifest/backend-software.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`backend-vulkan`](manifest/backend-vulkan.md) | 40 | AUDITED: 40 / PENDING: 0 (complete) |
| [`backend-webgpu`](manifest/backend-webgpu.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |

## CNA internal core (non-backend) (205 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`cna-devices`](manifest/cna-devices.md) | 39 | AUDITED: 39 / PENDING: 0 (complete) |
| [`cna-graphics`](manifest/cna-graphics.md) | 7 | AUDITED: 7 / PENDING: 0 (complete) |
| [`cna-input`](manifest/cna-input.md) | 31 | AUDITED: 31 / PENDING: 0 (complete) |
| [`cna-internal-core`](manifest/cna-internal-core.md) | 113 | AUDITED: 113 / PENDING: 0 (complete) |
| [`cna-root-utilities`](manifest/cna-root-utilities.md) | 15 | AUDITED: 15 / PENDING: 0 (complete) |

## Microsoft.Xna.Framework — public API (include+src) (541 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`xna-audio`](manifest/xna-audio.md) | 31 | AUDITED: 31 / PENDING: 0 (complete) |
| [`xna-content`](manifest/xna-content.md) | 15 | AUDITED: 15 / PENDING: 0 (complete) |
| [`xna-framework-core`](manifest/xna-framework-core.md) | 78 | AUDITED: 78 / PENDING: 0 (complete) |
| [`xna-gamerservices`](manifest/xna-gamerservices.md) | 89 | AUDITED: 89 / PENDING: 0 (complete) |
| [`xna-graphics`](manifest/xna-graphics.md) | 191 | AUDITED: 191 / PENDING: 0 (complete) |
| [`xna-input`](manifest/xna-input.md) | 44 | AUDITED: 44 / PENDING: 0 (complete) |
| [`xna-media`](manifest/xna-media.md) | 45 | AUDITED: 45 / PENDING: 0 (complete) |
| [`xna-net`](manifest/xna-net.md) | 42 | AUDITED: 42 / PENDING: 0 (complete) |
| [`xna-storage`](manifest/xna-storage.md) | 6 | AUDITED: 6 / PENDING: 0 (complete) |

## Microsoft.Devices — public API (include+src) (54 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`microsoft-devices`](manifest/microsoft-devices.md) | 54 | AUDITED: 54 / PENDING: 0 (complete) |

## Tests — Microsoft.Xna.Framework (235 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`tests-xna-audio`](manifest/tests-xna-audio.md) | 24 | AUDITED: 24 / PENDING: 0 (complete) |
| [`tests-xna-content`](manifest/tests-xna-content.md) | 30 | PENDING: 30 / AUDITED: 0 |
| [`tests-xna-framework-core`](manifest/tests-xna-framework-core.md) | 46 | AUDITED: 46 / PENDING: 0 (complete) |
| [`tests-xna-gamerservices`](manifest/tests-xna-gamerservices.md) | 16 | AUDITED: 16 / PENDING: 0 (complete) |
| [`tests-xna-graphics`](manifest/tests-xna-graphics.md) | 64 | PENDING: 64 / AUDITED: 0 |
| [`tests-xna-input`](manifest/tests-xna-input.md) | 24 | PENDING: 24 / AUDITED: 0 |
| [`tests-xna-media`](manifest/tests-xna-media.md) | 22 | PENDING: 22 / AUDITED: 0 |
| [`tests-xna-net`](manifest/tests-xna-net.md) | 9 | AUDITED: 9 / PENDING: 0 (complete) |

## Tests — CNA internals / Microsoft.Devices / misc (107 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`tests-cna-core`](manifest/tests-cna-core.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |
| [`tests-cna-devices`](manifest/tests-cna-devices.md) | 10 | AUDITED: 10 / PENDING: 0 (complete) |
| [`tests-cna-input`](manifest/tests-cna-input.md) | 5 | AUDITED: 5 / PENDING: 0 (complete) |
| [`tests-cna-internal`](manifest/tests-cna-internal.md) | 65 | PENDING: 65 / AUDITED: 0 |
| [`tests-microsoft-devices`](manifest/tests-microsoft-devices.md) | 25 | PENDING: 0 / AUDITED: 25 |
| [`tests-misc`](manifest/tests-misc.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |

## Tools (first-party tooling) (85 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`tools-audio`](manifest/tools-audio.md) | 6 | AUDITED: 6 / PENDING: 0 (complete) |
| [`tools-avatar-asset-pipeline`](manifest/tools-avatar-asset-pipeline.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`tools-avatar-builder`](manifest/tools-avatar-builder.md) | 15 | PENDING: 15 / AUDITED: 0 |
| [`tools-cna-reference`](manifest/tools-cna-reference.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`tools-devices`](manifest/tools-devices.md) | 5 | AUDITED: 5 / PENDING: 0 (complete) |
| [`tools-fna-reference`](manifest/tools-fna-reference.md) | 7 | AUDITED: 7 / PENDING: 0 (complete) |
| [`tools-gltf-to-cnj`](manifest/tools-gltf-to-cnj.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |
| [`tools-input-parity`](manifest/tools-input-parity.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`tools-net`](manifest/tools-net.md) | 2 | AUDITED: 2 / PENDING: 0 (complete) |
| [`tools-xna-oracle`](manifest/tools-xna-oracle.md) | 42 | PENDING: 42 / AUDITED: 0 |

## Examples — backend-named integration test executables (570 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`examples-tests-ascii`](manifest/examples-tests-ascii.md) | 6 | PENDING: 6 / AUDITED: 0 |
| [`examples-tests-bgfx`](manifest/examples-tests-bgfx.md) | 98 | PENDING: 98 / AUDITED: 0 |
| [`examples-tests-canvas`](manifest/examples-tests-canvas.md) | 2 | PENDING: 2 / AUDITED: 0 |
| [`examples-tests-d3d11`](manifest/examples-tests-d3d11.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-tests-d3d12`](manifest/examples-tests-d3d12.md) | 2 | PENDING: 2 / AUDITED: 0 |
| [`examples-tests-d3d9`](manifest/examples-tests-d3d9.md) | 14 | PENDING: 14 / AUDITED: 0 |
| [`examples-tests-dx3`](manifest/examples-tests-dx3.md) | 9 | PENDING: 9 / AUDITED: 0 |
| [`examples-tests-easygl`](manifest/examples-tests-easygl.md) | 218 | PENDING: 218 / AUDITED: 0 |
| [`examples-tests-generic`](manifest/examples-tests-generic.md) | 24 | PENDING: 24 / AUDITED: 0 |
| [`examples-tests-headless`](manifest/examples-tests-headless.md) | 7 | PENDING: 7 / AUDITED: 0 |
| [`examples-tests-sdlgpu`](manifest/examples-tests-sdlgpu.md) | 22 | PENDING: 22 / AUDITED: 0 |
| [`examples-tests-sdlrenderer`](manifest/examples-tests-sdlrenderer.md) | 67 | PENDING: 67 / AUDITED: 0 |
| [`examples-tests-software`](manifest/examples-tests-software.md) | 6 | PENDING: 6 / AUDITED: 0 |
| [`examples-tests-vulkan`](manifest/examples-tests-vulkan.md) | 70 | PENDING: 70 / AUDITED: 0 |
| [`examples-tests-webgpu`](manifest/examples-tests-webgpu.md) | 22 | PENDING: 22 / AUDITED: 0 |

## Examples — demo applications (110 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`examples-common`](manifest/examples-common.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_2d`](manifest/examples-demo_2d.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_achievement_showcase`](manifest/examples-demo_achievement_showcase.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_avatar`](manifest/examples-demo_avatar.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_avatar_animation_gallery`](manifest/examples-demo_avatar_animation_gallery.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_avatar_appearance_tint_studio`](manifest/examples-demo_avatar_appearance_tint_studio.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_avatar_bone_state_boundary`](manifest/examples-demo_avatar_bone_state_boundary.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_avatar_dual_compare`](manifest/examples-demo_avatar_dual_compare.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_avatar_multi_attach_stress`](manifest/examples-demo_avatar_multi_attach_stress.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_avatar_wardrobe_hotswap`](manifest/examples-demo_avatar_wardrobe_hotswap.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_devices`](manifest/examples-demo_devices.md) | 35 | PENDING: 35 / AUDITED: 0 |
| [`examples-demo_friends_and_gamercard`](manifest/examples-demo_friends_and_gamercard.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_gamer_profile_privileges`](manifest/examples-demo_gamer_profile_privileges.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_gamer_roster_hud`](manifest/examples-demo_gamer_roster_hud.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_gamerservices_dispatcher_watchdog`](manifest/examples-demo_gamerservices_dispatcher_watchdog.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_gamerservices_signin_presence`](manifest/examples-demo_gamerservices_signin_presence.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_guide_overlay_console`](manifest/examples-demo_guide_overlay_console.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |
| [`examples-demo_input`](manifest/examples-demo_input.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_leaderboard_viewer`](manifest/examples-demo_leaderboard_viewer.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_net_avatar_sync`](manifest/examples-demo_net_avatar_sync.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_net_client_server_arena`](manifest/examples-demo_net_client_server_arena.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_packet_roundtrip`](manifest/examples-demo_packet_roundtrip.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |
| [`examples-demo_qos_probe`](manifest/examples-demo_qos_probe.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |
| [`examples-demo_session_browser`](manifest/examples-demo_session_browser.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_session_lifecycle_events`](manifest/examples-demo_session_lifecycle_events.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |
| [`examples-demo_simulated_network_conditions`](manifest/examples-demo_simulated_network_conditions.md) | 3 | AUDITED: 3 / PENDING: 0 (complete) |
| [`examples-demo_sound`](manifest/examples-demo_sound.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`examples-demo_xact`](manifest/examples-demo_xact.md) | 4 | PENDING: 4 / AUDITED: 0 |
| [`examples-input_smoke`](manifest/examples-input_smoke.md) | 1 | AUDITED: 1 / PENDING: 0 (complete) |

## Documentation (docs/*.md) (72 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`docs`](manifest/docs.md) | 72 | PENDING: 72 / AUDITED: 0 |

## Build / CI / CMake / scripts (47 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`build-ci`](manifest/build-ci.md) | 3 | PENDING: 3 / AUDITED: 0 |
| [`build-cmake`](manifest/build-cmake.md) | 13 | PENDING: 13 / AUDITED: 0 |
| [`build-cmake-tests`](manifest/build-cmake-tests.md) | 14 | PENDING: 14 / AUDITED: 0 |
| [`build-root`](manifest/build-root.md) | 5 | PENDING: 5 / AUDITED: 0 |
| [`scripts`](manifest/scripts.md) | 12 | PENDING: 12 / AUDITED: 0 |

## EXEMPT files (by reason)

Full per-file exempt listing lives under `exempt/<reason>.md`. Summary:

| Reason | Count | Detail |
|---|---|---|
| `binary-or-data-asset` | 119 | [exempt/binary-or-data-asset.md](exempt/binary-or-data-asset.md) |
| `generated-content-asset` | 77 | [exempt/generated-content-asset.md](exempt/generated-content-asset.md) |
| `legal-text` | 3 | [exempt/legal-text.md](exempt/legal-text.md) |
| `planning-tracking-doc` | 44 | [exempt/planning-tracking-doc.md](exempt/planning-tracking-doc.md) |
| `third-party-vendored` | 44 | [exempt/third-party-vendored.md](exempt/third-party-vendored.md) |
| `vcs-meta` | 8 | [exempt/vcs-meta.md](exempt/vcs-meta.md) |
| `vendored-test-fixture` | 30 | [exempt/vendored-test-fixture.md](exempt/vendored-test-fixture.md) |
| `vendored-verbatim-stock-effect` | 12 | [exempt/vendored-verbatim-stock-effect.md](exempt/vendored-verbatim-stock-effect.md) |

## Graphics backends tracked (per AUDIT_GRAPHICS_BACKEND_MATRIX.md)

Ascii, Bgfx, Canvas, D3D11, D3D12, D3D9, Dx3, EasyGL, Headless, SdlGpu, SdlRenderer, Software, Vulkan, WebGPU (14 real backends, confirmed against the repository), plus shared `D3DCommon` and `Common` infrastructure (not standalone backends, audited as shared code).
