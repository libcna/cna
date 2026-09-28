# Avatar demos

The avatar demos use only the standard XNA avatar API (`AvatarDescription`, `AvatarAnimation`,
`AvatarRenderer`) on CNA's original avatar catalog, described in `avatars.md`. Each creates a
`GraphicsDeviceManager` and adds a `GamerServicesComponent`, which is all `AvatarRenderer` needs;
no extension call and no content directory is involved.

## Controls

Each demo's in-app **F1** overlay is the authoritative control reference; **Esc** quits.

| Demo | What it demonstrates | CLI flags |
|---|---|---|
| `demo_avatar` | One random avatar playing any preset, with a free camera. Space: next preset, R: new avatar, G: other body type, E: cycle expression overrides, Left/Right: orbit. | `--gender male\|female`, `--clip <preset>`, `--yaw <degrees>`, `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_animation_gallery` | Eight avatars playing the 31 presets a page at a time. Space: next page, R: new avatars. | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_dual_compare` | A female and a male avatar playing the same preset side by side, showing how one standard animation fits different heights and builds. Space: next preset, R: new pair. | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_bone_state_boundary` | The renderer lifecycle (`Unavailable`, `Loading`, `Ready`, `BindPose`, `ParentBones`) printed to the console, then a draw from game-supplied bone transforms instead of an `AvatarAnimation`. | `--screenshot <path>`, `--show-help` (runs a fixed window of frames) |
| `demo_net_avatar_sync` | Two processes in a System Link `NetworkSession`: each sends its avatar's `Description` bytes (reliable, in order) and its position, yaw and preset, and draws the other player's avatar. Arrows: move and turn, Space: next preset. | `--host` / `--join`, `--smoke N`, `--screenshot <path>`, `--show-help` |

The networking demos (`demo_net_avatar_sync` and the other SystemLink demos in
`modules/net/examples/`) need a signed-in gamer: they open the Guide's sign-in, which without a
service signs in a local offline profile. For unattended runs set
`CNA_GAMER_SERVICES_AUTO_SIGN_IN=<profile name>` (a different name per process), and
`CNA_GAMER_SERVICES_PROFILES_DIR` to keep the profiles out of your own store.

`--smoke N` exits after N drawn frames without input; `--screenshot <path>` saves the last frame's
back buffer as a PNG (the demos use the HiDef profile so it can be read back). Run them through
`tools/platform/run_gpu_tests_private.sh`'s private display, never on a live desktop.

## Troubleshooting: network demo startup (ENet port binding, discovery)

Applies to any Net-using demo (`demo_net_avatar_sync`, `demo_net_client_server_arena`,
`demo_session_browser`, etc.), not just avatar-related ones — collected here since
`demo_net_avatar_sync` is the avatar demo that exercises it.

- **"No session found after searching - is a host running?"** — the client
  (`NetworkSession::Find`) searches for `kSearchWindowMs` (150ms) per attempt and retries up to
  100 times (`modules/net/src/Internal/ENetDiscoveryService.cpp`); a host must already be running and
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
