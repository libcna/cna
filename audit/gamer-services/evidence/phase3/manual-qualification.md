# Phase 3 network / device qualification — GS-AUDIT-P3

2026-10-02. Tests here ran on private Weston/Xwayland, Debug/HEADLESS, native Linux and loopback TLS/WSS. No public Internet, physical LAN, microphone/speaker, Windows or macOS qualification is implied.

## Seven skips

All seven remain **ENVIRONMENT REQUIRED**. Each is relevant. No obsolete skip or bug masked by a skip was established. Every test is registered with return code 77; both scripts first require Linux, `unshare`, `ip`, and `slirp4netns` (`CNA_SERVICE_SLIRP4NETNS` may select the latter), then probe `unshare --user --map-root-user --net true`. The final run stopped at the missing-tool gate; `slirp4netns` is not in PATH. A configured nonexistent helper is an assertion failure, not a skip. Native harness variables were supplied, so their absence did not cause these skips.

| Exact CTest name | Subsystem / script flags | Reason / current relevance | Behavior still unqualified |
|---|---|---|---|
| service_cna_relay_nat | relay; cna_relay_e2e.py --isolated | missing namespace/NAT tooling; real raw relay gate | separate outbound-only IPv4 namespaces, framing and revocation across NAT |
| service_cna_owned_enet_nat | owned transport; cna_relay_e2e.py --owned --isolated | same gate; actual owned preparation path | owned ENet membership/game engine through NAT |
| service_cna_session_nat | public session; cna_session_e2e.py --isolated | same gate; PlayerMatch and Ranked | public Create/Find/Join/packets/game/boards under namespace NAT |
| service_cna_invite_nat | invitation; cna_session_e2e.py --invite --isolated | same gate; real Guide consent/join | InviteAccepted/JoinInvited across NAT |
| service_cna_session_restart_nat | reconnect; cna_session_e2e.py --restart --isolated | same gate; restart during a live session | directory/relay recovery under NAT |
| service_cna_session_host_crash | host migration; cna_session_e2e.py --crash --isolated | same gate; abrupt host loss | migration after killed host under NAT; new loopback counterpart passes |
| service_cna_session_add_gamer | membership; cna_session_e2e.py --add --isolated | same gate; adding Dana after joining | multi-account admission under NAT; new loopback counterpart passes |

Added regular CTest gates `service_cna_session_host_crash_loopback` and `service_cna_session_add_gamer_loopback`. Keeping the isolated originals preserves their stronger assertions; removing `--isolated` from them would weaken qualification.

## Current transport boundary

| Path | Status | Evidence / limit |
|---|---|---|
| Local/loopback | TESTED | 523 Net cases and paired real server/CNA processes |
| Physical LAN/SystemLink | IMPLEMENTED BUT UNQUALIFIED here | UDP discovery/ENet and loopback tests; two separate physical hosts not run |
| Online relay, IPv4 loopback TLS/WSS | TESTED | raw/owned/public session, invite, restart, migration/crash/add gates pass |
| Public Internet via relay | IMPLEMENTED BUT UNQUALIFIED | libcurl outbound WSS/TLS exists; no external routers/remote service tested |
| Direct peer Internet path | UNSUPPORTED in service session path | service ENet communicates through local loopback relay proxies, not externally advertised peer endpoints |
| NAT traversal/hole punching | UNSUPPORTED | no STUN/ICE/TURN/direct-path negotiation; relay is the initial online path, not fallback from a failed direct probe |
| Namespace NAT / symmetric NAT | IMPLEMENTED BUT UNQUALIFIED for relay connectivity | all seven namespace gates skip; symmetric physical NAT not tested; no symmetric-NAT hole-punch claim |
| IPv4 | TESTED for loopback | namespace tooling uses IPv4 `10.0.2.100`/gateway `10.0.2.2`; physical IPv4 remains unqualified |
| IPv6 | PARTIAL / IMPLEMENTED BUT UNQUALIFIED for service control/relay | generic Beast address binding and curl URL paths; LAN/local ENet routes use IPv4, no IPv6 E2E run |
| Reconnect | TESTED on loopback | real server restart; 15-second client relay recovery window, fresh tickets |
| Host migration | TESTED on loopback | graceful leave and killed host for PlayerMatch/Ranked; Internet/NAT not qualified |

`RelayWebSocket.cpp`: 3-second connect, 5-second upgrade and hello deadlines, 5-second partial-frame assembly timeout; failures surface as relay errors. `ServiceENetSession.cpp`: 15-second recovery, 30-second migration window, ENet peer timeout 20–30 seconds. Server disconnect shortens machine lease to 20 seconds; ordinary leases are 90 seconds. Authority loss is terminal rather than indefinitely retried. Relay grants are periodically revalidated and session/machine deletion cascades tickets. `RelayTransport::setRoutes` allocates replacements before removing obsolete machine routes. Tests prove loopback recovery and grant invalidation, not every packet-loss or address-family case.

## Resume namespace gates

Provision `slirp4netns`, `iproute2`, and util-linux on a Linux qualification host that permits unprivileged user/network namespaces. Verify both the tools and the namespace probe; do not run the gates without their original assertions.

From cnawork:

```sh
export CNA_SERVICE_SESSION_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_session_client_harness"
export CNA_SERVICE_DIRECTORY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_directory_client_harness"
export CNA_SERVICE_RELAY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_relay_client_harness"
export CNA_SERVICE_AVATAR_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_avatar_client_harness"
export CNA_AVATAR_CATALOGS="$PWD/modules/gamer-services/assets/avatars"
command -v slirp4netns ip unshare
unshare --user --map-root-user --net true
# Optionally: export CNA_SERVICE_SLIRP4NETNS=/absolute/path/to/slirp4netns
tools/platform/run_gpu_tests_private.sh --exec ctest --test-dir ../cna-gamer-services-server/build \
  -R '^service_cna_(relay_nat|owned_enet_nat|session_nat|invite_nat|session_restart_nat|session_host_crash|session_add_gamer)$' \
  --output-on-failure -j2
```

