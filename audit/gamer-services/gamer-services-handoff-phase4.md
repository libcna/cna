# CNA GamerServices Phase 4 handoff — GS-AUDIT-P4

2026-10-02. Continued the completed Phase 3 work; did not restart the broad audit, add game-specific scoring/anti-cheat/TrueSkill, redesign voice/storage, introduce bindings or push. **SOFTWARE AUDIT CLOSED WITH DOCUMENTED LIMITATIONS.** No further software phase is justified by the remaining evidence.

## Repositories, starting and ending commits

| Repository / branch | Phase 3 starting commits | Phase 4 ending production commit |
|---|---|---|
| `/rv/data/development/github.com/libcna/cnawork`, `work` | 3314ffb05, b05851a47, 2f95d29d7, **c889c94fc** | **e3ad2798166ec4bc162883079a0e229e4a88e958** |
| `/rv/data/development/github.com/libcna/cna-gamer-services-server`, `feature/gamer-services-server` | d5eeba01, a5252756, **f627536f** | **9bd5173341e32b40192738409425167b756cf0a8** |
| `/rv/data/development/github.com/libcna/libcna.com`, `develop` | f4b2212d56ba57533564443ed2b34526ad0b5426 | **df85d05d82abf8ffa39c5a02661eb2abb85946e3** |

All three trees started clean. Native ending audit-only commit contains this handoff; obtain its exact hash with `git log -1 --format='%H %s' -- audit/gamer-services/gamer-services-handoff-phase4.md`. This avoids a self-referential hash. The final response gives that hash. [Exact hashes/branches/production file list](evidence/phase4/repositories-and-files.json). Final closure check requires all three trees clean; no binaries/build artifacts were committed.

Native commits:

1. `f9978d8bda9e6af758d4dba2246ba6720a110036 gamer-services(GS-AUDIT-P4): preserve corrupt histories and first offline achievement completions`
2. `e3ad2798166ec4bc162883079a0e229e4a88e958 gamer-services(GS-AUDIT-P4): publish current voice policy and preserve complete block lists`

Server commits:

1. `42ccf4ec1f46a7e891cb5ab8e92e5f72191cc9b2 gamer-services(GS-AUDIT-P4): return fresh communication policy and support full block responses`
2. `9bd5173341e32b40192738409425167b756cf0a8 test(GS-AUDIT-P4): qualify score aggregation and historical ranked result ownership`

Website commit:

1. `df85d05d82abf8ffa39c5a02661eb2abb85946e3 docs(GS-AUDIT-P4): correct verified GamerServices networking and avatar capability claims`

The final native `docs(GS-AUDIT-P4)` commit records the final audit/matrix/dispositions/architecture, progression table, manual plan, evidence and this handoff. Production commits carry their respective focused qualification documents. No stubs, migrations or missing dependencies were added. Existing optional Opus/platform adapters were used. Internal backend event/store-option and additive capability/response fields are CNA internals/extensions, not public XNA signature additions. Canonical/vendored control parser files remain byte-identical. Default request limits/control/relay framing remain unchanged.

## Confirmed defects, reproduction and fixes

| Finding | Reproducer/root cause | Minimal fix | Regression evidence |
|---|---|---|---|
| GS-AUDIT-001 corrupt history overwrite | tolerant empty reads fed a successful subsequent save, erasing malformed existing bytes | keep tolerant reads; reject unreadable/invalid object-array shape for updates before temp creation; preserve bytes | malformed JSON/null/missing/wrong arrays for awards and boards failed before, pass after; old per-record salvage retained |
| GS-AUDIT-021 competing offline duplicates | eight callers observed unearned before lock; serialized generic save still overwrote first ticks | public Award uses insert-once within writer lock; only winner notifies; default internal overwrite mode retained | coordinated duplicate red adapter returned eight successes, now one with first ticks. Adapter retained old logic with new option; not an unchanged baseline binary |
| GS-AUDIT-019 published voice policy stale | heartbeat reply discarded; renewal changed backend slots without publishing to existing SignedInGamer | `policy-refresh` capability; auth.ping returns current privileges/blocks; validated PolicyChanged/Dispatcher applies to same identity; renewal publishes; preserve local mute and prior blocks on failed optional read | real HTTP/controller heartbeat failed before; current revocation/restoration/blocks/same gamer and renewal pass |
| GS-AUDIT-020 complete block list dropped | service permits 1,024 entries, optional sign-in parser default 256 silently failed | explicit response-only array maximum 1,024 for privacy.list/auth.ping; default/request max remains 256 | real HTTP full-block response failed before; now complete; byte/depth/duplicate/object/default guards tested |

