# CNA GamerServices / Net / avatar / server audit

Audit ID: **GS-AUDIT**, 2026-10-01. Native CNA `9976f4909a72a79e8d64ca4b3d15756f4696f5c9`; server `e45049abcbefcc8b31875c24b0f16c55f96def80`.

## Executive answers

1. **How much is genuinely implemented?** A substantial CNA replacement exists: account sign-in, profiles, social operations, achievements, boards, directory matchmaking, online session transport, invitations, avatar descriptions/catalogs/editor and standard avatar drawing have executable implementations. **VERIFIED:** 627 GamerServices and 523 Net tests pass; real server/client TLS, directory, relay, invitation, avatar and restart tests pass. These results establish meaningful working paths, not complete Xbox compatibility.
2. **How much is API-only/stubbed?** This is not predominantly a stub subsystem. Clear unsupported surfaces include all three partner-token calls; marketplace has a custom catalog/test-purchase UI but no real commerce; TrueSkill events do not compute skill; online search QoS is unmeasured. Browser online transport is explicitly refused. Do not translate type/test counts into a compatibility percentage. See the per-area matrix.
3. **Usable by real applications?** Native games adapted to CNA can use local profiles, local sessions/SystemLink, custom Guide, CNA online accounts/social services, property-filtered PlayerMatch/Ranked sessions and CNA avatars. Deployment/configuration and provisioned accounts/title definitions are prerequisites. Original console behavior, every overload and every platform have not been independently certified.
4. **Which features need the server?** CNA online identities, remote profiles/pictures, online friends/presence/messages/parties, service achievements/boards, online discovery/membership/invitations, authenticated online relay, and account avatar synchronization. Local avatars and their renderer/animation/editor, local profiles/storage, and local/SystemLink sessions have offline implementations.
5. **Protocol internally consistent?** **STRONG EVIDENCE, yes for tested paths.** Control protocol header and implementation and relay header are byte-identical across the two checkouts. Actual paired-client tests work. Capabilities and strict response schemas constrain compatibility; this is not proof that all historical versions interoperate. Three server operations lack native callers: `gamer.lookup` (client uses `profile.get`), `leaderboards.definition`, `profile.setGameDefaults`.
6. **Persistence functional?** **VERIFIED for tested SQLite service paths:** 23 migrations apply; integrity/foreign-key checks pass; restart and crash atomicity tests pass. Local persistence is distinct and has **verified bugs**: integer precision loss and successful returns after failed writes. It also silently omits stream columns and has an unsafe concurrent-write pattern.
7. **Matchmaking functional?** Property/slot/type-filtered directory search and join are real and tested. **PARTIAL XNA compatibility:** no service-computed TrueSkill matching, no pre-join online QoS, and online guests are rejected.
8. **Profiles/friends/achievements/leaderboards functional?** Yes within CNA service policy. Server tests exercise real SQLite and authorization; TLS tests exercise persistence. Achievements and scores remain client assertions, not independently validated game outcomes. Recent leaderboard keys have no age window. Profile administration is chiefly an operator tool, not an account self-service product.
9. **Avatars genuinely functional?** Yes for CNA's own catalog and encoding. **VERIFIED:** description/catalog/animation/editor tests and real client/server avatar integration. No Microsoft avatar asset or Xbox description interoperability. A 1021-byte buffer and matching enum names do not imply binary compatibility.
10. **AvatarRenderer usable?** **STRONG EVIDENCE:** standard `Draw` really creates buffers/textures and invokes SkinnedEffect; loading/ready/unavailable states and disposal are implemented. Headless tests passed. This audit did not independently render or visually inspect GPU output; platform pixel parity claims remain unverified here.
11. **Guide/system UI functional?** Substantial **CNA custom in-game UI**, not Xbox dashboard or native OS UI. Keyboard/message boxes, sign-in, social panes, parties, invitations, avatar editing and notifications have real handlers and tests. Store/payment/title-update installation do not reproduce Microsoft infrastructure.
12. **Voice functional?** An actual optional Opus capture/encode/session-transport/decode/playback path exists, with mute and privilege rules. **PARTIAL / STRONG EVIDENCE**, not a no-op. One local talker per machine; physical microphone-to-speaker quality and Internet voice were not tested in this audit.
13. **Relay functional?** **VERIFIED on loopback TLS/WSS** with real CNA peers and server grant authorization. It tunnels ENet datagrams through WebSockets/TCP; it is not a UDP relay or peer hole-punching service. NAT namespace tests skipped because `slirp4netns` was unavailable.
14. **Invitations functional?** **VERIFIED:** Guide acceptance, InviteAccepted and JoinInvited real-client test passed. Stored invitations are title/session/account scoped, expire and are consumed. This does not establish Xbox cross-title launch behavior.
15. **Serious security risks?** No confirmed CRITICAL vulnerability. **HIGH integrity limitation:** provisioned achievement keys and eligible score submissions are trusted client data. **MEDIUM privacy gap:** picture download authorization does not apply profile-view/block policy. Authentication itself has TLS verification, random tokens, hashed credentials, scrypt and scoped checks. Distributed admission pressure, token storage fallback and untested deployment behavior remain relevant risks.
16. **Five most serious technical problems?** (a) client-authoritative competitive progress; (b) silent failed offline saves and concurrency/durability weaknesses; (c) offline 64-bit/timestamp corruption; (d) picture access bypasses profile policy for known hashes; (e) online session acceptance tests incorrectly require an unreliable packet and fail before migration is tested. Severity and evidence are in the issue register. For browser or Microsoft-service-dependent games, unsupported infrastructure can be a larger blocker than these defects.
17. **What blocks an existing XNA game?** A C# binary does not run against a C++ API without the existing facade/porting toolchain. Microsoft identity/partner services/commerce/TrueSkill, original avatar bytes/assets, browser online networking, online guests and platform-specific expectations need adaptation or missing functionality. Matching names is insufficient. No blanket claim that all existing XNA games or samples work is justified by this audit.
18. **Work remaining?** Targeted persistence/privacy/test fixes: roughly **2–5 days**, with regression coverage; stronger qualification across Internet/NAT/devices/platforms: **1–2 weeks or more**; browser service/Net support, skill matching, broader guest support and production service features: **2–4 weeks to >1 month each**, high uncertainty. Full Xbox service equivalence is **>1 month and externally constrained**, not an engineering promise or necessarily an authorized product objective.