Require all seven to execute (no return 77), with distinct client namespace inodes, the same private IPv4 on separate namespaces, verified default routes and no published inbound ports. Save small logs including namespace IDs and outcome counts. Passing these gates still does not qualify real Internet or symmetric NAT.

## Physical LAN and Internet procedure

Use disposable provisioned titles/accounts; match the definitions in `tests/cna_session_e2e.py` (nonarbitrated BestScoreLifeTime with Rounds:int32, arbitrated Kills). Keep passwords interactive and tokens out of saved logs. Every native command runs through the private runner.

1. **Physical SystemLink:** on machine A run `tools/platform/run_gpu_tests_private.sh --exec cmake-build-debug/cna_net_two_process_harness --role=host --timeout=60`. Record its `PORT`. On machine B run the client role against that host's address using a game/example or a qualification adapter: the existing harness client hardcodes loopback, so changing only its port does not constitute a physical LAN test. Verify discovery and join, reliable identity/bytes, leave/rejoin and host loss on the actual LAN. A remote-address-capable qualification adapter is still required; no such run is claimed here.
2. **WAN relay:** use two machines on different routers and a reachable disposable TLS service. Configure `CNA_GAMER_SERVICES_ENDPOINT=https://<qualified-name>:<port>/cna/v1`, `CNA_GAMER_SERVICES_CA_BUNDLE`, and `CNA_GAME_ID` on both. Use the harness with `host player` on A, `join player` on B. Enter Alice/Charlie passwords on A and Bob/Dana on B. Start B after A emits `session-created`. Repeat with `ranked` and the corresponding provisioned title.
3. At both `session-roster`, send `continue` to both; require `session-exchanged 5` or `6`, with all five reliable payloads verified by the native harness. Then send `continue` to both for `session-playing`, and again for `session-lobby`. For normal PlayerMatch send `continue` to the joiner first, then host; for Ranked host first, then joiner. Require both `session-done` and exit zero. Use stdin pipes/coordinator for timely boundaries (each input gate has 60-second timeout).
4. Repeat the WAN sequence with `invite`, `add`, `migrate`, and `crash` as the third harness argument. For migration, after lobby let host leave first (`continue`) or kill only the test host for crash; then continue the joiner, require `session-migrated`/Alice→Bob and survivor-only roster. For restart, restart only the disposable service at the same endpoint for ~1 second between roster and first continue; require continued packet/game/board flow.
5. Test relay unreachable at creation: require completion with an error within configured deadlines, no playable half-session and no indefinitely retained membership. During play remove connectivity for 5 seconds, restore and require recovery; then exceed the 15-second recovery budget and require bounded termination. Measure actual behavior, allowing scheduler/transport slack rather than treating constants as exact elapsed wall time.
6. For IPv6, bind the disposable service to `::`, use a valid IPv6 DNS name or bracketed literal with matching certificate, and force external IPv6 connectivity through network configuration. Repeat relay/session/recovery. Do not reinterpret IPv4 loopback ENet proxies as external IPv6 failure. NAT64 and IPv6-only deployment remain separate tests.
7. Repeat behind symmetric NAT with no inbound mappings; record router/NAT facts and successful outbound WSS. This can qualify relay connectivity, never hole punching. Record MTU, loss/latency and reconnect/migration outcomes. Do not label symmetric NAT by guessing from a private address alone.

## Voice/cache freshness and storage-platform follow-up

- **Voice policy, DEFERRED MEDIUM:** `VoiceMutes` is seeded from sign-in and updated after this Guide's block action. `VoiceChat` checks a `SignedInGamer` privilege snapshot; `OnlineBackend::renewLocked` updates its private slot without publishing new privileges to the existing gamer. Heartbeats do not fetch blocks/policy. A second-device/operator change has no proven prompt effect on an existing voice session. Server relay membership validation is not game-specific voice authorization. Reproduce with synthetic voice on two clients plus a separate authenticated controller that blocks or an operator setting communication=blocked; keep existing clients signed in. Require capture/send/playback cessation, test friends-only unfriend, record latency and behavior after credential renewal, then restore and verify recovery. Existing same-device Guide mute tests and fresh server policy tests do not prove this. No new wire policy or promise of instant global voice revocation was introduced in Phase 3.
- **Physical audio, NEEDS DEVICE QUALIFICATION:** with the owner choosing disposable capture/output devices and consenting to a short quiet tone, run `tools/platform/run_gpu_tests_private.sh --exec tools/net/voice_physical_check.sh cmake-build-debug /tmp/gs-phase3-physical-voice`. This needs PipeWire tools, microphone/speaker and NumPy. The script records/analyzes the room, deletes recordings and keeps numeric evidence. Do not substitute synthetic capture for a device result.
- **Windows/macOS storage, NEEDS DEVICE QUALIFICATION:** build affected targets, repeat independent process awards/board updates and avatar installation, then force temp-open, flush/close and replacement failures. Verify old data remains, lock failure refuses writes, retry succeeds, and locks release after a killed writer. Win32 locking has source implementation, but replacement of existing JSON via filesystem rename remains unqualified. Existing checked streams do not guarantee fsync/power-loss durability.
- **Corrupt progress / power loss, DEFERRED:** current progress contract intentionally treats unreadable history as empty; a subsequent save may replace it. Profile stores instead preserve unreadable files. Decide the desired contract with the owner before changing behavior. Test crash/real power-loss and filesystem semantics separately from successful atomic rename.