[Storage root causes/contract/platform details](gamer-services-storage-phase4.md), [voice propagation/transport details](gamer-services-voice-phase4.md). Raw red evidence: [corrupt + heartbeat](evidence/phase4/native-red.log), [duplicate adapter](evidence/phase4/duplicate-awards-red.log), [full blocks](evidence/phase4/full-blocks-red.log), [server heartbeat fields](evidence/phase4/server-policy-red.log). Red tests were captured before their fix using Phase 3 production behavior with added tests; working trees were restored/fixed, not left rolled back.

## Progression trust classification

[Every-mutation required-column table](gamer-services-progression-phase4.md) covers achievements/completion/timestamps; local gameplay epochs; leaderboard scores/typed columns/latest/best/ranks; ranked report relationships; profile/reputation statistics; cosmetics/presence; offline state. No implemented inspected mutation remains UNKNOWN or UNSAFE CLIENT TRUST.

- Online outcome mutations are **SERVER VALIDATED / CLIENT AUTHORED**: current authentication/title, provisioned IDs/schema/ranges, ownership/local credentials or immutable ranked participant relationship, transaction and replay rules. Public XNA award shape supplies a key, not progress/timestamp; writable signed long score is title-reported. No generic game-truth validator can infer gameplay.
- First online completion ticks, committed update timestamps, ranks and account statistics are **SERVER AUTHORITATIVE relative to accepted claims**, not independently observed gameplay.
- Offline title/OS-owned files are **CLIENT AUTHORITATIVE BY DESIGN**. No competitive tamper resistance is promised.
- Incremental progress/monotonic maxima, direct skill/profile-stat/reward/unlock/entitlement mutations and general cloud progress storage have no supported route. Unsupported calls are refused; extraneous award progress/user/time cannot redirect or change persisted completion. Avatar catalog cosmetics are not an ownership economy.

Final server progress gate: **111 checks**; arbitration: **62 assertions**. Ownership, foreign titles/boards, negative/overflow/NaN/Infinity/type/schema inputs, duplicate award/retrieval, latest rollback, ascending/descending best/ties/concurrency, nonexistent rounds/outsiders/contradictory replay are qualified. Valid int64 extrema/negative scores are legal where schema permits. A ranked participant intentionally reports actual round peers; saved round membership permits legitimate results after disconnect/session deletion (migration 010 explicitly requires this). A nonparticipant still fails. Consensus is majority of reporting machines per row, not Xbox TrueSkill, full-participant quorum or anti-cheat.

**Accepted HIGH trust limitation** for untrusted competitive deployment: plausible false own achievements/scores and colluding reports remain possible. This is an architectural/title decision, not an unresolved generic software defect.

## Voice freshness and transport

Actual HTTP policy publication and genuine ENet/Opus synthetic receive/playback suppression are separately tested; no physical end-to-end voice run is claimed. Current local mute/block maps are checked for subsequent frames. Privileges use current published gamer data on the five-second permission refresh; FriendsOnly reads current backend friendships and denies failed reads. Healthy remote change freshness is next successful normally 30-second heartbeat + Dispatcher.Update + up to five-second permission interval, with scheduler/network slack, **not a hard 35-second SLA**.

**Accepted MEDIUM boundary:** outage/backoff/stopped Update extends last-known policy; older peers lack full refresh; already queued audio is not erased. Cooperative endpoints enforce privacy; opaque relay enforces current membership/credentials with periodic revalidation, not per-frame bilateral voice policy against modified clients. One microphone owner per machine. Capture/device adapters and actual speakers are implemented/platform dependent but require M12–M15. Voice path table explicitly labels TESTED / IMPLEMENTED / PLATFORM DEPENDENT / DEVICE QUALIFICATION REQUIRED.

## Storage durability

