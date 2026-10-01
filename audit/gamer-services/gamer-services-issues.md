# GamerServices audit findings

Audit GS-AUDIT, 2026-10-01. Paths are relative to CNA unless prefixed `server/` (`../cna-gamer-services-server`) or another sibling. Findings are ordered by severity, then subsystem. Unsupported product capabilities are explicitly distinguished from defects. No CRITICAL issue was established. Estimates cover a narrowly scoped correction/feature, not full Xbox equivalence.

## HIGH — persistence

### GS-AUDIT-001 — Offline saves can return success without saving; durability claim is false

**Phase 2: PARTIALLY FIXED (GS-AUDIT-001).** Four deterministic native regressions reproduced directory creation, temp open, rename and actual write/flush failures. Failed offline BeginAwardAchievement previously invoked its success callback; failed rating changes retained the new value. Checked stream/rename failures now propagate, temp files are cleaned after failed writes, the unsafe direct-truncate fallback is removed, and the entry restores its previous rating. All four regression tests pass. Atomic replacement on Windows, concurrent writers and power-loss durability remain unqualified/unfixed; this is not a full durability fix. See `evidence/phase2/gs-phase2-offline-before.log` and `gs-phase2-offline-after.log`.

**Phase 1: VERIFIED** for silent failure; **STRONG EVIDENCE** for concurrency/durability risk.

- Path: `SignedInGamer::BeginAwardAchievement` / offline `LeaderboardWriter` rating hook → `LocalGamerServicesStore` → `WriteJsonFile`.
- Evidence: `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:66–92` ignores directory creation and stream failure, writes a fixed `<target>.tmp`, then falls back to truncating the destination if rename fails. There is no fsync or read-modify-write lock. Missing/corrupt reads silently return empty (`:46–63`). `SaveLeaderboardEntryEXT` reads and replaces the whole board (`:346–403`).
- Reproduction: retained [offline store probe](evidence/offline-store-probe.cpp) creates a directory at the intended JSON filename and calls the real native save. Result: `destination-is-directory threw=0 loaded=0`. No mock backend is involved.
- Impact: games can report earned/saved progress which is absent on reload. Two processes can overwrite each other's updates or collide on the same temp name. Power-loss durability is not guaranteed; source comment claims otherwise. Does **not** describe the separately implemented SQLite service.
- Existing tests: ordinary round trips and all 627 GamerServices tests passed despite this defect. Disk-full/failed-write/concurrent-process coverage does not establish this path's safety.
- Next action: surface write errors and preserve previous contents; test failed persistence and concurrent writers. **2–5 days**, medium uncertainty including cross-platform atomic replacement.

## HIGH — progress integrity

### GS-AUDIT-002 — Achievement and score authority remains with the client

**STRONG EVIDENCE; trust-model limitation, not an authentication bypass.**

- `server/src/Service.cpp:505–512` accepts `achievements.award(key)` from a valid account/title token, checks only that the key is provisioned, then inserts `earned`.
- `server/src/Leaderboards.cpp` validates schema, int64 ratings, eligible users/game epochs and arbitration reports; it does not observe gameplay or validate that a claimed score was earned. Ranked agreement is stronger than a single arbitrary cross-user write but colluding participants still supply the truth.
- Impact: a modified authenticated client can claim its own configured achievements or submit eligible fabricated scores. Membership and title authorization do prevent this from being an arbitrary unauthenticated write.
- Tests prove authorization/transaction/arbitration policy, **not anti-cheat**. No offensive test was attempted.
- Next action: explicitly decide title trust requirements. Strong server-authoritative outcome validation is title-specific, **>1 month**, high uncertainty. Merely documenting trusted-client policy is **<4 hours**.

## HIGH — compatibility gaps (not newly introduced regressions)

### GS-AUDIT-003 — Browser online networking/service transport is absent