## Evidence and boundaries

This audit distinguishes **VERIFIED** (executed observation), **STRONG EVIDENCE** (traced source backed by relevant tests), **PROBABLE** (source-inferred risk), and **UNKNOWN** (not established). Status words classify behavior, not project intent. No aggregate subsystem is marked COMPLETE solely from a matching name or passing unit suite.

The working directory is `cnawork`, a separate clean checkout on `work`, not the sibling `cna` directory. `cna` on `next` initially had the identical commit and clean tree; its existing HEADLESS binaries were used for client runtime checks. The server is a real independent C++ repository, not a placeholder. A fresh Debug server build was made in `/tmp/cna-gs-audit-build`. Client binaries were not freshly rebuilt; this provenance limitation applies to their results. Snapshot hashes/branches and inspection levels are in the [handoff](gamer-services-handoff.md).

The configured FNA tree exists at the supplied location, but its tracked source inventory does not contain the GamerServices/Net/Avatar implementations; no local FNA.NetStub source was located in the searched library tree. It cannot establish console semantics here. The local XNA Windows decompilation supplies supplemental API-shape evidence. Its console-only method bodies are often stubs; they cannot prove Xbox behavior. The [reference member screen](evidence/reference-member-screen.md) is lexical and explicitly not a signature/overload certification. Full per-member exception ordering and Xbox thread timing remain UNKNOWN where not specifically tested.

All 20 requested inventory categories are mapped in the [architecture](gamer-services-architecture.md). The [compatibility matrix](gamer-services-compatibility-matrix.md) provides per-area surface, implementation, test and server evidence. The [issue register](gamer-services-issues.md) is severity ordered. Inventory inclusion means discovered, not line-by-line reviewed.

## Runtime results

| Experiment | Result | What this proves / does not prove |
|---|---|---|
| Fresh server Debug configure/build, GCC/CMake dependencies already installed | PASS | Current server sources compile; no server source fixes needed |
| Existing same-checkout-commit HEADLESS `CnaGamerServicesTests`, private runner | 627/627 PASS | API/local/Guide/fake-backend/asset behavioral checks; not a real server for each test |
| Existing HEADLESS `CnaNetTests`, private runner | 523/523 PASS | Packet, roster, session/preparation and transport checks; no physical voice or Internet certification |
| Fresh-server full CTest run, `-j2`, all four native harness paths supplied | **23 PASS, 2 FAIL, 7 SKIP (32 registered)** | Real TLS, WSS and native integrations execute; CTest's displayed 94% includes skips and is not coverage |
| Focused serial retry of session and migration tests | Ordinary session PASS; migration FAIL | Same missing unreliable-packet assertion; migration failure happens before the migration transition |
| SQL migrations 001–023 on empty SQLite | user_version 23, integrity `ok`, foreign-key check empty | Fresh schema validity; not proof of every historical populated upgrade |
| Disposable native offline-store probe | 9007199254740993 reads back as 9007199254740992; directory destination save returns without error and reads empty | Real precision loss and silent failed save, independent of mocks |

