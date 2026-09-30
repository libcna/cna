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

| Id | Finding | Class | Status |
|---|---|---|---|
| GSH-01 | A recorded request ID committed in its own transaction before the mutation ran; a crash in between left the ID spent and the change missing, so the retry was refused as a duplicate | DEFECT | Fixed (below) |
| GSH-02 | A SystemLink guest reads `IsGuest` false on every other machine: the roster carries no guest flag | COMPAT | Open |
| GSH-03 | `GamerPrivileges` other than online sessions and purchase are always `Everyone`/true; no block list; no profile privacy | COMPAT | Open |
| GSH-04 | `FriendGamer.HasVoice` is always false | COMPAT | Open |
| GSH-05 | Online (PlayerMatch/Ranked) search results never report QoS | COMPAT | Open |
| GSH-06 | The single-owner database lock is `flock` under `#ifdef __unix__`: nothing on Windows, and nothing on macOS (which does not define `__unix__`) | PLATFORM + DEFECT | Open |
| GSH-07 | Refresh credentials: POSIX keeps them in owner-only files, not encrypted at rest; Windows keeps none | HARDEN + PLATFORM | Open |
| GSH-08 | Host migration combined with Ranked results, a server restart and reconnects has no adversarial test | INFER | Open |
| GSH-09 | Avatar catalog install: gaps for concurrent installs, a client restart during staging, a restart of the server during a download and an unknown description format | INFER | Open |
| GSH-10 | README throughput figures have no committed raw evidence | VALID | Open |
| GSH-11 | README wording on TLS handshake and connection rate limits | DOC | Open |
| GSH-12 | README wording claims more Xbox 360 fidelity than was verified | DOC | Open |
| GSH-13 | `NEXTnet.md`, inventories and old plans read as current | DOC | Open |
| GSH-14 | Windows and macOS clients and server hosts are unvalidated | VALID | Open |
| GSH-15 | `AvatarRenderer` was measured only on OpenGL 3.3 and Vulkan | VALID | Open |
| GSH-16 | Voice and Guide sounds were never heard through real devices | VALID | Open |
| GSH-17 | No public-Internet qualification | VALID | Open |
| GSH-18 | Xbox LIVE wire, Microsoft accounts and assets, PartnerToken, Marketplace, TrueSkill, the Recent window | NONGOAL | Kept |
| GSH-19 | SQLite and no clustering | NONGOAL | Kept |
| GSH-20 | Full evidence corpus | VALID | Open |
| GSH-21 | Sample acceptance | VALID | Open |
| GSH-22 | Canonical limitations list | DOC | Open |

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
