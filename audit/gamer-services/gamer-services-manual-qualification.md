# GamerServices manual qualification plan — GS-AUDIT-P4

**NOT EXECUTED / NO PASS CLAIM.** Software audit closes with these device, network and platform limitations. Run only on disposable lab machines/VMs and approved devices, never the owner's live desktop. Linux window-capable runs must use `tools/platform/run_gpu_tests_private.sh`; Windows/macOS use an isolated lab session/VM. Keep raw passwords/tokens and room recordings out of retained evidence. This plan does not authorize destructive system crashes or power cuts.

## Common setup and evidence

Use the native/server Phase 4 commits from the [handoff](gamer-services-handoff-phase4.md). Build the four `cna_service_*_client_harness` targets and tests to completion before launching anything. Provision a disposable SQLite service with a trusted TLS certificate and accounts alice/Alice, bob/Bob, charlie/Charlie, dana/Dana. Copy the exact title/achievement/board setup from server `tests/cna_session_e2e.py` (BestScoreLifeTime, Rounds:int32, arbitrated Kills). Server README documents admin `title`, `user`, `achievement`, `leaderboard`, `privilege` and other commands; user passwords enter via stdin, never command-line arguments/logs.

Configure each native client with `CNA_GAMER_SERVICES_ENDPOINT=https://<qualified-name>:<port>/cna/v1`, `CNA_GAMER_SERVICES_CA_BUNDLE=<lab CA>`, `CNA_GAME_ID=<provisioned title>`. Use actual remote service address, not 127.0.0.1 on two hosts. Record OS/compiler/build hashes, endpoint address family, TLS verification, topology/router/NAT evidence, feature capabilities, device IDs and timestamped per-process logs. Capture server aggregate outcomes and database reads/integrity checks where indicated. Never label a loopback run as LAN/WAN/NAT qualification.

Session harness sequence on Linux (run host A and joiner B in separate private runners):

```sh
tools/platform/run_gpu_tests_private.sh --exec cmake-build-debug/cna_service_session_client_harness host player
tools/platform/run_gpu_tests_private.sh --exec cmake-build-debug/cna_service_session_client_harness join player
# Repeat with ranked, and optional third argument invite / add / migrate / crash.
```

Provide passwords for Alice/Charlie on A and Bob/Dana on B through stdin. Launch B after A prints `session-created`. At both `session-roster`, feed `continue`; require `session-exchanged 5` or `6`, with all five reliable packets verified. Feed `continue` at exchange and playing boundaries, then at `session-lobby`. Final normal departure order: PlayerMatch joiner then host; Ranked host then joiner. Each input gate has a 60-second timeout: use a coordinator/pipes if necessary. Require `session-done`, zero exit and expected persisted scores/columns. Public current harness is session qualification, not physical voice UI; voice procedures below require a small title/probe that continuously pumps Dispatcher and NetworkSession and exposes Guide/social/voice state.

## Test cases

