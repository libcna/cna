# AUDIT_MANIFEST.md — Master Work-Queue Index

This is the authoritative index of every manifest shard. Each shard file under `manifest/<shard>.md` lists every eligible first-party source-like file in that subsystem with a per-file audit status (`PENDING` / `IN_PROGRESS` / `AUDITED` / `BLOCKED`). See `AUDIT_SCOPE.md` for the classification rules that produced this split between AUDIT and EXEMPT, and `AUDIT_PROGRESS.md` for the live rollup and resume point.

Manifest shards are an organizational device only (grouped roughly by subsystem/backend so progress can be tracked and committed in logical batches) — they are not called out by name in the audit prompt itself, but every eligible file from the full repository inventory appears in exactly one shard below.

**Totals: 2365 AUDIT-eligible files across 105 shards, 337 EXEMPT files across 8 reason-categories, 2702 tracked files overall.**

**Note (as of Task #8/#9 closing out):** every one of the 105 shards below was fully `AUDITED`
(`PENDING: 0` in every row) at that point, independently reconciled against each shard's own
`manifest/<shard>.md` file and against `audit/<path>.audit.md` existing on disk for all 2297
AUDIT-eligible files then tracked. This full resync was originally deferred to Pass 7, but Tasks
#8/#9's completion made it natural to do it now rather than let staleness accumulate further; Pass 7
(completeness verification) will still perform an independent final rescan.

**Note (`plan_modern.md` `MOD-12`):** the statement above no longer describes the whole tree, and is
kept as the record of where Task #8/#9 left it rather than quietly amended. The `cna-graphics` shard
has grown from 7 files to 75: the `CNA::Graphics` engine layer was five enums and a settings bag when
it was audited, and `plan_modern.md` has since made it the largest body of new work in the
repository. Its 68 new rows are `PENDING` — work-queue entries, not audits — so the tree-wide claim
is now "104 shards complete, `cna-graphics` at 7/75". Every one of those 68 files landed with its own
tests and a verified build under its plan row; what is outstanding is the independent audit pass, not
the implementation.

**A second staleness worth stating plainly:** every path in this manifest is in the pre-modules
layout (`include/…`, `src/…`, `tests/…`). The repository has since moved to
`modules/<name>/{include,src,tests}/`. The paths below are therefore logical, not physical. New
`cna-graphics` reports name both.

## Graphics backends (src+include, mirrored) (271 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`backend-ascii`](manifest/backend-ascii.md) | 6 | PENDING: 0 / AUDITED: 6 (complete) |
| [`backend-bgfx`](manifest/backend-bgfx.md) | 34 | PENDING: 0 / AUDITED: 34 (complete) |
| [`backend-canvas`](manifest/backend-canvas.md) | 8 | PENDING: 0 / AUDITED: 8 (complete) |
| [`backend-common`](manifest/backend-common.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`backend-d3d11`](manifest/backend-d3d11.md) | 20 | PENDING: 0 / AUDITED: 20 (complete) |
| [`backend-d3d12`](manifest/backend-d3d12.md) | 26 | PENDING: 0 / AUDITED: 26 (complete) |
| [`backend-d3d9`](manifest/backend-d3d9.md) | 50 | PENDING: 0 / AUDITED: 50 (complete) |
| [`backend-d3dcommon`](manifest/backend-d3dcommon.md) | 46 | PENDING: 0 / AUDITED: 46 (complete) |
| [`backend-dx3`](manifest/backend-dx3.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`backend-easygl`](manifest/backend-easygl.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`backend-headless`](manifest/backend-headless.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`backend-sdlgpu`](manifest/backend-sdlgpu.md) | 27 | PENDING: 0 / AUDITED: 27 (complete) |
| [`backend-sdlrenderer`](manifest/backend-sdlrenderer.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`backend-software`](manifest/backend-software.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`backend-vulkan`](manifest/backend-vulkan.md) | 40 | PENDING: 0 / AUDITED: 40 (complete) |
| [`backend-webgpu`](manifest/backend-webgpu.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |

## CNA internal core (non-backend) (205 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`cna-devices`](manifest/cna-devices.md) | 39 | PENDING: 0 / AUDITED: 39 (complete) |
| [`cna-graphics`](manifest/cna-graphics.md) | 75 | PENDING: 68 / AUDITED: 7 (grown by `plan_modern.md` MOD-12) |
| [`cna-input`](manifest/cna-input.md) | 31 | PENDING: 0 / AUDITED: 31 (complete) |
| [`cna-internal-core`](manifest/cna-internal-core.md) | 113 | PENDING: 0 / AUDITED: 113 (complete) |
| [`cna-root-utilities`](manifest/cna-root-utilities.md) | 15 | PENDING: 0 / AUDITED: 15 (complete) |

## Microsoft.Xna.Framework — public API (include+src) (541 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`xna-audio`](manifest/xna-audio.md) | 31 | PENDING: 0 / AUDITED: 31 (complete) |
| [`xna-content`](manifest/xna-content.md) | 15 | PENDING: 0 / AUDITED: 15 (complete) |
| [`xna-framework-core`](manifest/xna-framework-core.md) | 78 | PENDING: 0 / AUDITED: 78 (complete) |
| [`xna-gamerservices`](manifest/xna-gamerservices.md) | 89 | PENDING: 0 / AUDITED: 89 (complete) |
| [`xna-graphics`](manifest/xna-graphics.md) | 191 | PENDING: 0 / AUDITED: 191 (complete) |
| [`xna-input`](manifest/xna-input.md) | 44 | PENDING: 0 / AUDITED: 44 (complete) |
| [`xna-media`](manifest/xna-media.md) | 45 | PENDING: 0 / AUDITED: 45 (complete) |
| [`xna-net`](manifest/xna-net.md) | 42 | PENDING: 0 / AUDITED: 42 (complete) |
| [`xna-storage`](manifest/xna-storage.md) | 6 | PENDING: 0 / AUDITED: 6 (complete) |

## Microsoft.Devices — public API (include+src) (54 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`microsoft-devices`](manifest/microsoft-devices.md) | 54 | PENDING: 0 / AUDITED: 54 (complete) |

## Tests — Microsoft.Xna.Framework (235 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`tests-xna-audio`](manifest/tests-xna-audio.md) | 24 | PENDING: 0 / AUDITED: 24 (complete) |
| [`tests-xna-content`](manifest/tests-xna-content.md) | 30 | PENDING: 0 / AUDITED: 30 (complete) |
| [`tests-xna-framework-core`](manifest/tests-xna-framework-core.md) | 46 | PENDING: 0 / AUDITED: 46 (complete) |
| [`tests-xna-gamerservices`](manifest/tests-xna-gamerservices.md) | 16 | PENDING: 0 / AUDITED: 16 (complete) |
| [`tests-xna-graphics`](manifest/tests-xna-graphics.md) | 64 | PENDING: 0 / AUDITED: 64 (complete) |
| [`tests-xna-input`](manifest/tests-xna-input.md) | 24 | PENDING: 0 / AUDITED: 24 (complete) |
| [`tests-xna-media`](manifest/tests-xna-media.md) | 22 | PENDING: 0 / AUDITED: 22 (complete) |
| [`tests-xna-net`](manifest/tests-xna-net.md) | 9 | PENDING: 0 / AUDITED: 9 (complete) |

## Tests — CNA internals / Microsoft.Devices / misc (107 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`tests-cna-core`](manifest/tests-cna-core.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |
| [`tests-cna-devices`](manifest/tests-cna-devices.md) | 10 | PENDING: 0 / AUDITED: 10 (complete) |
| [`tests-cna-input`](manifest/tests-cna-input.md) | 5 | PENDING: 0 / AUDITED: 5 (complete) |
| [`tests-cna-internal`](manifest/tests-cna-internal.md) | 65 | PENDING: 0 / AUDITED: 65 (complete) |
| [`tests-microsoft-devices`](manifest/tests-microsoft-devices.md) | 25 | PENDING: 0 / AUDITED: 25 (complete) |
| [`tests-misc`](manifest/tests-misc.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |

## Tools (first-party tooling) (85 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`tools-audio`](manifest/tools-audio.md) | 6 | PENDING: 0 / AUDITED: 6 (complete) |
| [`tools-avatar-asset-pipeline`](manifest/tools-avatar-asset-pipeline.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`tools-avatar-builder`](manifest/tools-avatar-builder.md) | 15 | PENDING: 0 / AUDITED: 15 (complete) |
| [`tools-cna-reference`](manifest/tools-cna-reference.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`tools-devices`](manifest/tools-devices.md) | 5 | PENDING: 0 / AUDITED: 5 (complete) |
| [`tools-fna-reference`](manifest/tools-fna-reference.md) | 7 | PENDING: 0 / AUDITED: 7 (complete) |
| [`tools-gltf-to-cnj`](manifest/tools-gltf-to-cnj.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |
| [`tools-input-parity`](manifest/tools-input-parity.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`tools-net`](manifest/tools-net.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`tools-xna-oracle`](manifest/tools-xna-oracle.md) | 42 | PENDING: 0 / AUDITED: 42 (complete) |

## Examples — backend-named integration test executables (570 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`examples-tests-ascii`](manifest/examples-tests-ascii.md) | 6 | PENDING: 0 / AUDITED: 6 (complete) |
| [`examples-tests-bgfx`](manifest/examples-tests-bgfx.md) | 98 | PENDING: 0 / AUDITED: 98 (complete) |
| [`examples-tests-canvas`](manifest/examples-tests-canvas.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`examples-tests-d3d11`](manifest/examples-tests-d3d11.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-tests-d3d12`](manifest/examples-tests-d3d12.md) | 2 | PENDING: 0 / AUDITED: 2 (complete) |
| [`examples-tests-d3d9`](manifest/examples-tests-d3d9.md) | 14 | PENDING: 0 / AUDITED: 14 (complete) |
| [`examples-tests-dx3`](manifest/examples-tests-dx3.md) | 9 | PENDING: 0 / AUDITED: 9 (complete) |
| [`examples-tests-easygl`](manifest/examples-tests-easygl.md) | 218 | PENDING: 0 / AUDITED: 218 (complete) |
| [`examples-tests-generic`](manifest/examples-tests-generic.md) | 24 | PENDING: 0 / AUDITED: 24 (complete) |
| [`examples-tests-headless`](manifest/examples-tests-headless.md) | 7 | PENDING: 0 / AUDITED: 7 (complete) |
| [`examples-tests-sdlgpu`](manifest/examples-tests-sdlgpu.md) | 22 | PENDING: 0 / AUDITED: 22 (complete) |
| [`examples-tests-sdlrenderer`](manifest/examples-tests-sdlrenderer.md) | 67 | PENDING: 0 / AUDITED: 67 (complete) |
| [`examples-tests-software`](manifest/examples-tests-software.md) | 6 | PENDING: 0 / AUDITED: 6 (complete) |
| [`examples-tests-vulkan`](manifest/examples-tests-vulkan.md) | 70 | PENDING: 0 / AUDITED: 70 (complete) |
| [`examples-tests-webgpu`](manifest/examples-tests-webgpu.md) | 22 | PENDING: 0 / AUDITED: 22 (complete) |

## Examples — demo applications (110 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`examples-common`](manifest/examples-common.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_2d`](manifest/examples-demo_2d.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_achievement_showcase`](manifest/examples-demo_achievement_showcase.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar`](manifest/examples-demo_avatar.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar_animation_gallery`](manifest/examples-demo_avatar_animation_gallery.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar_appearance_tint_studio`](manifest/examples-demo_avatar_appearance_tint_studio.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar_bone_state_boundary`](manifest/examples-demo_avatar_bone_state_boundary.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar_dual_compare`](manifest/examples-demo_avatar_dual_compare.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar_multi_attach_stress`](manifest/examples-demo_avatar_multi_attach_stress.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_avatar_wardrobe_hotswap`](manifest/examples-demo_avatar_wardrobe_hotswap.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_devices`](manifest/examples-demo_devices.md) | 35 | PENDING: 0 / AUDITED: 35 (complete) |
| [`examples-demo_friends_and_gamercard`](manifest/examples-demo_friends_and_gamercard.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_gamer_profile_privileges`](manifest/examples-demo_gamer_profile_privileges.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_gamer_roster_hud`](manifest/examples-demo_gamer_roster_hud.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_gamerservices_dispatcher_watchdog`](manifest/examples-demo_gamerservices_dispatcher_watchdog.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_gamerservices_signin_presence`](manifest/examples-demo_gamerservices_signin_presence.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_guide_overlay_console`](manifest/examples-demo_guide_overlay_console.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |
| [`examples-demo_input`](manifest/examples-demo_input.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_leaderboard_viewer`](manifest/examples-demo_leaderboard_viewer.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_net_avatar_sync`](manifest/examples-demo_net_avatar_sync.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_net_client_server_arena`](manifest/examples-demo_net_client_server_arena.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_packet_roundtrip`](manifest/examples-demo_packet_roundtrip.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |
| [`examples-demo_qos_probe`](manifest/examples-demo_qos_probe.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |
| [`examples-demo_session_browser`](manifest/examples-demo_session_browser.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_session_lifecycle_events`](manifest/examples-demo_session_lifecycle_events.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |
| [`examples-demo_simulated_network_conditions`](manifest/examples-demo_simulated_network_conditions.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_sound`](manifest/examples-demo_sound.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`examples-demo_xact`](manifest/examples-demo_xact.md) | 4 | PENDING: 0 / AUDITED: 4 (complete) |
| [`examples-input_smoke`](manifest/examples-input_smoke.md) | 1 | PENDING: 0 / AUDITED: 1 (complete) |

## Documentation (docs/*.md) (72 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`docs`](manifest/docs.md) | 72 | PENDING: 0 / AUDITED: 72 (complete) |

## Build / CI / CMake / scripts (47 files)

| Shard | Files | Status rollup |
|---|---|---|
| [`build-ci`](manifest/build-ci.md) | 3 | PENDING: 0 / AUDITED: 3 (complete) |
| [`build-cmake`](manifest/build-cmake.md) | 13 | PENDING: 0 / AUDITED: 13 (complete) |
| [`build-cmake-tests`](manifest/build-cmake-tests.md) | 14 | PENDING: 0 / AUDITED: 14 (complete) |
| [`build-root`](manifest/build-root.md) | 5 | PENDING: 0 / AUDITED: 5 (complete) |
| [`scripts`](manifest/scripts.md) | 12 | PENDING: 0 / AUDITED: 12 (complete) |

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