The initial private-display attempts returned 77 because sandbox socket creation was denied. Retrying with sandbox permission allowed the prescribed private Weston/Xwayland runner; no tests ran on the owner's live desktop. Server tests bind temporary loopback servers and use temporary databases. No offensive experiments were performed.

Passing server cases include service unit, TLS e2e, benchmark smoke, ranked migration (service model), privacy, database ownership lock, crash atomicity, directory, arbitration, avatars, avatar validation, social, invitations, CNA directory, relay framing, relay authorization/flow, WSS relay, raw CNA relay, owned ENet, CNA invitation, CNA avatar, and CNA session restart. The seven skipped cases cover NAT isolation, crash migration and add-local-gamer in isolated networking. They must not be reported as passes.

## Compatibility analysis

### Identity, async work and lifetime

`Guide` queues authentication on `OnlineBackend`; curl runs on backend work, not directly in the rendering loop. `GamerServicesDispatcher::Update` publishes signed-in slots and raises events. Slot generation counters suppress stale authentication; tokens are scoped to users/titles. `ServiceAsyncResult` carries result/error, signals completion and runs callbacks through Update, and `End` validates operation/owner and claims consumption before pumping. Blocking `End` has a 30-second deadline; HTTP has separate deadlines. A bare wait handle without a pumping dispatcher cannot publish completion. This differs from assuming arbitrary background callbacks.

Source-level defenses against stale work and reentrant End are real and have tests. Cross-thread calls/destruction of Guide/dispatcher/async objects are **not certified thread safe**: globals, `State::target` and `ended_` are not generally synchronized. Signed-out gamers are retained in an unbounded process-global ownership vector to keep borrowed pointers valid (GS-AUDIT-012). Raw-pointer return ownership and C++ value/copy rules matter; these APIs are not drop-in CLR object lifetime semantics.

### Net integration

These systems genuinely integrate. `SignedInGamer.serviceUserId_` is propagated to `LocalNetworkGamer`/roster members. `NetworkGamer.Id` is a session-local byte ordinal, not the account ID; directory user/machine/session IDs are separate opaque IDs. Online preparation sends access tokens for every local participant, validates the server roster and creates an authenticated relay/ENet engine before public completion. Do not collapse these distinct IDs in a future port.

Session state, membership, voice and leaderboard events are connected to `NetworkSession.Update`, StartGame/EndGame and disposal. A session directory row is not itself a working game connection: relay readiness and ENet welcome must succeed. Rollback paths exist for failed/unconsumed preparation. Failure/recovery and server-restart tests provide meaningful evidence. Direct Internet connectivity, maximum-capacity load and crash host migration were not established in this run.

### Avatar compatibility boundary

CNA preserves the XNA 1021-byte `Description` API and the reference `IsValid` rule (nonzero first byte). Rendering validity is stricter: CNA format 1/2, `CNA` magic, field ranges, catalog/item/slot matching, reserved bytes and CRC-32. Consequently **IsValid can be true while CNA cannot render that description**; this is not a successful Xbox avatar import.

Descriptions store body, height/build, RGB colors and versioned item IDs; format 2 adds facial hair/face parameters. Embedded catalogs v1–v3 are generated original assets. The model path reads GLB through cgltf, validates/assembles clothing/face/rig data and resolves exact catalog versions. Catalog packs use hashed files, bounded parsing, staging and install locking; account records carry revisions. Unavailable/newer catalog handling projects or falls back rather than reinterpreting IDs against a different catalog. Unknown formats do not acquire Microsoft semantics.

The skeleton is **71 bones**, not the obsolete website's 19-bone extension rig. `AvatarAnimation` samples real preset clips, interpolates rotations, applies bind translations and advances/clamps/wraps time; undefined presets produce a zero-length bind pose. `AvatarRenderer` owns graphics resources, reports Loading/Ready/Unavailable, uses SkinnedEffect and actual indexed draws, and releases resources on Dispose. Editor changes commit locally or through revision-checked `avatars.set`; service polling detects Changed. This is functional original content, not visual or binary reproduction of Microsoft's catalog. Pixel quality, non-Linux graphics families and custom imported animation compatibility are not certified by the headless run.

### Guide

Most Guide functionality routes to `GuideOverlay`, `GuideUi`, `GuideScreens`, `GuideSocial`, `GuideSignIn`, `GuideParty`, `GuideLeaderboards`, `GuideAvatarEditor`, style/input/sound helpers. It is rendered inside the game's graphics/runtime layer. Pending actions, visibility, input capture, cancellation and callback reentry have tests. Message/keyboard operations are actual pending UI operations; do not mistake internal completed offline results for all Guide calls being synchronous.