| ID / environment | Setup | Steps | Expected result | Logs/evidence to capture |
|---|---|---|---|---|
| M01 Linux host/client | two Linux machines, IPv4 LAN, remote service | run PlayerMatch then Ranked sequence above; repeat reversed host roles | current roster, all reliable payloads, gameplay/events/boards/leave; optional unreliable packet can be lost | both harness logs, server outcomes, hashes/addresses, board reads |
| M02 Windows host/client | two supported Windows lab builds, same service | execute M01, then Windows↔Linux both host roles; isolate user data | same functional behavior; bounded failures, correct property/event mapping | compiler/STL/OS versions, native logs and actual addresses |
| M03 macOS host/client | two supported macOS lab builds/APFS | execute M01, then macOS↔Linux both host roles | same sequence, identity and persistence; no unqualified Xbox parity claim | architecture/OS/compiler, logs, board snapshots |
| M04 physical SystemLink LAN | two machines on same broadcast LAN; build an existing game or remote-address-capable qualification adapter | discover/join actual remote host; exchange bytes; leave/rejoin; remove host | LAN discovery/packet identity/QoS behave as implemented, failures bounded | interfaces/broadcast routes, observed endpoint/RTT, packet/event logs. Existing two-process harness hardcodes loopback client address and cannot alone qualify this case |
| M05 WAN relay / NAT types | two clients on different routers, public TLS service; document full-cone/restricted/port-restricted/symmetric or CGNAT facts | repeat public session/invite sequence for each available topology with no inbound port mapping | outbound WSS relay carries packets; no claim of peer hole punching; reliable assertions unchanged | router configs/type measurement, outbound connection facts, timestamped client/server logs |
| M06 direct Internet / relay boundary | WAN setup; separate SystemLink LAN reference | attempt only supported online creation/join; make relay unavailable once | online starts on relay; unavailable relay gives bounded failure and rollback. Direct Internet peers and STUN/ICE are **UNSUPPORTED**, not a missing “fallback pass” | configured path/capabilities, refusal/rollback and deadlines; no invented direct-success assertion |
| M07 IPv4 / IPv6 | public IPv4 endpoint, then service bound to `::` with certificate-matching IPv6 DNS/literal; force external v6 path | repeat session/restart/recovery over each endpoint; separately test v6-only/NAT64 if deployment needs it | HTTP/WSS external IPv6 path works if supported by platform; ENet local carrier/LAN remains IPv4. Unsupported deployment must fail clearly | external DNS/routes/address family, TLS errors/success, carrier separation, no IPv4 fallback disguised as v6 success |
| M08 graceful migration / crash | M01/M05, harness `migrate` then `crash` | after lobby, leave host or terminate **only disposable host process**; continue survivor | HostChanged Alice→Bob, old gamers removed, survivor authoritative, continued session and bounded timeout | event/roster logs before/after; process termination timing; no system crash |
| M09 disconnect/reconnect | WAN/LAN disposable clients | interrupt client connectivity for 5 s and restore; separately exceed 15 s recovery budget; re-sign-in/rejoin | temporary recovery or bounded SessionEnded; old grants/membership not incorrectly reused; restored admission works | measured outage, reconnect/termination reason, directory rows/grant outcomes |
| M10 server restart | disposable service and live session | stop/restart service at same endpoint (~1 s) at roster boundary, then repeat longer outage | short restart recovers directory/relay; longer failure terminates within documented budgets; durable awards/boards survive | before/after SQLite state and integrity, server startup, client recovery/events |
| M11 client restart / replay | authenticated lab account and provisioned key/board | award and commit; terminate client after successful response; reopen/login/read; repeat identical requests, then contradictory score epoch | first completion time preserved, board columns/rating exact; same report no corruption; contradictory committed epoch refused | safe request IDs/outcome codes, ticks/int64 values, restart logs; no tokens |
| M12 physical microphone input | approved Linux microphone/speaker, PipeWire tools + NumPy, built `cna_net_two_process_harness` | run command below; then speak briefly between two lab machines in a live session | actual capture→Opus→remote decode detected; unplug/replug behaves safely; one local talker/machine is expected | numeric tone results, selected device/format, capture state; recordings discarded |
| M13 physical voice output | same approved devices; lab title/probe | run physical script output half, then remote speech through live session | decoded real packets produce sound on selected physical output; unplug/replug/decode failure safe | numeric spectrum/energy, output format/state, audible lab observation; no broad quality certification |
| M14 live mute/block/privilege | two continuous voice clients A/B, independent controller signed into A's account, operator CLI | confirm voice both ways; local mute Bob at A; unmute; controller block Bob while A stays signed in; restore; operator revoke A communication, then restore | local mute/block suppress **subsequent** playback/capture targeting Bob; external policy follows next successful heartbeat/Update plus up to permission interval, not immediate relay revocation; same gamer object persists | action/server commit times, current privilege/block snapshots, packet/capture/playback counters, HasVoice/IsTalking/Muted, measured policy latency; queued sound distinguished |
| M15 friendship/privacy changes | A has FriendsOnly policy; A/B accepted friends, live voice, third controller | remove friendship without leaving session; confirm policy at next permission refresh; re-add/accept; change Everyone→FriendsOnly→Blocked through operator | fresh `IsFriend` read eventually denies former friend and restoration works; failed read denies; no persistent sign-in-only cache | current friend rows, per-action times, state/playback counters; verify matched `policy-refresh` capability |
| M16 offline storage restart | isolated XDG/app profile on Linux, Windows NTFS, macOS APFS; scalar/int64/tick fixture | write award/board, close app, reopen/fresh process; repeat overlapping writers and Unicode profile paths; preserve manual backups | completed normal writes visible/exact; cooperating writers retain all keys; first duplicate time stays; unsupported streams/column-only saves not claimed | actual file paths, hashes/content/ticks, process exit/status, filesystem/compiler, restart results |
| M17 platform replacement/locks/failures | same platform fixture; disposable store; local filesystem | existing-target replace; open reader during replace; Windows readers with/without delete sharing; obstruct temp/lock paths; deny directory write; construct abandoned temp and corrupt target; retry after repair; kill only test writer | old target survives failed update; no unlocked write; success visible; old open reader may retain old bytes; temp ignored; corruption preserved and update fails; lock released after process exit | old/new file hashes, exception/status, ACL/sharing settings, lock/temp inventory; Windows/macOS runtime must actually execute before marking verified |
| M18 scale/long churn (deployment-specific) | representative provisioned account/title count and lab load generator, isolated service | sustain session/social/download usage within stated limits; record authenticated budget cardinality/cleanup and account churn | memory/latency within deployment budget; refusal/recovery at caps; no claim of cluster support or bounded retired gamer reclamation | RSS/cardinality/latency/load/timeout series and topology; optional capacity acceptance, not a generic new audit |
| M19 avatar GPU/platform | Linux private GPU runner and isolated Windows/macOS lab builds; provision CNA catalog, local and online avatar layouts | render standard AvatarRenderer.Draw with neutral and animated poses, multiple expressions/body layouts, reload changed catalog/layout; exercise loading/unavailable/dispose | actual skinned 71-bone model and textures draw without errors; state/disposal behavior correct; compare CNA expected poses, not unsupported Xbox asset parity | screenshots/frame captures, renderer/GPU/driver/build/catalog hashes, bone/pose diagnostics and graphics errors |