**OS-CACHE-DURABLE** ordinary application persistence: writer lock → validate existing target → serialize → sibling temp write/checked userspace flush/close → checked replace → success. Same-directory temporary file; old target never directly truncated. Failure preserves target and cleans owned temp best effort. Constructed interrupted temp is ignored, next locked progress write replaces it. Profile unique temps can remain after process death; no startup promotion. Public duplicate first time and failure rollback behavior tested.

No universal fsync/FlushFileBuffers/directory sync was added; **CRASH-DURABLE/POWER-LOSS-DURABLE are not promised** for offline JSON. Linux local filesystem runtime verified; Windows replace/sharing/native paths/locks and macOS POSIX branches source verified only. Microsoft STL uses replacement-capable MoveFileExW, disproving blanket existing-target refusal but not testing NTFS/ACL/non-ASCII runtime. Profile destructor-close is not separately checked; no close-only failure reproduced. No destructive OS/power-loss simulation. Stream columns/column-only offline durability remain accepted documented limitations. SQLite online FULL transactions are separate and existing atomicity/restart gates pass.

## Final tests and runtime evidence

Observed task clock: 08:10:24–10:47:54 UTC, **2 h 37 m 30 s through final qualification recording**, including the continuation gap; not an active compute-time estimate. Final commit/report follow this record. Executed suite durations are below.

| Suite | Starting baseline | Final passed | Failed | Skipped | Final duration |
|---|---|---|---|---|---|
| GamerServices | 642 | **649** | **0** | **0** | **60.018 s** |
| Net | 523 | **524** | **0** | **0** | **47.059 s** |
| server (40 registered gates) | 33 / 0 / 7 | **33** | **0** | **7** | **204.18 s** |
| offline concurrency/durability, 10 iterations | not baseline suite count | **70** | **0** | **0** | focused repeat, not added to full-suite totals |

[Machine-readable counts](evidence/phase4/final-results.json), [native summaries](evidence/phase4/native-final.log), [full server final output](evidence/phase4/server-final.log), [repeat output](evidence/phase4/storage-repeat.log), [73 privacy/62 arbitration/111 progress checks](evidence/phase4/server-progression.log), [six synthetic voice neighbors](evidence/phase4/voice-focused.log). Early [21-case focus](evidence/phase4/policy-focused.log) preceded final full-block/duplicate/protocol additions; final suites cover all additions.

Linux Debug HEADLESS, private Weston/Xwayland; real HTTP/TLS/WSS/SQLite native client/server paths and optional Opus 1.5.2 synthetic devices. Native test XML/logs remain `/tmp/gs-p4-final-{gamer,net}.{xml,log}`, server `/tmp/gs-p4-final-server.log`; `/tmp` is ephemeral, retained repository summaries/evidence are authoritative counts. Builds completed before final suites (no concurrent relinking). Native affected tests/four harness targets and whole server build succeeded. Protocol parity checker passed all six files. No live-desktop tests ran.

Eight new native GTests (seven GamerServices, one Net):

1. `OfflineDurabilityTest.CorruptProgressIsReadableAsEmptyButNeverOverwrittenByAnUpdate`
2. `OfflineDurabilityTest.InterruptedTemporaryFileIsIgnoredAndNextUpdateReplacesIt` (baseline already passed; constructed failure-state probe)
3. `OfflineConcurrencyTest.CompetingDuplicateAwardsPreserveTheFirstCompletionAndOnlyOneSuccess`
4. `ServiceRequestRetryTest.HeartbeatRefreshesPublishedPolicyWithoutReplacingTheGamer`
5. `ServiceRequestRetryTest.CredentialRenewalPublishesCurrentPrivilegesAndBlocks`
6. `ServiceRequestRetryTest.AFullServiceBlockListIsNotSilentlyDroppedAtSignIn`
7. `ServiceProtocolTest.FullPolicyResponsesRetainRequestSizeDepthAndDuplicateGuards`
8. `OnlineNetworkSessionTest.LiveBlockAndMuteSnapshotsSuppressSubsequentVoiceFrames`

Extended existing server gates `service_privacy`, `service_progress_trust`, `service_arbitration`; no CTest gate renamed/disabled. All prior fixed failure/precision/concurrency/asset/admission regressions rerun. Focused oracle corrections: well-shaped unauthenticated token for UNAUTHENTICATED (missing token correctly INVALID_ARGUMENT); actual roster wire ID rather than Dana/Bob mix-up; teardown sequence consistent with nonmigrating host deletion. These corrections are not new production defects or weakened correctness.