**VERIFIED source boundary.** `modules/gamer-services/CMakeLists.txt` excludes the TLS transport for Emscripten; `ServiceConfiguration.cpp:58–69` refuses configured service endpoints without transport. `GamerServicesBackend.cpp:39–50` reports unavailable browser transport. Native ENet/session behavior cannot be inferred to work in browser builds.

Impact: browser targets cannot use this online subsystem. Tests of explicit refusal can pass while the feature is unusable. Native service tests do not cover browser transport. **2–4 weeks or >1 month**, high uncertainty across service, relay and browser lifecycle; this audit proposes no redesign.

### GS-AUDIT-004 — Microsoft-dependent game paths remain unsupported

**STRONG EVIDENCE; intended CNA product boundary.** `Gamer.cpp:177–192` throws `NotSupportedException` for all partner-token variants. `Guide.cpp:972–983` opens CNA content/test-purchase screens, with no payment or Microsoft entitlement path. `NetworkSession.cpp:917–962` raises TrueSkill events but does not implement a rating solver; directory find is property/slot filtering (`server/src/SessionDirectory.cpp:125–133`). `LeaderboardKey` Recent names are ordinary keys; `Leaderboards.cpp:82` ranks all stored rows without age filtering.

Impact: similarly named APIs do not provide Xbox identity/partner tokens/store, skill matchmaking or expiring Recent boards. Windows Phone-only invite overload refusal is **not** counted as a defect. Generic native game ports may work while titles built around these services do not.

Effort: Recent retention policy **2–5 days**; skill-based service **2–4 weeks**; partner/commerce/Microsoft interoperability **>1 month / externally constrained**, high uncertainty. Do not implement these merely to remove an audit label.

## MEDIUM — persistence / data representation

### GS-AUDIT-005 — Offline int64 and timestamp round trips lose precision

**VERIFIED.** `LocalGamerServicesStore.cpp:183`, `:216–241`, `:380–392` convert ticks, Int64 columns and ratings to `double`; decode casts back. Native probe result:

```text
input=9007199254740993 rating=9007199254740992 column=9007199254740992
```

Integers above 2^53 can change; contemporary DateTime ticks are well above that. Near int64 extremes, rounded floating values can also fall outside the representable conversion range. Header promises of exact ticks are incorrect. Server nlohmann/SQLite int64 paths are separate and not implicated. **4–16 hours** for encoding/compatibility decision and boundary regression tests; existing local files require deliberate handling.

### GS-AUDIT-006 — Offline leaderboard column persistence silently drops streams

**STRONG EVIDENCE.** `LocalGamerServicesStore.cpp:193–197,252–256` explicitly skips Stream/unsupported values. `LeaderboardWriter.cpp:83` enables stream creation only for service entries; a normal offline writer's missing stream key does not gain equivalent behavior (`PropertyDictionary.cpp:88–104`). Rating changes trigger the offline save hook; column-only mutation is not independently wired to persist (`LeaderboardWriter.cpp:64–76`).

Impact: offline columns do not have the service's stream/content persistence semantics, and callers may assume a column edit is durable when it is not. Tests treating this as an intentional omission do not establish full XNA compatibility. **4–16 hours to 2–5 days**, depending on the supported offline contract. No new implementation was made.

## MEDIUM — server privacy / resource handling

### GS-AUDIT-007 — Known-hash account pictures bypass profile-view and block rules

**Phase 2: FIXED.** Server regression reproduced denial of profile.get while both picture routes returned bytes. Commit `f8c1491` applies existing mayView to picture-only asset ownership on both routes, maps HTTP denial to 403, and preserves explicit public title/catalog grants. New service_picture_privacy passes 42 checks; existing privacy test passes 66. Authenticated/anonymous/self/friend/pending/blocked/multi-owner cases are covered, with real SQLite and request serialization. Cached bytes cannot be revoked.

