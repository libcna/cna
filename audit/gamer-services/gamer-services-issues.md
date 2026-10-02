# GamerServices audit findings

Audit GS-AUDIT, 2026-10-01. Paths are relative to CNA unless prefixed `server/` (`../cna-gamer-services-server`) or another sibling. Findings are ordered by severity, then subsystem. Unsupported product capabilities are explicitly distinguished from defects. No CRITICAL issue was established. Estimates cover a narrowly scoped correction/feature, not full Xbox equivalence.

## Phase 3 dispositions (GS-AUDIT-P3, 2026-10-02)

- **001 concurrency FIXED on tested Linux paths:** four barrier/pipe-coordinated regressions first failed with missing rows, timestamp/column loss and rename failures, then passed with a scoped process/interprocess lock around the entire read-modify-replace. Both threads and separate processes keep all intended keys. Fixed `.tmp` names are safe among cooperating locked writers. No file schema or public XNA API changed. Locks are released on exceptions/process death; stable sibling `.lock` files are never removed by production code. Reads observe atomic replacement without holding writer locks. Successful return follows checked flush/close/rename, not fsync. Power-loss durability, corrupt-read-as-empty and Windows filesystem replacement remain **DEFERRED**.
- **017 FIXED:** `LocalProfiles::StoreLock` silently continued after open/flock failure. A directory at `profiles.json.lock` deterministically demonstrated unlocked profile creation and avatar writes. The shared scoped lock now refuses the operation; best-effort profile creation retains only its existing in-memory fallback and avatar save returns false. Previous persisted contents survive. Evidence: [red](evidence/phase3/offline-before.log), [green](evidence/phase3/offline-after.log), **18/18** nearby tests passed, including Phase 2 failure cases.
- **007 cache FIXED:** three real `OnlineBackend`/loopback-HTTP regressions first failed and now pass. A cache hit performs current token/title/picture authorization through a validated one-byte `assets.read` response. Denials are not cached; bytes stay cached and become readable again if authorized. A second backend/server cannot reuse the cache without its own grant; revoked tokens are invalidated before another access. Existing transport tests pass (**9/9**). Materialized profile/friend properties remain snapshots; previously returned streams and files in the OS user's cache cannot be revoked. No wire schema or new endpoint. Evidence: [red](evidence/phase3/cache-before.log), [green](evidence/phase3/cache-after.log).

## Phase 2 dispositions

These labels apply to the original findings; the historical evidence is retained below. “Confirmed source boundary” does not mean independently reproduced on every platform.

| Finding | Phase 2 disposition | Stronger evidence / remaining scope |
|---|---|---|
| 001 | PARTIALLY FIXED | four real filesystem red→green regressions; concurrent fixed-temp/read-modify-write and power-loss durability unresolved |
| 002 | CONFIRMED | eight concurrent authenticated self-awards need no gameplay proof; extra userId cannot override account; existing score commits remain client assertions |
| 003 | CONFIRMED source boundary | no browser service transport added/tested |
| 004 | CONFIRMED source boundary | no Xbox infrastructure/TrueSkill solver/Recent retention added; console semantics not runtime-certified |
| 005 | FIXED for new writes | five integer/tick paths and extrema reproduced; safe legacy reader; server exact restart tests pass |
| 006 | CONFIRMED source boundary | stream omission/column-only save trigger unchanged; intended offline contract needs decision |
| 007 | FIXED server routes | 42 checks cover both picture routes; public grants preserved; cached-content policy remains unqualified |
| 008 | NEEDS FURTHER QUALIFICATION | address-map growth source evidence retained; no admission pressure reproducer/fix |
| 009 | FIXED test oracle | documented unreliable semantics; fresh ordinary/migration tests pass; no production transport fix indicated |
| 010 | CONFIRMED, narrowed | seven NAT skips remain; separate non-isolated crash/add variants now pass; fresh client binaries remove Phase 1 provenance limitation |
| 011 | CONFIRMED | online guest rejection before wire; LAN QoS measurements + synthetic/real-process voice pass; online candidate QoS/physical voice unknown |
| 012 | NEEDS FURTHER QUALIFICATION | retained ownership vector unchanged; no quantitative account-churn probe |
| 013 | CONFIRMED | exact website source and replacement wording added to main audit; website unchanged |
| 014 | CONFIRMED | fresh codec/animation/paired avatar tests; Xbox format boundary unchanged |
| 015 | CONFIRMED source boundary | supported single-server mutex/SQLite path qualified, not multi-node/general cloud storage |

Additional suspected unsigned asset-offset bypass: **DISPROVED** for the tested boundary values; all refused before storage access. A first test's demand for LIMIT_EXCEEDED specifically was too strict; no production change. Evidence records both error codes. No original Phase 1 defect was disproved; GS-AUDIT-009 correctly identified a test defect rather than a production failure.

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

**Phase 2: CONFIRMED**, with real concurrent `Service::handle` requests and restart survival (`service_numeric_persistence`, 47 checks). The token owner earns all eight provisioned keys with no gameplay proof; an extra supplied userId cannot award Bob. Leaderboard trust remains source/paired commit evidence, not anti-cheat qualification.

**Phase 1: STRONG EVIDENCE; trust-model limitation, not an authentication bypass.**

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

**Phase 2: FIXED for new writes.** A fresh native regression reproduced all five affected data paths (rating, Int64 column, TimeSpan, DateTime, achievement ticks). INT64_MAX loaded as INT64_MIN on this build; rounded DateTime.MaxValue threw. The offline serializer now uses the module's existing integer-preserving nlohmann JSON dependency, retaining the same fields/type tags and JSON number schema. Valid integral legacy scientific/decimal encodings remain readable; out-of-range/fractional/wrong-type integer records are skipped instead of cast unsafely. New tests cover 2^53−1/2^53/2^53+1, INT64_MIN/MAX and DateTime.MaxValue. Fresh complete GamerServices suite: **633/633 passed**. Old rounded values are unrecoverable; old executables still round when reading/re-writing large numbers. No wire/server migration was made or needed. Source scan isolates these floating conversions to offline progress, not the nlohmann/SQLite int64 service path.

**Phase 1: VERIFIED.** `LocalGamerServicesStore.cpp:183`, `:216–241`, `:380–392` convert ticks, Int64 columns and ratings to `double`; decode casts back. Native probe result:

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

## MEDIUM — newly qualified compatibility boundary (not a production fix)

### GS-AUDIT-016 — Combined Chat send flags need XNA semantic qualification

**NEEDS FURTHER QUALIFICATION / STRONG SOURCE EVIDENCE.** `../xna4-spec/Microsoft.Xna.Framework.Net/SendDataOptions.xml` declares a Flags enum and explicitly permits Chat combined with Reliable/InOrder and separately ordered chat traffic. CNA `SendDataOptions.hpp` describes non-flags values 0–4; `NetPacketCodec.cpp:562–575` maps Chat and unknown combinations to ENET_PACKET_FLAG_RELIABLE. The service's channel choice distinguishes None/InOrder from the remaining values, without establishing separate ordered chat traffic. Existing tests establish the individual options, not this reference contract.

Impact: ports combining flag values may get different reliability/channel ordering/resource behavior. Extra reliability alone does not prove a game-breaking failure. No failing public packet reproducer was added, so no production fix is justified yet. **4–16 hours** for focused reference/differential cases; implementation effort uncertain until the intended CNA plaintext/TLS/chat boundary is agreed.

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
