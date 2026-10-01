# GamerServices Phase 2 handoff

Task **GS-AUDIT-P2**, 2026-10-01. Continue here; do not repeat Phase 1 inventory. Primary report: [audit](gamer-services-audit.md), [issues](gamer-services-issues.md), [matrix](gamer-services-compatibility-matrix.md), [qualified flows](gamer-services-architecture.md#phase-2-qualified-paths-and-remaining-breaks).

## Repository snapshots and commits

All paths below are under `/rv/data/development/github.com/libcna` except FNA. Exact sibling hashes/status are in [repositories.txt](evidence/phase2/repositories.txt); its native dirty files are the audit documents being prepared for the final commit, not uncommitted production fixes.

| Repository / branch | Start | End inspected / production |
|---|---|---|
| `cnawork` / `work` | `812db9656d98e34fc9013914d149e390a6059ef3` | `38287637d85ad8bd3ca782eaa289b1f1c6cf2125` plus the final audit-only commit containing this handoff |
| `cna-gamer-services-server` / `feature/gamer-services-server` | `e45049abcbefcc8b31875c24b0f16c55f96def80` | `477fe6926026c0afa067bacbd82724a2b583bee6` |
| `cna` / `next` (initial paired harness source/binaries) | `9976f4909a72a79e8d64ca4b3d15756f4696f5c9` | unchanged |
| `sharp-runtime` / `next` | `db86514c5bb86a5886d8015b8e2916d49be04ae8` | unchanged |
| `xna4-spec` / `develop` | `fedc17aa48eb3ddec884e004c52eedb5f969985c` | unchanged |
| `xna4-decomp` / `develop` | `bb5eb888053aca8009197552fe6c29add2c2ae2e` | unchanged |
| `libcna.com` / `develop` | `f4b2212d56ba57533564443ed2b34526ad0b5426` | unchanged |
| `/rv/data/library/github.com/FNA-XNA/FNA` / `master` | `b35512475ed7980169574d2c40927381c1764d5a` | unchanged; pre-existing untracked `.idea/`, `.junie/` left alone |

Created commits, in logical issue order:

1. Server `f8c1491b9430ef77da9f1c26e70d257411fb1af9`: picture access policy + regression.
2. Server `d8e4fea38d8f0addb20497cb4a3c20568f31addd`: unreliable delivery acceptance oracle.
3. Native `33e08899fbff0ad2cf96b0243a7adbb4b31b66ec`: checked offline writes, rating rollback, four regressions and initial evidence.
4. Native `38287637d85ad8bd3ca782eaa289b1f1c6cf2125`: exact int64/tick JSON, two regressions and full client result.
5. Server `477fe6926026c0afa067bacbd82724a2b583bee6`: numeric/restart/concurrent-award qualification and offset rejection tests.
6. Native final audit-only commit: this file is part of that commit. Obtain its exact ending hash with `git log -1 --format='%H %s' -- audit/gamer-services/gamer-services-handoff-phase2.md` (avoids a self-referential hash). The final user report also names it.

No push, migration, dependency addition, stubs, protocol version changes, broad refactor or unrelated cleanup. Native/server task working trees are intended clean at handoff; build directories are ignored artifacts.

## Confirmed, fixed, disproved, deferred

- **001 PARTIALLY FIXED:** independently reproduced mkdir/temp-open/rename/write failures; unsuccessful award falsely completed and callback ran; leaderboard memory could diverge. Checked writes and rollback fixed. Fixed temp names, concurrent read/modify/write and no fsync remain.
- **005 FIXED for new writes:** reproduced rating/Int64/TimeSpan/DateTime/award-tick loss, INT64_MAX→INT64_MIN and invalid DateTime on this build. Same-schema integer JSON fixes these; valid legacy numbers read, impossible values skipped, old precision unrecoverable.
- **007 FIXED server-side:** profile denial with successful picture retrieval reproduced. Both binary and JSON asset grants now enforce existing profile policy. Public title/catalog assets and at least one viewable owner intentionally authorize shared hashes. Account-switch/cache policy is a separate unresolved boundary.
- **009 FIXED test-only:** InOrder does not guarantee delivery. Native harness already requires five reliable payloads and validates identities/bytes/duplicates. Corrected outer oracle allows optional sixth packet to be missing. No production transport defect was demonstrated.
- **002 CONFIRMED:** token-derived identity prevents awarding another account, but own achievement claims require no gameplay evidence. Real concurrent award test survives restart. Scores remain eligible client reports, not authoritative game simulation.
- **010 narrowed:** seven NAT variants remain skipped, but additional non-isolated crash and add-local-gamer variants pass.
- **Disproved new suspicion:** huge unsigned asset offsets are refused, not accepted after signed wrap. The first diagnostic demanded LIMIT_EXCEEDED for every huge input; actual INVALID_ARGUMENT is also a valid rejection. No production offset fix.
- Other Phase 1 HIGH/MEDIUM findings remain source-qualified or explicitly unverified; disposition table in issues avoids implying independent reproductions of all 15 findings.

## Builds and exact regression results

Stable native build: `cnawork/cmake-build-debug`, Debug/HEADLESS, SDL3 prebuilt dependency, shared `/rv/cnaccache`. Stable server build: sibling `build`, Debug/Ninja. No new build tree in `/tmp`; temporary state/logs are there. Configure/build logs remain `/tmp/gs-phase2-*-build.log` and `/tmp/gs-phase2-configure.log`.

| Run | Result | Retained evidence |
|---|---|---|
| Native before fixes | four initial regressions FAILED; fifth write-failure probe FAILED | `gs-phase2-offline-before.log`, `gs-phase2-full-device-before.log` |
| Native failure fix targeted | 4/4 PASS | `gs-phase2-offline-after.log` |
| Fresh GamerServices full | **633 passed, 0 failed, 0 skipped**, 80 suites | `gs-phase2-client-tests.log`, `native-results.json` |
| Fresh Net full | **523 passed, 0 failed, 0 skipped**, 48 suites | `gs-phase2-net-tests.log`, `native-results.json` |
| Picture before/after | fails first denied policy check → **42 checks PASS**; original privacy **66 PASS** | picture-before/after and privacy logs |
| Server initial final-fix run using existing sibling clients | **26 pass, 0 fail, 7 skip / 33 registered** | `gs-phase2-server-tests.log` |
| Final server with **fresh cnawork clients** and new numeric test | **27 pass, 0 fail, 7 skip / 34 registered**, 123.30 seconds | `gs-phase2-server-final.log` |
| New server numeric/restart/concurrency | **47 checks PASS** | `gs-phase2-numeric-server.log` |
| Existing server unit plus new offset checks | **5302 assertions PASS**, all oversized offsets refused | `gs-phase2-offset-qualified.log` |
| Extra session crash and add, no NAT isolation | PlayerMatch + Ranked both PASS | `gs-phase2-session-crash-loopback.log`, `gs-phase2-session-add-loopback.log` |
| Protocol drift + property harness | all six copies identical; **5068 checks PASS** | `gs-phase2-protocol.log` |

Full native XML remains `/tmp/gs-phase2-client.xml`, `/tmp/gs-phase2-net.xml`; compact totals/per-suite metadata are committed. Binary hashes in [client-binaries.sha256](evidence/phase2/client-binaries.sha256). Initial paired tests and extra crash/add used the unchanged sibling binaries (same source baseline as Phase 1); final registered server suite used freshly rebuilt cnawork harnesses. These provenance differences are deliberate and recorded.

Setup mistakes corrected during new test development: PropertyDictionary factory required `{}`; server test loop copied JSON under `-Werror`; numeric test initially compared opaque user ID to username rather than joining users. None prompted a production change or weakened a product test. Initial restricted build could not write shared ccache; automatically reviewed escalation allowed the prescribed cache/build/test operations. No unresolved approval block.

### Reproduction commands

From cnawork, configure if needed:

```sh
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv cmake -S . -B cmake-build-debug \
  -DCMAKE_BUILD_TYPE=Debug -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=OFF -DCNA_ENABLE_VIDEO=OFF -DCNA_BUILD_C_API=OFF \
  -DCNA_SHARP_RUNTIME_ROOT=/rv/data/development/github.com/libcna/sharp-runtime \
  -DSDL3_DIR=/rv/data/development/github.com/libcna/cna/.sdl-prebuilt-Linux-x86_64-wayland/install/lib/cmake/SDL3 \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv cmake --build cmake-build-debug \
  --target CnaGamerServicesTests CnaNetTests cna_service_session_client_harness \
  cna_service_directory_client_harness cna_service_relay_client_harness \
  cna_service_avatar_client_harness cna_service_protocol_harness -j4
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv cmake --build ../cna-gamer-services-server/build -j2
```

Target only the new offline cases during development:

```sh
tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase2-state XDG_CONFIG_HOME=/tmp/gs-phase2-state \
  XDG_CACHE_HOME=/tmp/gs-phase2-state CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaGamerServicesTests \
  '--gtest_filter=SignedInGamerTest.FailedOffline*:LeaderboardWriterTest.FailedOffline*:LeaderboardWriterTest.Offline*'
```

Omit the filter for the full suite. Substitute `CnaNetTests` and `/tmp/gs-phase2-net-state` for Net. `/dev/full` case is intentionally skipped on platforms without that device. All display-capable native tests use the private runner; do not substitute the live desktop.

```sh
export CNA_SERVICE_SESSION_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_session_client_harness"
export CNA_SERVICE_DIRECTORY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_directory_client_harness"
export CNA_SERVICE_RELAY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_relay_client_harness"
export CNA_SERVICE_AVATAR_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_avatar_client_harness"
export CNA_AVATAR_CATALOGS="$PWD/modules/gamer-services/assets/avatars"
tools/platform/run_gpu_tests_private.sh --exec \
  ctest --test-dir ../cna-gamer-services-server/build --output-on-failure -j2
# Target new service tests:
ctest --test-dir ../cna-gamer-services-server/build \
  -R '^service_(picture_privacy|numeric_persistence|unit)$' --output-on-failure
# Existing skipped feature variants, deliberately WITHOUT claiming NAT:
python3 ../cna-gamer-services-server/tests/cna_session_e2e.py ../cna-gamer-services-server/build --crash
python3 ../cna-gamer-services-server/tests/cna_session_e2e.py ../cna-gamer-services-server/build --add
python3 tools/net/check_service_protocol.py ../cna-gamer-services-server
cmake-build-debug/cna_service_protocol_harness modules/gamer-services/tests/fixtures/service-protocol-v1.json
```

To reproduce original failures, use an isolated worktree at starting native/server commits and apply just the new regression tests before their corresponding production changes. Retained red output records the already executed baseline. Do not revert the current fixed working tree or rerun the old destructive `/dev/full` writer against real user state.

## Skips still remaining

All seven registered cases return 77 because `slirp4netns` was unavailable; CMake deliberately configures them with `--isolated`. `unshare`/`ip` are also required, and the scripts next probe unprivileged namespaces. No production bypass was substituted for NAT isolation.

| Test | Still unverified | Additional evidence now available |
|---|---|---|
| service_cna_relay_nat | raw CNA relay across two NAT namespaces | non-NAT relay PASS |
| service_cna_owned_enet_nat | owned ENet across two NAT namespaces | non-NAT owned ENet PASS |
| service_cna_session_nat | public PlayerMatch/Ranked under NAT | normal session PASS |
| service_cna_invite_nat | invite acceptance/join under NAT | normal invite PASS |
| service_cna_session_restart_nat | recovery under NAT | normal restart PASS |
| service_cna_session_host_crash | abrupt host loss under NAT | explicit `--crash` loopback PASS |
| service_cna_session_add_gamer | admission after join under NAT | explicit `--add` loopback PASS |

Resume by provisioning the existing helper and setting `CNA_SERVICE_SLIRP4NETNS` if outside PATH; then rerun these exact gates. Public Internet routing still needs separate qualification even when namespace tests pass.

## Files/modules inspected and changed

Inspection concentrated on LocalGamerServicesStore, LeaderboardEntry/Writer, SignedInGamer async branch, LocalProfiles, offline Json, online backend/asset cache, ServiceProtocol and protocol drift, server Service/Privacy/Listener/Store/Authentication/Leaderboards/Atomicity/Directory tests; NetworkSession local-participant validation/search, codec send flags, ENet discovery, voice pipeline and tests, Guide state tests, avatar description/animation/renderer tests, website exact claims. Existing inventories were reused. Not every API overload, migration on historical populated DBs or every response-field serializer was independently re-read.

**Native production files:**

- `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp`
- `modules/gamer-services/src/Xna/LeaderboardEntry.cpp`

**Native tests:** `modules/gamer-services/tests/Microsoft/Xna/Framework/GamerServices/GamerServicesGamerTests.cpp` — six added cases, no existing assertions removed.

**Server production files:** `include/CnaService/Service.hpp`, `src/Privacy.cpp`, `src/Service.cpp`, `src/Listener.cpp` — one existing policy reused for both picture routes, HTTP denial mapping.

**Server tests/build:** new `tests/PicturePrivacyTests.cpp`, new `tests/NumericPersistenceTests.cpp`, `tests/ServiceTests.cpp` three huge-offset checks, `tests/cna_session_e2e.py` unreliable acceptance correction, `CMakeLists.txt` two test registrations.

**Audit:** updated main report/matrix/issues/architecture, Phase 1 handoff forwarding link, this Phase 2 handoff, small textual logs/metadata under `evidence/phase2`. No binaries committed. `git diff 812db9656..HEAD --stat` and the equivalent server starting commit provide exact changed-file lists.

## Highest-priority Phase 3 work / uncertainties

1. **HIGH 001 remainder:** deterministic interprocess offline lost-update repro, narrow serialization/locking and atomic replacement on supported platforms. Decide corrupt-read-as-empty vs preserving unreadable history. Windows replacement and fsync/power-loss qualification remain; do not call checked stream writes transactional.
2. **HIGH 002 trust:** establish target game's authoritative progression requirements. Do not implement generic anti-cheat or accept arbitrary XNA behavior guesses. Confirm extreme public-client score commits, beyond the now-proven server persistence/response path.
3. **MEDIUM 007 residual:** define cached-picture/profile-handle behavior under blocked users/account switching. Fresh server privacy is fixed; local hash cache currently has no per-account authorization or revalidation. Need a concrete public API reproducer before a cache policy change.
4. **MEDIUM 008:** bounded rate-map/admission bookkeeping reproduction and cleanup analysis; no new stress test in this phase.
5. **MEDIUM 010/011:** run seven NAT gates, real microphone/playback and Windows/macOS qualification; online guest and pre-join QoS are unsupported boundaries, not tiny defects to casually implement.
6. **MEDIUM 006/016:** resolve offline stream/column-only contract and combined Chat flags/separate ordering using reference-backed tests. Current source suggests gaps, not an authorized redesign.
7. Avatar corrupt asset/resource lifetime fuzzing and real GPU pixels; Guide native-provider-absent/error/cancellation matrix; historical protocol-version pairs and populated migration upgrades. Existing tests cover many pieces, not a complete proof.

Most important unqualified correctness risk is **offline multi-process read/modify/write data loss**. Most important environmental gap is **NAT/Internet and physical-device behavior**. Preserve these distinctions when reporting compatibility.