## Remaining HIGH/MEDIUM dispositions, compatibility and gaps

[Final complete finding dispositions and remaining-risk table](gamer-services-issues.md) covers GS-AUDIT-001 through 021. **No unresolved software-only HIGH issue was found in the audited scope.** No unexplained software-only MEDIUM defect remains; accepted HIGH competitive trust and MEDIUM cooperative-policy/OS-cache/offline-column limitations are not hidden behind that statement.

XNA compatibility gaps explicitly retain disposition: browser online; partner/commerce/title-update infrastructure; TrueSkill/Recent age semantics; online guests/pre-join QoS/multiple local talkers; combined Chat flags/separate chat ordering. Avatar public shape/real 71-bone Draw is distinct from custom CNA data/assets and unqualified GPU pixels. Guide is working CNA UI, not Xbox dashboard. The [four-axis matrix](gamer-services-compatibility-matrix.md) separates API, behavior, CNA service and Microsoft/Xbox interoperability; the [main audit](gamer-services-audit.md) gives the 15 final compatibility answers. C ABI/existing facade/exact Xbox exception timing and deployment-scale churn/load/backup matrices are DEFERRED WITH JUSTIFICATION outside this bounded native phase; no binding plan or new broad audit.

[Manual qualification plan](gamer-services-manual-qualification.md) is **NOT EXECUTED**. M01–M17/M19 provide setup, steps, expected results and evidence for Linux/Windows/macOS, real LAN/WAN/NAT/IPv4/IPv6, relay boundary, migration/crash/reconnect/restart, actual microphone/speaker, live third-controller block/mute/privilege/friend changes, restart/storage/locks/platform failures and GPU avatar rendering. M18 is optional deployment capacity acceptance. Direct online peer/STUN/ICE is unsupported; relay is the initial online transport, not a claimed fallback test. No forced OS crash/power cut authorized.

Exact preserved server skips (missing slirp4netns/namespace isolation prerequisites):

- `service_cna_relay_nat`
- `service_cna_owned_enet_nat`
- `service_cna_session_nat`
- `service_cna_invite_nat`
- `service_cna_session_restart_nat`
- `service_cna_session_host_crash`
- `service_cna_session_add_gamer`

No skips disabled, assertions weakened or NAT qualification inferred from loopback. Distinct namespaces plus real isolated routes are required for these gates; physical WAN/symmetric NAT/device/platform work remains separate. No offensive/general security review.

## Exact reproduction commands

Run from native repository root on Linux; use fresh `/tmp` state paths if rerunning. Finish builds before any test launch:

```sh
CCACHE_DIR=/tmp/gs-phase4-ccache cmake --build cmake-build-debug --target \
  CnaGamerServicesTests CnaNetTests cna_service_session_client_harness \
  cna_service_directory_client_harness cna_service_relay_client_harness \
  cna_service_avatar_client_harness -j4
CCACHE_DIR=/tmp/gs-phase4-ccache cmake --build ../cna-gamer-services-server/build -j2
python3 tools/net/check_service_protocol.py ../cna-gamer-services-server

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-p4-final-gamer-state XDG_CONFIG_HOME=/tmp/gs-p4-final-gamer-state \
  XDG_CACHE_HOME=/tmp/gs-p4-final-gamer-state CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaGamerServicesTests --gtest_output=xml:/tmp/gs-p4-final-gamer.xml

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-p4-final-net-state XDG_CONFIG_HOME=/tmp/gs-p4-final-net-state \
  XDG_CACHE_HOME=/tmp/gs-p4-final-net-state CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaNetTests --gtest_output=xml:/tmp/gs-p4-final-net.xml

export CNA_SERVICE_SESSION_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_session_client_harness"
export CNA_SERVICE_DIRECTORY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_directory_client_harness"
export CNA_SERVICE_RELAY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_relay_client_harness"
export CNA_SERVICE_AVATAR_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_avatar_client_harness"
export CNA_AVATAR_CATALOGS="$PWD/modules/gamer-services/assets/avatars"
tools/platform/run_gpu_tests_private.sh --exec \
  ctest --test-dir ../cna-gamer-services-server/build --output-on-failure -j2
```