Physical Linux probe (select approved devices first; the script uses system defaults and emits a short tone):

```sh
tools/platform/run_gpu_tests_private.sh --exec tools/net/voice_physical_check.sh \
  cmake-build-debug /tmp/gs-phase4-physical-voice
```

Windows/macOS microphone/output M12–M15 require their actual platform recording/playback adapters and a lab title/probe; the PipeWire shell script is Linux-specific. Record adapters and device permissions. No software test is a substitute.

## Seven unchanged isolated-network gates

Prerequisites: Linux `unshare`, `ip`, `slirp4netns`, permitted user/network namespaces; `CNA_SERVICE_SLIRP4NETNS` may select a real helper. Establish `unshare --user --map-root-user --net true` succeeds. Supply all four fresh harness paths and avatar catalogs, as in the handoff. Execute:

```sh
tools/platform/run_gpu_tests_private.sh --exec ctest --test-dir ../cna-gamer-services-server/build \
  --output-on-failure -j2 \
  -R '^service_cna_(relay_nat|owned_enet_nat|session_nat|invite_nat|session_restart_nat|session_host_crash|session_add_gamer)$'
```

| Gate | Setup/steps and expected result | Evidence |
|---|---|---|
| `service_cna_relay_nat` | raw relay script `--isolated`; distinct outbound-only client namespaces exchange authorized frames | namespace IDs/routes and framing/revocation assertions |
| `service_cna_owned_enet_nat` | owned relay script `--owned --isolated`; owned preparation/membership/game engine crosses isolation | transport/roster/identity assertions |
| `service_cna_session_nat` | public session `--isolated`; PlayerMatch/Ranked create/find/join/game/board sequence | all public API/session/board assertions |
| `service_cna_invite_nat` | public script `--invite --isolated`; real Guide consent and JoinInvited | invite recipient/scope/acceptance and packet logs |
| `service_cna_session_restart_nat` | `--restart --isolated`; restart service during session | reconnect/directory/relay sequence and timeout timings |
| `service_cna_session_host_crash` | `--crash --isolated`; terminate disposable host, migration survives | old/new host roster/event/packet assertions |
| `service_cna_session_add_gamer` | `--add --isolated`; admit Dana after initial join | additional account/machine/roster/packet assertions |

Require **all seven execute without return 77**, preserve their original assertions, distinct namespace inodes and genuine isolated routes. Missing tools/namespaces remain SKIPPED, never passes. Existing loopback crash/add passes do not remove these stronger gates. Namespace NAT success still does not prove physical WAN or symmetric NAT.

Application process interruption is safe to qualify with disposable files. OS crash/power-loss simulation is not part of this plan's execution authorization: the declared OS-cache contract promises neither. A future explicitly authorized storage-lab test may measure loss behavior without reclassifying ordinary persistence as power-loss-durable.
