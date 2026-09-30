# GamerServices / Net / Avatar final hardening (living plan)

Task ids `GSH-*`. This pass follows an independent audit of CNA `ec30ca990` and
cna-gamer-services-server `a85a146`; both HEADs were verified at the start (2026-09-30). It fixes
what the audit found, adds adversarial evidence and makes the documents match the code. It adds
no architecture. The canonical list of what remains is
[`docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md); this
file records the findings, decisions and evidence of the pass.

Branches: CNA `feature/gamer-services-server` (worktree `cnawork/cna-gamer-services`), server
`feature/gamer-services-server`. Nothing is pushed or merged into `next`. Evidence is kept in
`/rv/tmp/gs-final-hardening/`.

## Classification

| Mark | Meaning |
|---|---|
| DEFECT | Confirmed defect, reproduced in the code |
| COMPAT | Compatibility gap with XNA-observable behaviour |
| HARDEN | Security or hardening gap |
| PLATFORM | Platform gap |
| VALID | Validation gap: implemented, not measured the way named |
| DOC | Documentation gap: the words say more (or less) than the code |
| NONGOAL | Intentional non-goal, kept |
| INFER | Audit inference that needed reproduction before being called a defect |

## Findings and status

Commits: CNA `63b3d2a9a`..; server `f5ce769`.. (each row names its own).

| Id | Finding | Class | Outcome |
|---|---|---|---|
| GSH-01 | A recorded request ID committed in its own transaction before the mutation ran; a crash in between left the ID spent and the change missing, so the retry was refused as a duplicate | DEFECT | **Fixed**: server `f5ce769`, CNA `63b3d2a9a` |
| GSH-02 | A SystemLink guest read `IsGuest` false on every other machine | COMPAT | **Fixed**: CNA `07fe3074c` |
| GSH-03 | `GamerPrivileges` other than online sessions and purchase were always Everyone/true; no block list | COMPAT | **Implemented** as CNA account policy: server `1e4cca6`, CNA `148d9db35` |
| GSH-04 | `FriendGamer.HasVoice` was always false | COMPAT | **Implemented**, CNA definition: server `bdc0d7b`, CNA `422857620` |
| GSH-05 | Online search results never report QoS | COMPAT | **Kept unavailable, justified** (below); limitation 17 |
| GSH-06 | No single-owner database lock on Windows or macOS | PLATFORM + DEFECT | **Fixed**: server `d1d130c` (Linux; Windows code under Wine) |
| GSH-07 | Refresh credentials plaintext on POSIX, absent on Windows | HARDEN + PLATFORM | **Improved**: CNA `835810e5a` (Secret Service on Linux, DPAPI on Windows; macOS keeps 0600 files) |
| GSH-08 | Host migration + Ranked results, restart, reconnect untested | INFER -> DEFECT | **Reproduced and fixed**: a report made after the session moved on was lost; server `824decd` |
| GSH-09 | Avatar catalog install under concurrency, restarts, unknown formats | INFER -> DEFECT | **Reproduced and fixed**: concurrent installers of one pack failed each other; CNA `62f88b4e9` |
| GSH-10 | README throughput without raw evidence | VALID | **Done**: server `50f3862` (`benchmarks/`) |
| GSH-11 | README wording on handshake and rate limits | DOC | **Done**: server `e45049a` |
| GSH-12 | README claims more Xbox fidelity than verified | DOC | **Done**: CNA `aff1ff7ba` |
| GSH-13 | History read as current | DOC | **Done**: CNA `4788ff066` |
| GSH-14 | Windows and macOS unvalidated | VALID | **Partly**: lock and DPAPI store under Wine; no Windows (VM gone) or macOS host: UNVERIFIED |
| GSH-15 | `AvatarRenderer` measured on two renderers only | VALID | **Done** on five renderers (below) |
| GSH-16 | Voice and Guide sounds never on real devices | VALID | **Partly**: real microphone and output device exercised; the air loop not (speaker muted by its owner): CNA `35f4e379b` |
| GSH-17 | No public-Internet qualification | VALID | **Pending**: no remote network available; not simulated |
| GSH-18 | Xbox LIVE wire, Microsoft accounts and assets, PartnerToken, Marketplace, TrueSkill, the Recent window | NONGOAL | Kept |
| GSH-19 | SQLite and no clustering | NONGOAL | Kept; the benchmark shows no database bottleneck at this scale (GSH-10) |
| GSH-20 | Full evidence corpus | VALID | **Run** (below) |
| GSH-21 | Sample acceptance | VALID | **Run**: all five pass (below) |
| GSH-22 | Canonical limitations list | DOC | **Done**: every entry categorized |

## GSH-01 Request-ID atomicity

**Found.** `Service::dispatch` inserted the request ID and committed it (`BEGIN IMMEDIATE ...
COMMIT`, "the request's own bookkeeping"). Only then did the handler run: autocommitting
statements or a second transaction of its own. A crash between the two left the ID recorded and
the change absent, and the retry got `DUPLICATE_REQUEST` forever. Refusals after validation
already spent their IDs, by design.

**Design.**
- `Store::Transaction` nests. The outermost level is `BEGIN IMMEDIATE`; an inner one is a
  `SAVEPOINT`, so each helper keeps its own all-or-nothing rule. The five local transaction
  classes and the raw `BEGIN`/`COMMIT` sites became this one type.
- A recorded request is one transaction: the ID row, the handler's changes and the outcome, all
  committed together.
- A refusal (an `Error` other than `INTERNAL_ERROR`) is the request's outcome and commits with
  whatever the request did before it. That preserves the revocation a replayed refresh credential
  performs before it refuses.
- `INTERNAL_ERROR`, any other exception or a crash rolls everything back, the ID included.
- Schema 21 adds `user_id`, `op`, `outcome` and `result` to `request_ids`.
- A repeat of an ID by the same caller for the same operation is answered from the record: the
  same result, or the same error code. The caller is the authenticated account, or nobody for a
  sign-in or refresh.
- Anything else still returns `DUPLICATE_REQUEST`. That covers another account, another
  operation, a pre-schema-21 row, and a result over 16 KiB.
- Results carrying a secret are never stored: `auth.login` and `auth.refresh` tokens and
  `sessions.relayTicket` tickets. Those repeat as `DUPLICATE_REQUEST`, as before.
- The service advertises capability `request-outcomes`.
- Sign-in's scrypt still runs without the service lock. It now runs *before* the transaction,
  because one SQLite connection serves every request and no transaction may stay open while the
  lock is released.
- `PRAGMA synchronous` is settled before `BEGIN`, since SQLite refuses to change it inside a
  transaction.

**Which operations need what.**
- Exactly once:
  - `messages.send`, `invites.send`, `sessions.create`/`join`/`joinInvited`/`addMembers`,
    `parties.*`, `avatars.set`;
  - `leaderboards.game.begin`/`commit` (commit is also guarded by the round digest);
  - `auth.refresh` (rotation).
- Safely idempotent already:
  - `friends.*`, `achievements.award` (but its `awarded` flag is only right the first time, so it
    too needs the stored result), `reviews.submit`, `profile.set*`;
  - `messages.read`, `sessions.leave`/`remove`/`update`, `auth.logout`.
- The atomic record serves both. Nothing is serialized beyond the service lock that already
  existed.

**Client.** libcurl already re-sent a request once, with the same body and so the same ID, when a
reused keep-alive connection died before any answer. Against the old server that repeat could
only hear `DUPLICATE_REQUEST` if the first attempt had committed. `exchange()` now also asks once
more when the connection is lost after sending (`CURLE_GOT_NOTHING`, `SEND_ERROR`, `RECV_ERROR`).
It does so only against a `request-outcomes` service and never for the three secret-bearing
operations.

**Evidence.**
- Server `tests/AtomicityTests.cpp` (ctest `service_atomicity`, 62 checks). A child process runs
  the request against the real database and `_exit`s at `before-record`, `after-record`, during
  the mutation (after the first of two messages), `before-commit` or `after-commit`, with its
  transaction open. The parent opens the database afresh (the restart) and repeats the ID.
  - Before the commit: nothing survives, the ID is unused, the retry runs, a second retry is
    answered from the record, and exactly two messages exist.
  - After the commit: both messages exist and the retry hears `sent: 2` without sending again.
  - `sessions.create` after the commit: the retry names the one session created.
  - `achievements.award`: the retry still reports `awarded: true`.
  - A recorded refusal repeats even after the state changed.
  - An injected `INTERNAL_ERROR` rolls everything back.
  - Sign-in results are not stored, and a refresh replay still revokes.
  - Legacy IDs answer `DUPLICATE_REQUEST`.
- CNA `ServiceRequestRetryTest` (4 cases) uses a loopback HTTP service that drops chosen
  connections. It checks:
  - a lost response is asked for once more under the same ID;
  - only once;
  - never against a service without `request-outcomes`;
  - never for sign-in.

## GSH-02 SystemLink guests

The join (ClientHello), welcome, join broadcast and AddLocalGamer end with a gamer-flags block,
`[0x47][count][flags]*count`, bit 0 = guest, written only when a gamer is a guest. Without guests the
bytes are the old format, so the online relay path (strict parser, no guests) is unchanged and
still refuses a claimed guest. Older SystemLink peers stop reading before the block. Evidence: codec
tests pin the old bytes; `TwoProcessLoopbackTest.GuestsAreGuestsOnEveryMachineAcrossRealProcesses`
(guests on both machines, leave, rejoin, a second guest via AddLocalGamer). Noted, not changed: a
local gamer who signs out does not leave a SystemLink session in CNA (XNA's rule there was not
re-established in this pass).

## GSH-03 Privileges and privacy

XNA contract (IL): seven properties from the account's native state; tri-state communication,
profile viewing and user content; only `ShowMarketplace` checks a bit in managed code, every other
refusal is the console's `ProfileNotPrivileged` mapped to `GamerPrivilegeException`; no block-list
API. CNA: operator-set account policy (admin `privilege`, schema 22), returned to the signed-in
client; the service enforces communication on messages, game and party invitations, join and
friend requests, and profile viewing on profile reads; the Guide refuses ShowComposeMessage,
ShowGameInvite and another gamer's ShowGamerCard when the privilege is Blocked (inferred from the IL
mapping and Guide.xml, not observed on a console). Blocking (gamer card, `privacy.*`) works both
ways, ends friendship and pending invitations, hides sessions, mutes voice. User content, trade and
premium are reported only. Evidence: server `PrivacyTests` (66 checks), `GuidePrivilegeTest`,
`GuideUiTest.TheGamerCardBlocksAndUnblocksAMember`.

## GSH-04 FriendGamer.HasVoice

XNA reads a console-supplied "friend has voice" bit ("currently has voice capability"); what set it
is not documented. CNA: the heartbeat says whether the client can talk now (voice built and on, a
recording device, communication not Blocked); friends see it while the friend is online. One
boolean; stale with presence (90 s). A CNA definition, recorded as such (limitation 18).

## GSH-05 Online QoS (kept unavailable)

XNA did expect QoS for matchmaking results (filled asynchronously; the same native query for every
session type). CNA's online path is searcher -> service relay -> host; before joining there is no
path to the host, the searcher's round trip to the service is not the game's path, and one leg's
bandwidth says nothing of the other's. A faithful measurement needs relay probes to the host before
joining (non-member relay authority, a host responder): new protocol. Kept unavailable rather than
invented; limitation 17 states it.

## GSH-06, GSH-07 Platform storage

`DatabaseLock`: flock on POSIX (macOS included), a no-sharing file on Windows; `DatabaseLockTests`
(separate processes, a holder that dies without cleanup) on Linux and, MinGW-built, under Wine.
Credentials: Secret Service via run-time libsecret (a keyring that refuses a write leaves the private
file; one that does not answer in two seconds counts as absent), DPAPI-sealed files on Windows
(`tools/gamer-services/windows-credential-probe`, 11 checks under Wine). Keyring tests run in a
private D-Bus session with a throwaway gnome-keyring. During development one early test run wrote
four test records into the owner's login keyring; they were identified by schema and deleted
(nothing else there was touched), and the store now uses the keyring only for the default location.

## GSH-08 Ranked + host migration

`RankedMigrationTests` (server): the host crashes mid-game; the service migrates the host (gamer ids
unchanged; the old host cannot end the game or keep its machine); the new host ends the game; one
machine reports and leaves; the service restarts; the other reports with its later revision and
retries. Found: that last report was NOT_FOUND (lost), because a report had to name a revision within
the round's start and end. Fixed: the latest round started at or before the revision. Verified:
exactly one round resolved once, one report per reporting machine, one row per gamer, retries
answered from the record, a second game's report goes to the second round. The CNA client-level
migration and host-crash end-to-end tests (`service_cna_session_migration`, `_host_crash`) pass.

## GSH-09 Avatar catalogs

Found: two installers of one pack (threads or processes) raced; the one that activated the pack
removed the other's staging (5 of 12 runs failed). Fixed with a per-version OS lock (flock,
LockFileEx); 0 of 20 afterwards, and 5 of 6 fail with the lock removed. Tests added: four threads,
two processes, a client restarted mid-install (staging reused, week-old staging removed), an
unavailable update, a pack claiming another version, v1-v3 descriptions unchanged by a newer
catalog (geometry fingerprints), an unknown description format. v1, v2 and v3 stay pinned by the
existing `CatalogV1/2/3IsFrozen`.

## GSH-10 Benchmarks

Server `benchmarks/2026-09-30-824decd.json`: steady 1,210 req/s, sign-in storm 1,013, 160 idle
connections 1,161, 90,000 request IDs 1,335, descriptor exhaustion survived; alternated with the
audited `a85a146` in the same minutes the difference is noise; under load average 13-17 the same run
measures half.

## GSH-14..17 Validation

- Windows/macOS: see GSH-06/07; nothing else runnable (the Windows 10 VM was deleted; no macOS).
- Renderers: one multi-renderer build (OPENGL33 default; VULKAN, SOFTWARE, SDL_GPU, WEBGPU), 257-frame
  avatar review on each through the private display runner; against OpenGL 3.3 the mean absolute
  difference is at most 0.10/255 and silhouette IoU at least 0.9995; sheets inspected. AvatarShadows
  runs on OpenGL ES 3. Evidence `/rv/tmp/gs-final-hardening/avatar-review/`.
- Audio (`tools/net/voice_physical_check.sh`): the real microphone's sound goes through speech
  detection, Opus and SystemLink to a decoder; a synthetic voice reaches the real output device intact
  (433 Hz share 0.9993); the six Guide sounds reach it. Speaker muted by its owner, left muted: no
  air loop, nothing heard. Programs without a Game must pump FrameworkDispatcher.Update for audio, as
  XNA requires.
- Public Internet: pending; no second network.

## GSH-20, GSH-21 Evidence corpus (final HEADs)

| Suite | Result |
|---|---|
| Server ctest with every CNA harness and NAT tools | 32/32 pass, 0 skipped |
| `CnaGamerServicesTests` | 627: 626 pass, 1 skip (`GuideTest.IsScreenSaverEnabledGetSet`), 0 fail |
| `CnaNetTests` | 523/523 pass |
| `CnaRuntimeTests` | 196: 194 pass, 2 skip (platform-window tests), 0 fail |
| C API gates (`-R '^CApi'`) | 116: 113 pass; 3 known environment failures of this HEADLESS/NULL-audio tree (Content, Audio, AudioUnavailable smokes), as in the previous baseline |
| Protocol drift (`check_service_protocol.py`) | match |
| SDL/platform boundary gates (5) | pass |
| Demos (18, private display) | 18/18 |
| Samples | AvatarShadows, Invites, NetworkStateManagement SystemLink and online, Achievements/Leaderboards: 5/5 |
| Avatar review, five renderers | all frames, see GSH-15 |

Evidence: `/rv/tmp/gs-final-hardening/`, `/rv/tmp/samples/*/evidence/*-20260930-final-hardening`.