**Phase 1: STRONG EVIDENCE; source-only, no exploitation performed.** `server/src/Privacy.cpp:30–40` and `Service.cpp:307–312` gate profile reads with `mayView(viewer,target)`. Binary `Service::file` (`Service.cpp:97–99`) and JSON `assets.read` (`:408–411`) instead authorize a picture if **any** user's `picture` equals the hash. Neither invokes `mayView` for that account. Native `GamerProfile::GetGamerPicture` eventually uses the asset path.

Impact: an authenticated account knowing a picture hash can retrieve it even when fresh profile lookup would be blocked; this includes parental profile-view restrictions. Hash guessing is not assumed, and already cached/downloaded pictures cannot be retroactively erased. This is a narrower privacy gap than arbitrary profile/account disclosure. Images also shared as public title/catalog assets complicate policy.

Next action: define whether account pictures are intentionally public; if not, enforce consistent authorization on both routes and test it. **4–16 hours**, with possible policy ambiguity. Existing service_privacy tests passing does not prove all data routes obey the policy.

### GS-AUDIT-008 — Listener's per-address rate map is not strictly bounded

**STRONG EVIDENCE.** `server/src/Listener.cpp:56–78`: once `rates_.size()>=4096`, expired entries are removed, but `rates_[peer]` still inserts a new entry with no hard cap, including before global-connection refusal. Only entries older than one minute are removable. There is also no global handshake rate cap; 256 concurrent pre-auth connections, 32 per address and 600 new connections/minute/address are separate controls.

Impact: many distinct connecting sources can grow address bookkeeping within the window and consume handshake capacity. Real-world feasibility/impact was not stress-tested. This is not “no rate limiting”; significant limits exist. **4–16 hours** for bounded bookkeeping/qualification; deployment-wide flood protection is separate.

## MEDIUM — test validity / qualification

### GS-AUDIT-009 — Session acceptance gate requires delivery of an unreliable packet

**Phase 2: FIXED (test oracle), commit `d8e4fea`.** Local XNA reference `../xna4-spec/Microsoft.Xna.Framework.Net/SendDataOptions.xml` explicitly permits loss with InOrder. Native packet mapping uses ENet flags 0 (unreliable sequenced). The native harness still requires all five reliable payloads with identities/bytes/duplicates checked. Python now accepts five or six in every mode. Rebuilt server suite: **26 passed, 0 failed, 7 skipped**; ordinary and migration acceptance both pass. No production delivery fix was indicated.

**Phase 1: VERIFIED.** Fresh server CTest failed `service_cna_session` and `service_cna_session_migration` with `('session-exchanged 5', 'session-exchanged 6')`. Serial rerun passed the ordinary case and failed migration again at the same assertion.

`tools/net/service_session_client_harness.cpp:210–240` sends five reliable payloads and one `SendDataOptions::InOrder` payload (ordered but unreliable); its own completion condition explicitly permits five reliable deliveries after a grace period. `server/tests/cna_session_e2e.py:133–136` only permits five in restart mode. An unreliable send has no unconditional delivery guarantee in ordinary mode either.

Impact: the gate is timing-sensitive and prevents later migration assertions from running. Do **not** infer that a host-migration bug was observed or that a reliable packet was lost. The PlayerMatch half of the first migration run completed HostChanged before the Ranked pre-migration exchange failed; the full test did not pass. **<4 hours** to align the oracle with transport semantics and rerun; additional work only if reliable loss then appears.

### GS-AUDIT-010 — Important end-to-end qualification is conditional or absent

**VERIFIED gaps.** Seven of 32 server tests skipped without `slirp4netns`: raw/owned relay NAT, session NAT, invite NAT, restart NAT, crash host migration, add-local-gamer. CMake assigns `SKIP_RETURN_CODE 77` to these tests. Native integration tests also skip unless four harness environment variables are supplied; this audit supplied them.

Guide/social/avatar service unit tests commonly select `makeFakeBackend`, while real service tests use SQLite and separate TLS/CNA clients. Both forms are useful, but fake tests do not prove wire behavior. Headless avatar draw tests do not prove pixels. Physical audio, Internet routing, real devices, Windows/macOS/mobile and all historical version combinations were not executed here.

