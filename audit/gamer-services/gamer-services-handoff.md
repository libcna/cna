# GamerServices audit handoff

Task **GS-AUDIT**, 2026-10-01. Audit only; no implementation fixes or redesign. Start with [executive answers](gamer-services-audit.md), [issues](gamer-services-issues.md), [matrix](gamer-services-compatibility-matrix.md), then [architecture](gamer-services-architecture.md).

## Repository snapshots

Working root `/rv/data/development/github.com/libcna/cnawork`, branch `work`, initial HEAD **9976f4909a72a79e8d64ca4b3d15756f4696f5c9**, initially clean.

Sibling `/rv/data/development/github.com/libcna/cna`, branch `next`, same initial HEAD, initially clean. Existing `cmake-build-debug` targets are HEADLESS. Client binaries were reused, not rebuilt. Their hashes are in [client-binaries.sha256](evidence/client-binaries.sha256); matching repository commits alone do not prove binary freshness.

Server `/rv/data/development/github.com/libcna/cna-gamer-services-server`, branch `feature/gamer-services-server`, HEAD **e45049abcbefcc8b31875c24b0f16c55f96def80**, initially clean. Fresh Debug build performed outside its tree.

Exact branches/hashes/status for **every located relevant sibling** are retained in [repositories.txt](evidence/repositories.txt): cna-samples-gamer-services, cna-examples, cna-samples, cna-cs, cna-cs-samples, xna4-decomp, xna4-spec, sharp-runtime and libcna.com. Those inventories are not claims of full audits of those repositories.

FNA `/rv/data/library/github.com/FNA-XNA/FNA`, `master` at **b35512475ed7980169574d2c40927381c1764d5a** was searched for the reference subsystem. Relevant source was not present under src and FNA.NetStub was not found in the library search. Supplemental Windows XNA decompilation was used for signatures/format facts, not to infer functioning console behavior from Windows stubs.

## What was inspected

- **Core detailed paths:** Gamer/SignedInGamer/collections/profile/privileges/leaderboard/dispatcher/Guide/avatar implementations; OnlineBackend/ServiceAsyncResult/ServiceProtocol/ServiceCompletionPump/config/credentials; offline store/profiles; Guide panes; avatar codec/assets/catalog/model/GLB/clips/editor; native Net sessions/local/remote gamers/QoS and online operation/preparation/roster/relay/voice.
- **Server detailed paths:** CMake; Main/Listener/Service/Store/Authentication/Privacy; directory/invitation/party/leaderboard/avatar/asset handlers; relay auth/protocol/flow/hub/listener and event listener; all migration files; Admin CLI; tests and Python paired-client harnesses.
- **Supplemental samples/bindings:** c-api gamer/Guide/avatar/Net handle wrappers and smoke-test inventory; cna-cs representative GamerServices integration test and facade refusal scan; cna-examples avatar/Net sample sources; cna-samples-gamer-services/cna-samples file inventory. No managed facade test execution or binding plan/implementation.
- **Claims:** current known-limitations document, server/build docs and relevant plan/acceptance records; `../libcna.com/features.html` has demonstrably stale claims.
- **Not fully audited:** every overload's exact exception order; all enum numeric values by independent manual comparison; sharp-runtime internals; all StorageContainer methods; every renderer; all sample/game behavior; every populated historical DB migration; full production operations/security. UNKNOWN in the matrix is deliberate.

[Inventory](evidence/inventory.txt) records 1,043 files across the requested categories; [operation cross-reference](evidence/protocol-operations.md) maps all declared control operations and native literal/dynamic call sites. These are inventories, not line-by-line review certificates. The [reference-member screen](evidence/reference-member-screen.md) is lexical; `AchievementCollection.GetEnumerator` is represented by C++ iteration and must not be called a missing feature based solely on that screen.

## Builds and experiments executed

From the cnawork root:

```sh
cmake -S ../cna-gamer-services-server -B /tmp/cna-gs-audit-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cna-gs-audit-build -j4
```

