# Avatar demo controls and troubleshooting

**Date:** 2026-07-17 (`plans/plan_net.md` Tasks 9.6/9.7)

Quick reference for the 8 avatar-related demos (`demo_avatar`, `demo_avatar_animation_gallery`,
`demo_avatar_appearance_tint_studio`, `demo_avatar_bone_state_boundary`,
`demo_avatar_dual_compare`, `demo_avatar_multi_attach_stress`, `demo_avatar_wardrobe_hotswap`,
`demo_net_avatar_sync`) plus troubleshooting for the asset-generation pipeline behind them and for
network demo startup in general.

## Controls

**The authoritative control reference is each demo's own in-app F1 help overlay** (`plans/plan_net.md`
Phase 8) — press F1 while any of the 8 demos is running to see its exact current controls. This
file deliberately does not duplicate that text line-by-line (a duplicated list drifts out of sync
the next time a control changes); it only summarizes what's common across demos and points at
each one's own overlay for the rest.

Common to all 8: **F1** toggles the help overlay, **Esc** quits.

| Demo | What it demonstrates | Notable CLI flags |
|---|---|---|
| `demo_avatar` | Baseline real avatar rendering | `--gender male\|female`, `--wardrobe-hair Cap\|Ponytail`, `--yaw`, `--clip`, `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_animation_gallery` | Auto-cycles all 31 `AvatarAnimationPreset` clips across both genders | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_appearance_tint_studio` | Live per-slot (Skin/Hair/Shirt/Pants/Shoes) color tinting via `AvatarAppearanceEXT` | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_bone_state_boundary` | Documents the real, faithful-XNA `AvatarRenderer` skeleton API boundary (console output) vs. the working `SkinnedModelEXT` EXT path | `--screenshot <path>`, `--show-help` (no `--smoke` — always runs a fixed ~30-frame window) |
| `demo_avatar_dual_compare` | Two independent `AvatarRenderer`/`SkinnedModelEXT` instances drawing simultaneously, proving per-instance appearance isolation | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_multi_attach_stress` | Repeated `SkinnedModelEXT::AttachPartEXT` calls, proving accumulation doesn't break skinning/tinting | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_wardrobe_hotswap` | `AttachPartEXT`/`RemovePartEXT` used live at runtime (hair changes without restarting) | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_net_avatar_sync` | Net + Avatar combined: two real processes sync position/yaw/clip over a real `NetworkSession` | `--host` / `--join`, `--smoke N`, `--screenshot <path>`, `--show-help` |

`--smoke N` exits cleanly after N `Draw()` frames (automated verification, no interactive input
needed). `--show-help`/`--screenshot <path>` together let a non-interactive run verify the F1
overlay actually renders, without simulating keyboard input — see `plans/plan_net.md` Phase 8 for why
that pairing exists.

## Troubleshooting: avatar asset generation (mesh-craft + Blender pipeline)

See `tools/avatar_builder/README.md` for the full pipeline description. Common failure points:

- **`mc3togltf` not found** — `generate_body_meshcraft.py`/`generate_clothes_meshcraft.py` need
  the sibling [`mesh-craft`](../../mesh-craft) tool's `mc3togltf` CLI built and resolvable.
  `_locate_mc3togltf()` checks the `$MC3TOGLTF` environment variable first, then a handful of
  conventional build-output paths relative to the sibling repo. If neither resolves, set
  `MC3TOGLTF=/path/to/mc3togltf` explicitly before running `generate_avatar.py`/
  `generate_wardrobe.py`.
- **Body/clothing geometry looks disproportionate or intersecting** — confirm you're actually
  running the mesh-craft pipeline, not the older primitive-join one: `generate_avatar.py`/
  `generate_wardrobe.py` alias `generate_body_meshcraft`/`generate_clothes_meshcraft` in as
  `generate_body`/`generate_clothes`, but the original, unmerged-geometry modules
  (`generate_body.py`/`generate_clothes.py`) remain independently runnable and will reproduce the
  original "monster" mesh-explosion symptoms if invoked directly by mistake.
  See `docs/avatar-real-rendering-ext.md`'s "Phase 7" section for the root cause.
  **Known, still-open residual gaps** (not bugs to re-report): a shoe-area dark artifact and a
  `Wave`-pose chest-band artifact — see `plans/plan_net.md` Phase 7's own "Honest overall assessment".
  Genuinely new/different-looking artifacts are worth investigating; these two specific ones are
  already tracked.
- **`validate_gltf.py` passes but the content still looks wrong at runtime** — `validate_gltf.py`
  checks mesh/joint/animation/shape-key *presence*, not per-vertex correctness — it does not catch
  NaN/Inf weight values or out-of-range bone indices (a known gap, `plans/plan_net.md` Task 7.8/7.10).
  If a model loads but renders garbled, inspect the actual buffer data, don't assume `validate_gltf.py`
  passing rules that out.
- **CMake doesn't pick up regenerated `Content/` after re-running the pipeline** — CMake's own
  `Content/` copy step only re-triggers on a build reconfigure, not automatically when only binary
  content files change underneath it. Re-run `cmake --build` after touching `CMakeLists.txt` (or
  manually copy `modules/gamer-services/examples/demo_avatar/Content/` into the build directory) if a demo still shows
  stale content after regenerating assets.
- **Regenerating content changes vertex/animation output slightly between runs** — expected:
  Blender's own floating-point rounding introduces ~1-ULP (`2^-23`) noise between otherwise
  byte-identical exports (`tools/avatar_builder/README.md`'s own "Orchestration and export"
  section documents this precisely) — not a sign of a broken pipeline.

## Troubleshooting: network demo startup (ENet port binding, discovery)

Applies to any Net-using demo (`demo_net_avatar_sync`, `demo_net_client_server_arena`,
`demo_session_browser`, etc.), not just avatar-related ones — collected here since
`demo_net_avatar_sync` is the avatar demo that exercises it.

- **"No session found after searching - is a host running?"** — the client
  (`NetworkSession::Find`) searches for `kSearchWindowMs` (150ms) per attempt and retries up to
  100 times (`src/Net/Internal/ENetDiscoveryService.cpp`); a host must already be running and
  past its own `NetworkSession::Create` call before the client starts searching. Launch the host
  process first, give it a moment to finish creating its session, then launch the client — a tight
  race between the two process launches (both started in the same shell command with no delay) is
  the most common cause of a spurious "not found," not a real bug. `plans/plan_net.md` Phase 5's own
  cross-process integration test uses exactly this ordering (host, brief pause, then client) for
  the same reason.
- **"Failed to bind discovery UDP socket."** — the discovery service binds a well-known UDP port
  (61190) with `SO_REUSEADDR` specifically so multiple independent host/client *processes* on the
  same machine can each bind it (confirmed empirically reliable on Linux; Windows' `SO_REUSEADDR`
  has different, looser semantics and is expected to work but is not independently verified in
  this project's Linux-only dev environment). If this still fails, something else on the machine
  (a firewall rule, a completely unrelated process, or a stale process from a crashed previous
  run) is holding that exact port in a way `SO_REUSEADDR` can't share around — check for and kill
  any leftover demo processes (`pkill -f cna_demo_net`) before retrying, and check firewall rules
  if it persists on a fresh machine.
  - Note this is **specifically** the discovery port, not the real game-session transport port —
    `NetworkSession::Create`'s own ENet host binds an OS-assigned ephemeral port (`CreateHost(0,
    ...)`, native platforms), so session-level connections essentially never hit a fixed-port
    binding conflict; only the shared discovery port can.
- **Real cross-process play works locally but not across machines** — `SystemLink` discovery uses
  UDP broadcast (`ENET_SOCKOPT_BROADCAST`) plus a loopback fallback for same-machine testing; a
  broadcast packet does not cross routed network segments/VLANs by design (standard UDP broadcast
  behavior, not a CNA limitation) — both host and client must be on the same local broadcast
  domain (e.g. the same Wi-Fi/switch segment) for discovery to find each other automatically.
- **Host migration (`AllowHostMigration`) doesn't seem to promote a new host** — host migration
  targets the specific 3-real-process scenario `plans/plan_net.md` Phase 5's own integration test
  exercises (the original host process actually exiting/disconnecting); it does not run
  speculatively or "just in case" — confirm the original host process actually terminated, not
  just became unresponsive within the same process.