Impact: “tests pass” without prerequisites is misleading. **1–2 weeks or more** for a scoped hardware/Internet/platform qualification campaign, high uncertainty. No blanket feature-broken claim follows from missing qualification.

## MEDIUM — Net / voice compatibility

### GS-AUDIT-011 — Online guests, pre-join QoS and multiple local talkers are partial

**STRONG EVIDENCE.** `NetworkSession.cpp:1375–1387` filters/rejects guests on the authenticated service path, despite local Guide guest support. `:1512` constructs service search results with unmeasured QoS; `QualityOfService.cpp` initializes unavailable/zero measurements. `VoiceChat.cpp` selects one local talker per machine, using optional Opus and a recording provider; absent dependencies/devices mean no voice.

Impact: split-screen guest titles, games selecting lobbies by measured QoS and per-controller voice expectations can require adaptation. Four separately authenticated local accounts are a different feature and have tests. **Online guests 1–2 weeks; pre-join QoS 2–5 days; multi-device voice 1–2 weeks or more**, high uncertainty for device/platform behavior.

## LOW — lifetime / documentation

### GS-AUDIT-012 — Signed-out gamer ownership accumulates for process lifetime

**STRONG EVIDENCE.** `GamerServicesDispatcher.cpp:24` owns a global vector; sign-in appends at `:150`, sign-out marks objects disposed and removes slots (`:113–117`) without reclaiming vector entries. This avoids dangling borrowed references, but repeated account churn retains objects and associated state until process exit. No bounded reclamation proof was found.

Impact: long-lived kiosk/test processes can accumulate memory. Ordinary short game sessions are unlikely to suffer major impact. A naive delete would create worse lifetime bugs. **2–5 days**, high uncertainty for ownership compatibility. Arbitrary concurrent dispatcher/result destruction also remains unqualified, not a confirmed race reproduction.

### GS-AUDIT-013 — Website capability claims are obsolete

**VERIFIED source contradiction.** `../libcna.com/features.html:179–182` claims online sessions unsupported, voice no-op, no HTTP service, numerous Guide no-ops and standard avatar Draw empty with only a 19-bone EXT renderer. Current native source and fresh service tests contradict these claims; current skeleton is 71 bones.

Impact: wrong product/porting decisions. **<4 hours** to update claims from qualified evidence; no website changes were made by this audit.

## INFO — architectural constraints / deliberate semantics

### GS-AUDIT-014 — Avatar API compatibility does not include Xbox asset/byte interoperability

**VERIFIED format boundary.** `AvatarDescription.cpp:242–274` retains `IsValid`'s nonzero-byte rule, but `AvatarDescriptionCodec.cpp:96+` decodes only CNA versions/magic/CRC. `server/src/Avatars.cpp:49+` validates that same original format and catalog IDs. A foreign nonzero 1021-byte buffer can be IsValid but yield height 0 and renderer Unavailable. Assets, animation motions and art are original CNA data, not Microsoft data. This is not fake rendering. Interoperability effort **>1 month / external asset and specification constraints**; exact Xbox visuals are not assumed required.

### GS-AUDIT-015 — Single-process service, no general cloud save, limited account operations

**STRONG EVIDENCE.** `server/src/Main.cpp:23–28` enforces one server owner per database. Relay/event hubs, budgets and live connections are memory state. SQLite is the only store. `Service.cpp:151` has no generic cloud file/container upload/download operations or account registration/password reset API; operator `Admin.cpp` provisions users/titles/assets and policies. `modules/storage/src` implements local storage, not server sync.

These are deployment/product boundaries, not proof that existing persistence is fake. Account self-service **1–2 weeks**; general cloud saves **2–4 weeks**; multi-node service **>1 month**, all high uncertainty and outside this audit's implementation scope.
