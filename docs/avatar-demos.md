# Avatar demos

The avatar demos use only the standard XNA avatar API (`AvatarDescription`, `AvatarAnimation`,
`AvatarRenderer`) on CNA's original avatar catalog, described in [avatars.md](avatars.md). Each creates a
`GraphicsDeviceManager` and adds a `GamerServicesComponent`, which is all `AvatarRenderer` needs;
no avatar-specific extension or application content directory is needed. CLI/capture helpers are
demo utilities rather than additions to XNA's avatar API.

## Build availability

The four `cna_demo_avatar*` targets and `cna_demo_net_avatar_sync` are currently registered only
with `CNA_BUILD_EXAMPLES=ON`, a native build and a default renderer of OPENGLES3 or OPENGL33.
Gamer-services must be enabled, and the network demo also requires Net. Merely compiling a GL
renderer into a multi-renderer binary is insufficient when another renderer is the default.
These target gates do not describe the wider renderer support of the avatar API.

`cna_avatar_editor`, `cna_avatar_review` and `cna_guide_review` use a separate native example gate
that excludes only HEADLESS/STUB defaults. They are not the same target family as the four demos.

## Controls

Each demo's in-app **F1** overlay is the authoritative control reference; **Esc** quits.

| Demo | What it demonstrates | CLI flags |
|---|---|---|
| `demo_avatar` | A random avatar, a lineup of five, or one given description, playing any preset, for looking at avatars closely: camera views of the whole body, the head, the hands and the feet, lighting a game might use (studio, daylight, sunset, dim room). Space/Backspace: next/previous preset, R: new avatars, G: other body type, L: lineup, Tab: next of the lineup, V: view, K: lighting, E: expression overrides, P: pause, Left/Right: orbit, Up/Down: tilt, PgUp/PgDn: zoom. | `--gender male\|female`, `--clip <preset>`, `--lineup N`, `--view body\|head\|hands\|feet`, `--description <hex\|file>`, `--yaw <degrees>`, `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_animation_gallery` | Eight avatars playing the 31 presets a page at a time. Space: next page, R: new avatars. | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_dual_compare` | A female and a male avatar playing the same preset side by side, showing how one standard animation fits different heights and builds. Space: next preset, R: new pair. | `--smoke N`, `--screenshot <path>`, `--show-help` |
| `demo_avatar_bone_state_boundary` | The renderer lifecycle (`Unavailable`, `Loading`, `Ready`, `BindPose`, `ParentBones`) printed to the console, then a draw from game-supplied bone transforms instead of an `AvatarAnimation`. | `--screenshot <path>`, `--show-help` (runs a fixed window of frames) |
| `demo_net_avatar_sync` | Two processes in a System Link `NetworkSession`: each sends its avatar's `Description` bytes (reliable, in order) and its position, yaw and preset, and draws the other player's avatar. Arrows: move and turn, Space: next preset. | `--host` / `--join`, `--smoke N`, `--screenshot <path>`, `--show-help` |

The networking demos (`demo_net_avatar_sync` and the other SystemLink demos in
`modules/net/examples/`) need a signed-in gamer: they open the Guide's sign-in, which without a
service signs in a local offline profile. For unattended runs set
`CNA_GAMER_SERVICES_AUTO_SIGN_IN=<profile name>` (a different name per process), and
`CNA_GAMER_SERVICES_PROFILES_DIR` to keep the profiles out of your own store.

For the ordinary avatar/gallery/dual demos, `--smoke N` counts Update calls, not Draw calls;
fixed-step catch-up can therefore produce fewer than N rendered frames. Screenshot capture is
attempted during Draw near the end of that update budget. Use a sufficiently long run for async
avatar loading and inspect the output; very short runs need not yield a ready avatar or screenshot.
The bone-state boundary demo instead runs its own bounded lifecycle, including a window after
Ready. Avatar sync has a separate final-update/screenshot handshake. The demos select HiDef for
backbuffer readback. Run them through
`tools/platform/run_gpu_tests_private.sh`'s private display, never on a live desktop.

## Troubleshooting: network demo startup (ENet port binding, discovery)

Applies to any Net-using demo (`demo_net_avatar_sync`, `demo_net_client_server_arena`,
`demo_session_browser`, etc.), not just avatar-related ones — collected here since
`demo_net_avatar_sync` is the avatar demo that exercises it.

- **"No session found after searching - is a host running?"** — the client
  (`NetworkSession::Find`) uses the discovery search window (`kSearchWindowMs`, currently 150 ms).
  The avatar-sync demo, not Find itself, adds up to 100 retry searches separated by 50 ms.
  A host must already be running and
  past its own `NetworkSession::Create` call before the client starts searching. Launch the host
  process first, give it a moment to finish creating its session, then launch the client — a tight
  race between the two process launches can produce a temporary empty result. The host-migration
  cross-process integration test (`TwoProcessLoopbackTest.cpp`, Net Task 5.5) uses exactly this ordering (host, brief pause, then client) for
  the same reason.
- **"Failed to bind discovery UDP socket."** — discovery binds UDP port 61190 with
  `SO_REUSEADDR`, plus `SO_REUSEPORT` on Apple, for the platform's shared discovery socket rules.
  Check the bind diagnostic and socket/process ownership; stop only the specific demo processes
  belonging to the test. A packet-filtering or broadcast problem can prevent discovery without
  causing a bind error, so diagnose those separately. Linux and Apple socket behavior must not be
  treated as a Windows qualification.
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
  should be tested with a real transport disconnect and surviving peers. The permanent reference
  is `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`
  in `modules/net/tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp`; pausing the original host
  process is a different failure condition and is not equivalent to that test.

## Verification

The 2026-11-01 documentation audit built all five targets in the existing GL-default multi tree.
The four local avatar demos completed bounded private-display runs and wrote screenshots;
the dual/gallery captures were inspected. The network demo was built but its two-process play was
not rerun as part of that smoke. These checks validate current demo startup/capture, not every
animation, catalog combination or network/hardware environment.