Focused final-tree reproductions (expected pass):

```sh
tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase4-focus XDG_CONFIG_HOME=/tmp/gs-phase4-focus \
  XDG_CACHE_HOME=/tmp/gs-phase4-focus CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaGamerServicesTests \
  '--gtest_filter=OfflineConcurrencyTest.*:OfflineDurabilityTest.*:ServiceRequestRetryTest.*:ServiceProtocolTest.*:GuidePrivilegeTest.*'

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase4-repeat XDG_CONFIG_HOME=/tmp/gs-phase4-repeat \
  XDG_CACHE_HOME=/tmp/gs-phase4-repeat CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaGamerServicesTests \
  '--gtest_filter=OfflineConcurrencyTest.*:OfflineDurabilityTest.*' --gtest_repeat=10

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase4-voice XDG_CONFIG_HOME=/tmp/gs-phase4-voice \
  XDG_CACHE_HOME=/tmp/gs-phase4-voice CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaNetTests \
  '--gtest_filter=ENetBackendTest.*Voice*:ENetBackendTest.*Mute*:OnlineNetworkSessionTest.*Voice*'

tools/platform/run_gpu_tests_private.sh --exec ctest \
  --test-dir ../cna-gamer-services-server/build --output-on-failure -V \
  -R '^service_(privacy|progress_trust|arbitration)$'
```

Historical red reproduction must use disposable checkouts, not revert finished trees: apply new regression tests to c889c94fc/server f627536f before their production hunks. For duplicate awards expose the new internal bool/option while retaining old overwrite behavior (the retained red log's adapter), then compare insert-once. Corrupt/interrupted probes only construct disposable file states. See named tests/retained logs above; never manufacture NAT environment or mutate the owner's live data.

## Files changed and closure

**native:**

- `audit/gamer-services/gamer-services-storage-phase4.md`
- `audit/gamer-services/gamer-services-voice-phase4.md`
- `modules/gamer-services/include/CNA/Internal/GamerServices/IGamerServicesBackend.hpp`
- `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp`
- `modules/gamer-services/src/Internal/GamerServicesBackend.cpp`
- `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp`
- `modules/gamer-services/src/Internal/Protocol/CnaService/Protocol.hpp`
- `modules/gamer-services/src/Internal/ServiceProtocol.cpp`
- `modules/gamer-services/src/Xna/GamerServicesDispatcher.cpp`
- `modules/gamer-services/src/Xna/SignedInGamer.cpp`
- `modules/gamer-services/tests/Microsoft/Xna/Framework/GamerServices/GamerServicesGamerTests.cpp`
- `modules/gamer-services/tests/Microsoft/Xna/Framework/GamerServices/ServiceProtocolTests.cpp`
- `modules/gamer-services/tests/Microsoft/Xna/Framework/GamerServices/ServiceRequestRetryTests.cpp`
- `modules/net/tests/CNA/Internal/Net/OnlineNetworkSessionTest.cpp`

**server:**

- `README.md`
- `protocol/include/CnaService/Protocol.hpp`
- `src/Protocol.cpp`
- `src/Service.cpp`
- `tests/ArbitrationTests.cpp`
- `tests/PrivacyTests.cpp`
- `tests/ProgressTrustTests.cpp`

**website:**

- `features.html`

Audit-only final files: `gamer-services-{audit,issues,compatibility-matrix,architecture}.md`, `gamer-services-progression-phase4.md`, `gamer-services-manual-qualification.md`, this handoff and explicitly named `evidence/phase4/` text/JSON files. Exact production lists and hashes above are retained; no build/vendor/unrelated files included. Two website cards updated narrowly from verified behavior; HTML parser and whitespace checks passed; not published.

All confirmed scoped software defects have regressions; final neighboring/full suites and storage repeats pass. Every remaining HIGH/MEDIUM finding has an explicit final disposition, with environment-dependent work separated. **SOFTWARE AUDIT CLOSED WITH DOCUMENTED LIMITATIONS**, not optimism or a new Phase 5 plan. Final commit/check leaves native/server/website trees clean; no push.