**PASS**, all server targets. [Configure](evidence/server-configure.log), [build](evidence/server-build.log). No fresh native CNA build; reused binaries at the same source-checkout HEAD. No production modifications or added stubs/dependencies.

Native tests used the mandatory private display runner:

```sh
tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/cna-gs-audit-client-state \
  XDG_CONFIG_HOME=/tmp/cna-gs-audit-client-state \
  XDG_CACHE_HOME=/tmp/cna-gs-audit-client-state CNA_GAMER_SERVICES_KEYRING=0 \
  ../cna/cmake-build-debug/CnaGamerServicesTests \
  --gtest_output=xml:/tmp/cna-gs-audit-client.xml

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/cna-gs-audit-net-state \
  XDG_CONFIG_HOME=/tmp/cna-gs-audit-net-state \
  XDG_CACHE_HOME=/tmp/cna-gs-audit-net-state CNA_GAMER_SERVICES_KEYRING=0 \
  ../cna/cmake-build-debug/CnaNetTests \
  --gtest_output=xml:/tmp/cna-gs-audit-net.xml
```

**627/627** GamerServices, **523/523** Net passed. Initial restricted-sandbox attempts stopped with exit 77: private Weston `bind() failed: Operation not permitted`. Permission to run outside that sandbox was granted by automatic review; the same private runner then succeeded. Never substitute the owner's DISPLAY. Summaries and per-suite metadata: [client log](evidence/client-test-summary.log), [Net log](evidence/net-test-summary.log), [results JSON](evidence/client-test-results.json). Full transient outputs remain `/tmp/cna-gs-audit-client.log`, `/tmp/cna-gs-audit-net.log` and their XML siblings.

The full fresh-server suite was executed with real existing native clients:

```sh
export CNA_SERVICE_SESSION_CLIENT_HARNESS=/rv/data/development/github.com/libcna/cna/cmake-build-debug/cna_service_session_client_harness
export CNA_SERVICE_DIRECTORY_CLIENT_HARNESS=/rv/data/development/github.com/libcna/cna/cmake-build-debug/cna_service_directory_client_harness
export CNA_SERVICE_RELAY_CLIENT_HARNESS=/rv/data/development/github.com/libcna/cna/cmake-build-debug/cna_service_relay_client_harness
export CNA_SERVICE_AVATAR_CLIENT_HARNESS=/rv/data/development/github.com/libcna/cna/cmake-build-debug/cna_service_avatar_client_harness
export CNA_AVATAR_CATALOGS=/rv/data/development/github.com/libcna/cnawork/modules/gamer-services/assets/avatars
ctest --test-dir /tmp/cna-gs-audit-build --output-on-failure -j2
ctest --test-dir /tmp/cna-gs-audit-build \
  -R '^(service_cna_session|service_cna_session_migration)$' --output-on-failure -j1
```

Loopback socket/network permission was granted for these existing tests. First run **23 pass, 2 fail, 7 skip of 32**. Retry **ordinary session pass, migration fail**. Logs: [full run](evidence/server-tests.log), [retry](evidence/session-retry.log). The seven skips require Linux unshare/ip/slirp4netns; `slirp4netns` was not found. Full client host-crash migration/add-local-gamer were therefore not reached. Do not count those as pass. Source tests with `SKIP_RETURN_CODE 77` can also skip silently without the harness env above.

Failure reproduction is timing-sensitive: `tests/cna_session_e2e.py:136` requires six deliveries except in restart mode, but the sixth is intentionally unreliable. `tools/net/service_session_client_harness.cpp:210–240` permits five reliable packets. This is a flawed acceptance oracle, not established broken host migration. Do not relax reliable-packet checks. A later run can pass without correcting the false guarantee.

Fresh schema check: sequentially execute `migrations/001…023` with SQLite foreign_keys ON; inspect user_version, `PRAGMA foreign_key_check`, `PRAGMA integrity_check`. Result **23 / empty / ok**. [Exact assembled schema](evidence/schema-check.txt). This check only covers empty-database ordering; service restart/crash tests add behavioral persistence evidence.