Marketplace is a custom content pane/test-purchase flow, not a payment service. `ShowGameInvite(string sessionId)` intentionally throws for a Windows Phone-only overload; fixed-size/read-only collection refusals and Ranked join-in-progress refusal are also deliberate API constraints, not implementation stubs. Guide sounds have code but were not audibly evaluated.

## Persistence and server integrity

SQLite is real storage with prepared statements, foreign keys, WAL, a 5-second busy timeout, schema-version refusal for newer databases, per-migration transactions and nested savepoints. The request wrapper commits mutations and replay outcomes together; secrets are deliberately excluded from persisted successful response bodies. Application refusals can intentionally commit effects (refresh replay revocation), whereas internal failures roll back. Crash atomicity tests exercise this distinction.

Persistent data and volatile leases/hubs are listed in the architecture. Presence and directory rows surviving restart do not mean live sockets survive; clients must reconnect before lease expiry. Accounts/avatars/catalogs/achievement definitions/boards require provisioning. No general cloud-save API or cloud `StorageContainer` backend is present in the service operation set. Local storage remains a filesystem product.

Fresh migration order and schema integrity passed. Service tests cover several upgrade/restart scenarios, but there is no exhaustive populated-database upgrade matrix in this audit. Indexes exist for title session search, invitation inbox/quota, messages, blocks, catalog hashes and arbitration. Full query-plan/load analysis, backup/restore procedures, disk-full durability and all account-deletion/orphan scenarios remain UNKNOWN. The single-service mutex/SQLite writer is a real scalability boundary; a benchmark smoke pass is not a production capacity result.

## Security findings and positive controls

Authentication does **not** simply trust an arbitrary user ID: access tokens are 32 random bytes represented as hex, stored by hash and checked against title/expiry; password verification uses OpenSSL scrypt and constant-time comparison. Refresh families rotate and replay revokes them. TLS certificates/hosts are verified; redirects and ambient proxies are disabled. Deliberate insecure mode is loopback-only. Relay tickets are short-lived and one-use, derived from membership/credential families; frame source identity comes from server authority. SQL uses bindings and trusted fixed fragments. JSON/body/depth/header/relay queue bounds and per-address/login/account limits exist.

These controls do not make game progress trustworthy: achievement awards accept the authenticated client's requested configured key; score arbitration validates membership/schema/agreement rather than observing gameplay. Picture routes authorize any stored account picture by hash without `mayView`, even when profile lookup would refuse. No privilege escalation or arbitrary-code execution was demonstrated. The listener's address-rate map also has no hard cardinality cap and TLS handshake throughput has no global rate cap; distributed traffic needs separate qualification/protection. Further details and impact limits are in the issues file.

## Documentation conflicts

| Documentation claim | Implementation reality | Evidence |
|---|---|---|
| `../libcna.com/features.html:181`: no online service / no HTTP | Real curl TLS client, Beast server, SQLite and passing paired tests | `GamerServicesBackend.cpp:621`, server `Listener.cpp`, full server test log |
| Same page `:182`: fifteen Guide no-ops; standard Draw does nothing; working renderer only DrawRealEXT, 19 bones | Custom Guide handlers; standard Draw invokes real renderer; 71-bone catalog | `Guide.cpp:960–1020`, `AvatarRenderer.cpp:315–451`, `AvatarAssets.hpp:30` |
| Same page `:179`: online sessions unsupported, voice a no-op | Online session/relay tests pass; optional Opus implementation exists | Net session harness, `VoiceChat.cpp`, module CMake |
| `LocalGamerServicesStore.hpp:27`: ticks exact round-trip | Ticks converted through double, losing low bits above 2^53 | Store encoder/decoder; precision probe |
| `LocalGamerServicesStore.cpp:69`: crash/power loss can never leave half-written file | No fsync, stream errors unchecked, same `.tmp`, truncating fallback | GS-AUDIT-001 |
| Known-limitations footer says only browser/Microsoft-service dependencies could block faithful porting | Too broad as a qualification statement: known online guest/QoS/voice constraints plus discovered save bugs can affect titles | Matrix/issues; historical sample runs were not rerun here |

Historical plans explicitly labeled historical were not counted as false current claims. Repository documentation contains useful accurate limitations (SQLite/single process, no Microsoft services, format boundaries), but was treated as a hypothesis list, not proof. No website/docs outside this audit directory were modified.

## Recommended order

1. Repair silent local-save failures, numeric encoding and regression gaps without changing server protocol.
2. Decide and enforce picture access policy consistently in binary and JSON routes.
3. Correct the unreliable-packet integration gate; rerun migration and isolated cases with prerequisites.
4. Establish the trust policy for competitive progress before calling it secure ranked service support.
5. Qualify real devices, public Internet/NAT, additional platforms and actual target games; update stale website claims.

These are audit recommendations and effort estimates, not a new implementation plan or authorization for a redesign.