The local-store bug probe used real compiled CNA library code and an isolated StorageDevice app root:

```sh
cp audit/gamer-services/evidence/offline-store-probe.cpp /tmp/cna-gs-audit-probe.cpp
python3 audit/gamer-services/evidence/build-offline-store-probe.py
env XDG_DATA_HOME=/tmp/cna-gs-audit-probe-state /tmp/cna-gs-audit-probe
```

The build helper reads existing `compile_commands.json` and link.txt, substitutes only the diagnostic object/main and writes `/tmp` outputs; it requires the exact existing sibling build layout. Initial diagnostic compile used a nonexistent public default PropertyDictionary constructor, was corrected to the actual CreateInternal factory, and then succeeded. This was a diagnostic-only correction, no product fix. Output:

```text
input=9007199254740993 rating=9007199254740992 column=9007199254740992
destination-is-directory threw=0 loaded=0
```

[Probe](evidence/offline-store-probe.cpp), [build helper](evidence/build-offline-store-probe.py), [output](evidence/offline-store-probe.log). No graphical/audio/network calls occur in this probe.

Protocol comparisons run:

```sh
cmp modules/gamer-services/src/Internal/Protocol/CnaService/Protocol.hpp ../cna-gamer-services-server/protocol/include/CnaService/Protocol.hpp
cmp modules/gamer-services/src/Internal/Protocol/CnaService/RelayProtocol.hpp ../cna-gamer-services-server/protocol/include/CnaService/RelayProtocol.hpp
diff -q modules/gamer-services/src/Internal/ServiceProtocol.cpp ../cna-gamer-services-server/src/Protocol.cpp
```

All identical (no output, exit 0). This checks copied definitions, not every serializer branch. Live paired tests supply the separate wire-behavior evidence.

## Highest-priority next actions / where to resume

1. **GS-AUDIT-001/005/006**, `LocalGamerServicesStore.cpp`, `LeaderboardWriter.cpp`: reproduce retained probe, fix failed writes/numeric representation and decide stream/column-only persistence semantics with narrow tests. Do not touch the service schema casually.
2. **GS-AUDIT-007**, server `Service.cpp:83–119,402–418` vs `Privacy.cpp:30–40`: clarify account-picture visibility policy and cover binary + JSON routes. No exploitation is required to validate authorization logic.
3. **GS-AUDIT-009/010**, server `tests/cna_session_e2e.py`: repair the unreliable expectation, rerun reliable exchange/migration; supply slirp4netns and qualified namespace environment for crash/add-local tests. Freshly rebuild the CNA client harnesses before final acceptance.
4. **GS-AUDIT-002**: decide whether trusted-client achievements/boards meet the target game's needs. Do not equate arbitration agreement with server-authoritative gameplay.
5. Update website claims from verified current behavior; then run actual target games/devices/renderers in the private display environment. Avoid expanding into speculative platform or binding work.

Open questions: original Xbox thread/callback/exception timing; public Internet latency and relay recovery under packet loss; physical voice devices/multiple talkers; non-Linux service/client builds; populated migration paths and backups; all long-lived cleanup/quotas; profile picture policy; fake-backend vs service behavioral drift beyond executed scenarios; missing native setters/endpoints intentionally operator-only; actual C ABI and managed facade lifetime/error parity.

## Files changed and commits

Only `audit/gamer-services/` was added: the five requested Markdown documents plus inventory, schema/protocol/reference evidence, test/build logs, binary hashes and the disposable probe/build helper. No source/build/plan/AUDIT.md/NEXT.md capability status was changed: this audit does not certify an implementation task complete. No production stubs, dependencies, intentional logic deviations or fixes were added. Sibling repositories were read-only; temporary build/test/probe data is under `/tmp`.

The audit commit uses task ID **GS-AUDIT** and message `docs(Task GS-AUDIT): audit GamerServices, networking, avatars and server evidence`. The commit containing this handoff is the audit commit; use `git log -1 --format='%H %s' -- audit/gamer-services` to obtain its exact hash without a self-referential amend. No push authorized or performed.
