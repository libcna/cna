# CNA diagnostics and Inspector production-readiness audit

Status: complete (`AUD-DIAG-INSP-0001`, started and completed 2026-09-17)

Follow-up: `AUD-DIAG-INSP-0002` (2026-09-18) closes the socket and HTTP hypotheses this pass
recorded as **NOT TESTED IN THIS AUDIT PASS**, `AUD-DIAG-INSP-0003` (2026-09-18) closes
invalid UTF-8 and parser fuzzing, `AUD-DIAG-INSP-0004` (2026-09-18) is the first end-to-end run
against a real game in a real browser, and `AUD-DIAG-INSP-0005` (2026-09-18) is the first native
Windows build and run. See the *Follow-up fixes* sections below.

## Evidence vocabulary

- **PROVEN**: established directly from source/object inspection or a deterministic proof.
- **MEASURED**: observed quantitatively on the audit host.
- **TESTED**: exercised by a repeatable build, test, stress case, or malformed input.
- **NOT TESTED**: relevant, but not exercised in this environment.
- **NOT APPLICABLE**: outside the approved scope or unavailable by design.

## Starting point and environment

- **PROVEN** starting SHA: `dfc7b0aea41808693a3be18d295f27e069790de5`.
- **PROVEN** starting worktree: clean, branch `inspector`.
- **PROVEN** profiler foundation: `b7e856221b61c273e0ac085ec67d103e0ac7184a`.
- **PROVEN** Inspector implementation: `dfc7b0aea41808693a3be18d295f27e069790de5`.
- **MEASURED** audit start: `2026-09-17T20:52:40+02:00`.
- **MEASURED** host: Debian GNU/Linux, Linux `6.12.107+deb13-amd64`, x86-64.
- **MEASURED** toolchain: CMake 3.31.6, Ninja 1.12.1, GCC 14.2.0, Clang 19.1.7.
- **NOT TESTED** native Windows and macOS execution; cross-build evidence is recorded separately.

## Scope

The completed pass covers ordinary C++ correctness, concurrency, lifetime, build composition,
sanitizers, deterministic trace and JSON fixtures, metrics, resource accounting, regression, and
performance for `modules/diagnostics`, plus non-network Inspector regression evidence. The earlier
execution environment interrupted the broader audit after this ledger and the thread-identity
reproduction had been created. On resumption, the owner explicitly excluded new networking, HTTP,
authentication, security, penetration-testing, protocol-abuse, fuzzing, and socket edge-case
experiments. Those original hypotheses remain below and are explicitly marked **NOT TESTED IN THIS
AUDIT PASS**; this ledger does not claim that the security/network audit was completed.

## Invariants under audit

### Diagnostics compile-time OFF

- Instrumentation macro arguments are not evaluated.
- Engine objects contain no diagnostics symbol references or diagnostics-owned initialization.
- Conditional resource state is absent and representative object sizes/code paths are unchanged.
- Hot paths contain no diagnostics branches, allocations, locks, or temporary construction.

### Diagnostics STATS/FULL

- Counters remain cumulative, gauges persist, and frame counters reset once per completed frame.
- Frames are monotonic and bounded; malformed transitions do not corrupt later frames.
- FULL-only zones remain absent from STATS.
- Zone parent/thread identities remain unambiguous across nesting, concurrency, and thread reuse.
- Runtime-mode transitions invalidate open zones safely and cannot corrupt later scopes.
- Per-thread buffers, process history, name/metric registries, and recordings remain bounded.
- Thread exit cannot race a snapshot/event drain into a dangling context.
- Resource IDs are stable and non-reused; metadata removal/update is race-safe and byte totals do
  not wrap into plausible values.
- Trace parsing treats every size/count/enum/reserved/identifier field as hostile and fails
  deterministically without excessive allocation or out-of-bounds access.
- Chrome Trace output remains valid JSON for empty, nested, concurrent, control-character, Unicode,
  long, and malformed byte-string names.

### Inspector lifecycle and transport

- Merely linking creates no endpoint/thread; starting without a client makes zero provider calls.
- Authentication and negotiation complete before any provider or preview-provider access.
- The wire header is exactly 24 explicit little-endian bytes and never uses structure layout.
- TCP fragmentation, coalescing, partial I/O, interruption, peer close/reset, and timeouts are safe.
- Unknown, malformed, oversized, out-of-order, or unauthenticated messages cannot desynchronize the
  stream or reach providers.
- A slow/disconnected client cannot block the game/render thread or create an unbounded queue.
- Stop/destruction always interrupts socket work and has bounded latency for conforming providers.
- Provider over-return, errors, discontinuities, and name-resolution failures remain contained.
- Preview work is explicit/asynchronous, limited to four pending operations by default, and bounded
  by dimensions/bytes/tickets even after disconnect or provider failure.

### Browser bridge and security

- HTTP request/header/target size is bounded; Host and per-process UI-token checks precede APIs.
- Duplicate/conflicting framing headers, traversal forms, invalid methods, and malformed input fail
  without serving files or forwarding provider work.
- Assets are embedded; no path can escape to the local filesystem.
- CSP is restrictive, CORS is absent, and reusable public URLs do not carry agent credentials.
- Agent tokens come only from OS secure randomness when generated and no insecure fallback exists.
- Non-loopback agent binding requires explicit opt-in; the bridge always binds IPv4 loopback.

## Source-review hypotheses and final classifications

- **NOT TESTED IN THIS AUDIT PASS**: POSIX `select` uses `FD_SET` and may be unsafe when an
  Inspector descriptor is at least `FD_SETSIZE`; interrupted waits currently return failure rather
  than retrying. This is a socket edge case excluded from the resumed audit.
- **PROVEN**: `std::hash<std::thread::id>` reused a published profiler identity after short-lived
  threads terminated. The deterministic reproduction produced 256 events from 256 sequential
  `std::thread` instances but only one distinct published thread identity.
- **NOT TESTED IN THIS AUDIT PASS**: CNATRACE decoding bounds allocations but does not yet reject
  every inconsistent identifier, reserved field, or trailing byte pattern. Ordinary deterministic
  empty, populated, overflow, and round-trip fixtures were tested; no fuzzing or offensive parser
  testing was performed.
- **NOT TESTED IN THIS AUDIT PASS**: Chrome Trace JSON escaping with invalid UTF-8. Ordinary Unicode
  (`Ω`), quotes, and backslashes were tested successfully.
- **NOT TESTED IN THIS AUDIT PASS**: an injected Inspector provider returning more events than
  requested before the agent encoder enforces its response limit. This is protocol-abuse testing.
- **PROVEN**: frame-source collection occurred before the active-frame check. Unmatched `EndFrame`
  and OFF-mode `BeginFrame`/`EndFrame` pairs could invoke registered sources without a frame, and
  concurrent frame completion was not serialized. This was fixed and covered by deterministic
  source/frame-transition tests.
- **NOT TESTED IN THIS AUDIT PASS**: HTTP duplicate headers and conflicting `Content-Length` values.
  Hostile live HTTP testing was explicitly excluded.

## Experiments and commands

Representative repeatable commands (all run from the repository root):

```text
cmake -S . -B cmake-build-audit-{off,stats,full}-release -G Ninja ...
cmake --build cmake-build-audit-<configuration> --target <focused targets> -j2
./cmake-build-audit-<configuration>/CnaDiagnosticsTests
./cmake-build-audit-full-release/CnaGraphicsTests --gtest_filter=GraphicsDiagnosticsTest.*
./cmake-build-audit-full-release/CnaRuntimeTests
./cmake-build-audit-full-release/CnaInspectorTests --gtest_filter=<non-network subset>
CNA_AUDIO_DEVICE=null ./cmake-build-audit-alsa-release/CnaAudioTests \
  --gtest_filter=AudioDiagnosticsTest.*
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ./cmake-build-audit-asan-ubsan/CnaDiagnosticsTests
TSAN_OPTIONS=halt_on_error=1 ./cmake-build-audit-tsan/CnaDiagnosticsTests
cmake --build cmake-build-audit-strict --target CnaDiagnosticsTests CnaInspectorTests
cmake --build cmake-build-audit-mingw --target cna_diagnostics cna_inspector -j2
git diff --check
```

- **TESTED**: Release HEADLESS/NULL configurations for Diagnostics OFF, STATS, and FULL.
- **TESTED**: Debug HEADLESS/NULL Diagnostics FULL with Inspector ON.
- **TESTED**: explicit `-Wall -Wextra -Wpedantic -Werror` focused build and tests.
- **TESTED**: Inspector OFF and ON build composition; the ON live benchmark and socket lifecycle
  cases were built but not executed because their entry points necessarily bind/listen.
- **TESTED**: x86-64 MinGW cross-compilation of `cna_diagnostics` and `cna_inspector`.
- **TESTED**: the existing malformed-magic CNATRACE unit test only. No fuzz target or fuzz corpus was
  executed.

## Findings and fixes

### Thread identity reuse

- Severity: high for historical profiler correctness.
- **MEASURED** reproduction before the fix: `events=256 distinct_thread_ids=1` for 256 sequential
  threads, each publishing one event.
- **PROVEN** root cause: `GetThreadContext()` derived the public diagnostics identity from
  `std::hash<std::thread::id>`. The runtime reused the native `std::thread::id` after each joined
  thread; process event history outlived those thread contexts, so unrelated historic threads
  became indistinguishable.
- **PROVEN** fix: every newly registered TLS `ThreadContext` now obtains an ID from a process-local
  monotonic `std::atomic<std::uint64_t>`. Allocation uses relaxed compare/exchange only during first
  context creation, not per event. `UINT64_MAX` is issued once, the allocator then saturates to
  zero, and subsequent registrations fail through the existing producer-drop path instead of
  reusing an ID. Public APIs and event/trace formats are unchanged.
- **TESTED**: 256 sequential threads publish 256 distinct IDs; 64 concurrently-created threads
  publish 64 distinct IDs; normal nested zones retain parent/child relationships.

### Resource accounting and snapshot consistency

- Severity: medium for metrics correctness.
- **PROVEN** root cause: `registeredResourceBytes` used wrapping atomic addition/subtraction. Two
  valid large estimates could wrap to a plausible small total. The snapshot also loaded the total
  before locking the resource map, allowing one snapshot to combine a total and records from
  different lifecycle instants.
- **PROVEN** fix: byte accounting is protected by the existing resource mutex, uses saturating
  addition, and recomputes from live records when an update/removal follows saturation. Snapshot
  records and their total are copied under the same lock. Resource and source ID allocators now
  saturate at exhaustion and return an invalid zero handle rather than wrap and reuse IDs.
- **TESTED**: `UINT64_MAX + 1` saturates, later update/removal recovers the exact total, and 2,000
  ordinary create/update/destroy cycles restore baseline resource count and bytes.
- **TESTED**: concurrent snapshots during 2,000 worker lifecycle cycles always equal the saturating
  sum of the records in that same snapshot. Resource-handle move construction and move assignment
  leave exactly one live registration.

### Frame completion and optional sources

- Severity: medium for frame/source semantics.
- **PROVEN** root cause: sources were collected before checking `frameActive`, and simultaneous
  `FinishFrame` calls could overlap source collection and completion.
- **PROVEN** fix: frame completion is serialized, frame activity is validated first, OFF-mode
  unmatched ends remain no-ops, and STATS/FULL unmatched ends increment the malformed-frame counter
  without invoking sources. Source callbacks are still copied under the source mutex and invoked
  after releasing it.
- **TESTED**: invalid and OFF transitions do not call sources; a valid frame boundary calls once;
  ordinary OFF/STATS/FULL transitions, repeated cycles, worker lifetime, and an open scope across a
  mode transition remain safe.

### Reviewed lifetime and boundedness properties

- **PROVEN**: producer buffers remain per-thread SPSC rings; publication is release/acquire.
- **PROVEN**: TLS destruction holds the thread registry mutex, drains final events, removes the
  pointer, and only then deletes the context. Snapshots cannot retain a dangling context pointer.
- **PROVEN**: optional source callbacks retain copied `shared_ptr` instances during collection, so
  concurrent deregistration cannot invalidate the active callback.
- **PROVEN**: process history, per-thread buffers, frame history, metric registry, and name registry
  are bounded. Thread contexts are released at thread exit; resource records retain only live
  registrations. The Diagnostics state itself is intentionally process-lifetime so late TLS
  destructors never access destroyed static state.
- **TESTED**: an active worker can be snapshotted and its profiler-owned bytes return to baseline
  after join; bounded producer/history overflow counters advance as documented.

## Verification results

### Tests and build matrix

| Configuration | Inspector | Result |
|---|---:|---|
| Release, Diagnostics OFF, HEADLESS/NULL | OFF | **TESTED** build; Diagnostics 2/2 pass |
| Release, Diagnostics STATS, HEADLESS/NULL | OFF | **TESTED** build; Diagnostics 14/14 pass |
| Release, Diagnostics FULL, HEADLESS/NULL | ON | **TESTED** build; Diagnostics 34/34 pass |
| Debug, Diagnostics FULL, HEADLESS/NULL | ON | **TESTED** build; Diagnostics 34/34 and non-network Inspector 7/7 pass |
| Debug FULL, strict warnings | ON | **TESTED** build with `-Wall -Wextra -Wpedantic -Werror`; 34/34 + 7/7 pass |
| Debug FULL, ASan+UBSan | ON | **TESTED** 34/34 + 7/7 pass, no finding |
| Debug FULL, TSan | OFF | **TESTED** 34/34 pass, no race report |
| Release FULL, ALSA | OFF | **TESTED** exact audio metric test 1/1 pass |
| Release FULL, MinGW x86-64 cross-build | ON | **TESTED** Diagnostics and Inspector libraries compile/link |

Additional release regression results:

- **TESTED** graphics instrumentation: 5/5 pass (exact indexed/non-indexed draws, primitives,
  effect changes, texture bindings, render-target changes, SpriteBatch submissions, resource-byte
  estimates).
- **TESTED** runtime instrumentation regressions: 176 executed, 174 pass and 2 HEADLESS-specific
  skips.
- **TESTED** Inspector protocol/frontend/non-loopback validation subset: 7/7 pass.
- **NOT TESTED IN THIS AUDIT PASS**: the other 17 existing Inspector agent lifecycle tests. One
  attempted run showed every case unable to start because the sandbox rejects `bind`/`listen` with
  `Operation not permitted`; no escalation or socket workaround was attempted.

### Metrics, traces, and JSON

- **TESTED** exact cumulative counters, gauges, frame-counter reset, 240-frame bounded history,
  monotonic frame numbers, and FPS calculation.
- **TESTED** exact graphics draw/state/submission metrics and ALSA mixer allocated-voice gauge plus
  cumulative voice-creation counter.
- **TESTED** CNATRACE empty and one-event round trips; all event fields, names, identities,
  timestamps, categories, and kinds survive serialization.
- **TESTED** nested zones, multiple threads, maximum documented recording capacity (32,768 events),
  exact overflow/drop count, ordered sequences/timestamps, and recording/trace move construction and
  assignment. The overflow drop indicator also survives binary save/load.
- **TESTED** exact empty Chrome JSON plus nested, multi-thread, ordinary Unicode, quote, and
  backslash output fixtures.

### Sanitizers and dynamic analysis

- **TESTED** GCC AddressSanitizer + UndefinedBehaviorSanitizer: all 34 Diagnostics and 7
  non-network Inspector tests pass with `halt_on_error=1`, no report.
- **TESTED** GCC ThreadSanitizer: all 34 Diagnostics tests pass with `halt_on_error=1`, including
  sequential/concurrent thread registration, active-worker snapshots, and concurrent resource
  lifecycle snapshots; no race report.
- **NOT TESTED** LeakSanitizer: it exits with `LeakSanitizer does not work under ptrace` in this
  execution environment. ASan was rerun with leak scanning disabled.

### Fuzzing and malformed-input corpus

- **NOT TESTED IN THIS AUDIT PASS**: fuzzing and offensive/malformed parser corpora were explicitly
  excluded. The existing deterministic invalid-magic CNATRACE rejection test passed; it is not
  represented as a fuzz/security result.

### Stress and resource high-water marks

- **TESTED**: 256 sequential thread lifetimes, 64 concurrent thread registrations, 2,000 repeated
  resource lifecycles, 2,000 resource lifecycles under concurrent snapshots, full 32,768-event
  recording retention, thread-buffer overflow, and bounded process-history overwrite.
- **TESTED**: maximum supported signed graphics dimensions are converted without intermediate
  overflow and saturate the public byte estimate to `UINT64_MAX`.

### Performance reproduction

Five Release runs per Diagnostics mode; values are median net overhead in ns/op:

| Path | Before fix | After fix | After range |
|---|---:|---:|---:|
| OFF counter | not separately retained before interruption | -0.010 | -0.059 to 0.047 |
| OFF zone | not separately retained before interruption | 0.047 | 0.026 to 0.063 |
| STATS counter | not separately retained before interruption | 4.340 | 4.228 to 4.422 |
| FULL counter | 2.920 | 1.903 | 1.273 to 3.720 |
| FULL completed zone | 85.526 | 88.204 | 60.337 to 98.534 |

- **MEASURED**: FULL zone median changed by +3.1%, with before/after ranges strongly overlapping;
  FULL counter improved in this noisy host sample. There is no material hot-path regression. The
  identity allocator runs only on first event publication by a new thread and adds no per-event
  work.
- **MEASURED** Inspector compiled-out control medians: OFF 1.495, STATS 1.462, FULL 1.569 ns/op.
- **NOT TESTED IN THIS AUDIT PASS**: the live Inspector benchmark. It necessarily opens local
  sockets. Inspector implementation code did not change in this audit, and the benchmark target
  compiled successfully.

### Cross-platform evidence

- **TESTED**: x86-64 MinGW-w64 Release cross-build of the changed Diagnostics library and the
  Inspector library with `CNA_PLATFORM=WIN32`, Diagnostics FULL, and Inspector ON.
- **NOT TESTED**: execution on native Windows or macOS.

## Follow-up fixes (`AUD-DIAG-INSP-0002`, 2026-09-18)

The network hypotheses this audit deferred were exercised on a host where `bind`/`listen` are
permitted. Two of them were real defects and are fixed; the remainder are recorded as tested.

### Descriptor-set overflow in the socket wait (critical)

- Severity: critical. An out-of-bounds write inside the host game's own process.
- **PROVEN** root cause: `Wait()` published the socket through `FD_SET`. A POSIX `fd_set` is a
  bitmap indexed by descriptor number, so a descriptor at or above `FD_SETSIZE` (1024) wrote past
  the end of the 128-byte stack object. The agent runs inside the game, which reaches that
  descriptor count with ordinary files.
- **MEASURED** reproduction: `cna-inspector` launched with inherited descriptors so its listener
  landed above the limit. Bisection: top descriptor 503 served normally, 903 served normally,
  1103 accepted nothing and the process hung with no diagnostic. AddressSanitizer on the same
  input reported `stack-buffer-overflow ... in Wait` at `InternalSocket.cpp`.
- **PROVEN** fix: POSIX waits use `poll()`, which addresses the descriptor by value. Windows keeps
  `select()`, whose `fd_set` is a counted handle array and is unaffected; its ignored first
  argument is now passed as `0`. An `EINTR` wait now resumes on the remaining timeout instead of
  failing, which was a second hypothesis recorded in this ledger.
- **TESTED**: `InspectorAgentTests.ServesClientsWhenSocketsExceedTheDescriptorSetLimit` fills the
  descriptor table past `FD_SETSIZE` and completes a real authenticated snapshot round trip; it
  skips where the descriptor limit cannot reach that far. Against the previous implementation the
  same test does not complete and is killed by its timeout. Top descriptor 2203 now serves
  normally, and AddressSanitizer reports nothing with 1,100 descriptors held.

### Serial browser bridge (high)

- Severity: high for ordinary use, not only under abuse.
- **PROVEN** root cause: `WebBridge::Run()` ran one connection to completion before accepting the
  next, and the request-header read waited the full five-second response timeout. Browsers open
  speculative connections and keep idle ones for reuse, so each idle socket delayed every later
  request.
- **MEASURED** before: 1 idle socket 5.5 s, 3 idle sockets 14.7 s, growing linearly and without
  bound.
- **PROVEN** fix: connections are served concurrently, bounded at 32, with excess connections
  refused rather than queued; the request-header wait is one second; the single agent link is
  serialized by its own mutex, so static assets stay fully concurrent while API requests keep the
  agent's one-request-at-a-time contract. Connection threads are joined before the bridge is
  destroyed.
- **MEASURED** after: 0.00 s with 1, 3 and 8 idle sockets, and six parallel asset requests in
  0.00 s.
- **TESTED**: Host, UI-token, traversal and normal-page gates re-verified live after the change;
  a 40-connection flood is refused and recovers; the process returns to one thread and four
  descriptors afterwards.

### Event cursor scan and bound (medium)

- **PROVEN** root cause: `ReadEvents` scanned the whole 32,768-entry history on every poll while
  holding `historyMutex`, which frame completion also needs on the game thread. `afterSequence + 1`
  also wrapped at `UINT64_MAX`, which a client can send through the bridge's unvalidated `after=`
  query, producing a fabricated discontinuity count.
- **PROVEN** fix: sequences are consecutive within the ring, so a poll starts at its first unseen
  event; `UINT64_MAX` and cursors past the newest sequence return empty without a phantom
  discontinuity.
- **TESTED**: `DiagnosticsEventsTest.IncrementalCursorReturnsExactlyTheUnseenEvents` and
  `DiagnosticsEventsTest.ACursorBeyondTheHistoryReportsNoPhantomDiscontinuity`.

### Unchecked instrumentation arguments at OFF (medium)

- **PROVEN** root cause: the OFF macros expanded to `do { } while (false)` and discarded their
  arguments, so instrumentation call sites were never compiled. `CNA_DIAGNOSTICS=OFF` is the
  default, so a broken call site passed an ordinary build and failed only for whoever enabled
  STATS or FULL.
- **MEASURED**: a file with three deliberately broken instrumentation calls produced 0 errors at
  OFF and 4 at FULL.
- **PROVEN** fix: the OFF macros keep the arguments inside `sizeof`, which type-checks them
  without evaluating them, so instrumentation still costs nothing.
- **TESTED**: broken call sites now produce errors at levels 0, 1 and 2; valid call sites compile
  clean with `-Wall -Wextra` at all three. Seven of the eight instrumented translation units
  compile clean at all three levels; `Backend/Sdl3Mixer/MixerEngine.cpp` is **NOT TESTED** because
  no configured build tree selects it, and its four call sites are the same literal forms as the
  CnaMixer ones that were tested.

### Verification after the follow-up

| Configuration | Result |
|---|---|
| Release FULL HEADLESS/NULL, Inspector ON | Diagnostics 36/36, Inspector 25/25, Graphics 2,325 passed, Runtime 174 passed |
| Release STATS / OFF | 14/14 and 2/2 |
| Debug FULL ASan+UBSan | 36/36 + 25/25, no report |
| Debug FULL TSan, Inspector ON | 36/36 + 25/25, no race report — TSan covered the Inspector for the first time |
| Debug FULL `-Wall -Wextra -Wpedantic -Werror` | 36/36 + 25/25 |
| MinGW-w64 x86-64 cross-build | `cna_diagnostics` and `cna_inspector` compile and link, Winsock branch included |

Duplicate and conflicting `Content-Length` headers were tested live and are inert, because the
bridge closes the connection after one request and never reads a body. Fuzzing and invalid UTF-8
were closed by `AUD-DIAG-INSP-0003` below.

## Follow-up fixes (`AUD-DIAG-INSP-0003`, 2026-09-18)

Closes the last two items `AUD-DIAG-INSP-0002` left **NOT TESTED** and one source-review finding.

### Invalid UTF-8 names (medium)

- **PROVEN** root cause: names reach the profiler from game code and from trace files, and neither
  is required to be valid UTF-8. Nothing validated them. The Chrome exporter copied bytes straight
  into its JSON, producing a document a strict parser rejects. The Inspector encoder does validate,
  so it threw on the first invalid name; the agent contains that as a structured error, but the
  name stays in the registry, so **every later events response failed** for the rest of the process.
- **PROVEN** fix: one validator rejects overlong encodings, UTF-16 surrogates, values above
  U+10FFFF, truncated sequences and stray continuation bytes, replacing each invalid byte with
  U+FFFD so the name stays readable instead of vanishing. It runs where names enter: event and zone
  names, metric names, resource labels and formats, and names read by `Trace::ReadBinary`. The Chrome
  exporter validates again, because a `Trace` may come from a file this process did not write. Size
  bounds now apply to the sanitized form, which can be up to three times longer.
- **TESTED**: `InvalidUtf8NamesStillProduceValidChromeJson` covers all five invalid forms;
  `InvalidUtf8MetricAndResourceNamesAreStoredValid` covers the metric and resource paths at STATS.

### Parser fuzzing (previously NOT TESTED)

Seeded and deterministic, so a failure reproduces from the seed and runs in ordinary CI.

- **TESTED** CNATRACE: 3,000 mutations of a real trace (byte overwrite, truncation, insertion). Every
  input is rejected or read back into a trace whose names are valid UTF-8 and whose export is valid
  UTF-8 JSON. **This found a defect**: `ReadBinary` stored file names unchecked, so `ResolveName` on a
  loaded trace returned invalid UTF-8 to consumers. Fixed as above.
- **TESTED** Inspector payloads: 7,000 mutations across `SnapshotResponse`, `EventsResponse`,
  `HelloRequest` and `EventsRequest`, including extreme values aimed at length and count fields. No
  decoder threw, and every value a decoder accepted re-encoded; the encoder enforces UTF-8 and every
  bound, so that checks both. **No defect found** — the protocol codec holds.
- **TESTED** Inspector headers: 20,000 mutated headers; none that decoded carried a payload size above
  the 8 MiB bound the receive path allocates from.

### Frame completion re-entry (low)

- **PROVEN** root cause: source callbacks run while frame completion holds `frameCompletionMutex`,
  which is not recursive. A source that called `EndFrame()` from its own `Collect()` blocked forever.
- **PROVEN** fix: a thread-local guard refuses the nested end and counts it as a malformed frame
  transition; the outer frame still completes.
- **TESTED**: `ASourceEndingAFrameFromCollectIsRefusedNotDeadlocked`. Against the previous code the
  same test deadlocks and is killed by its 20-second limit.

### Verification after `AUD-DIAG-INSP-0003`

| Configuration | Result |
|---|---|
| Release FULL HEADLESS/NULL, Inspector ON | Diagnostics 40/40, Inspector 27/27, Graphics 2,325 passed, Runtime 174 passed |
| Release STATS / OFF | 16/16 and 2/2 |
| Release FULL ALSA | Audio diagnostics 1/1 |
| Debug FULL ASan+UBSan | 40/40 + 27/27, no report |
| Debug FULL TSan, Inspector ON | 40/40 + 27/27, no race report |
| Debug FULL `-Wall -Wextra -Wpedantic -Werror` | 40/40 + 27/27, no warning in any of the seven configurations |
| MinGW-w64 x86-64 cross-build | `cna_diagnostics` and `cna_inspector` compile and link |

The strict build caught two defects in this task's own new tests before commit. One was a
dangling `else` around an `EXPECT_TRUE`. The other was the literal `"Bad\x80End"`: `E` is a hex
digit, so C++ reads `\x80E` as a single out-of-range escape, and in the non-strict build that case
had silently become the valid ASCII byte `0x0E`, passing without testing a continuation byte at
all. Both are fixed; the literal is split so the escape ends at `0x80`.

Still **NOT TESTED**: execution on native Windows or macOS. The fuzzers are seeded mutation corpora,
not coverage-guided fuzzing; they exercise every decoder that parses untrusted input but do not
search the input space the way libFuzzer would.

## Remaining limitations

- All networking, HTTP, authentication, security, penetration-testing, protocol-abuse, fuzzing,
  and socket edge-case hypotheses are **NOT TESTED IN THIS AUDIT PASS**. They are not silently
  treated as validated.
- Normal Inspector start/stop and linked-but-unused runtime measurements are **NOT TESTED IN THIS
  AUDIT PASS** because the existing tests/benchmark necessarily bind a socket and the sandbox
  rejected that operation. Their build configurations are **TESTED**.
- LeakSanitizer is unavailable under the environment's ptrace/sandbox arrangement.
- The 64-bit thread/resource/source exhaustion behavior is **PROVEN** by source inspection, not by
  iterating to `UINT64_MAX`. Other 64-bit counters and event sequences retain the practical
  process-lifetime assumption that they will not perform 2^64 increments.
- The workstream is sufficiently validated for the explicitly scoped C++ correctness,
  concurrency, lifetime, regression, and performance concerns and can move to normal maintenance.
  This conclusion does not close the separately untested networking/security scope.

## Follow-up fixes (`AUD-DIAG-INSP-0004`, 2026-09-18): first end-to-end run

Until this task nothing in the tree started an agent except the tests and a benchmark with a fake
provider, so the Inspector had never been used as a product: a game, its agent, the bridge and a
browser together. The serial-bridge defect of `AUD-DIAG-INSP-0002` already implied as much, since a
browser would have hit it within the first minute.

### Method

- **PROVEN** host: `cna_inspector_demo` (new, `modules/inspector/examples/`), a game that opts in
  exactly as `docs/inspector.md` describes. It draws 48 sprites into a render target and composes
  it, owns two textures, a render target and a vertex and an index buffer, publishes CPU zones,
  markers and a gauge, and creates and destroys a texture every two seconds. HEADLESS platform and
  renderer, NULL audio, Diagnostics FULL, Debug.
- **PROVEN** browser: Google Chrome 152, headless, driven over the DevTools Protocol (Node 20's
  built-in WebSocket, no dependency). Each run loads the page, refreshes resources, visits all
  eight views with a screenshot of each, records every console message, exception, CSP report and
  network request, then kills the game while the page is open.

### What held

- **TESTED**: session negotiation and metadata, the frame-time graph and FPS (60.2), CPU zones and
  the timeline, markers, resource metadata, and the Input view's explicit unavailable state. No
  JavaScript exception and no CSP violation in any run. Resource metadata is exact end to end: every
  described resource carried its kind, size and byte estimate, and their sum matched the provider's
  total to the byte (535,552).
- **TESTED**: killing the game turns the page to RECONNECTING within one poll, it keeps polling at
  about two requests per second without flooding, and the bridge survives.

### Defects found and fixed

1. **State assignment re-registered resources (high).** `GraphicsResource::operator=` registered a
   new diagnostic resource that `ShareResourceIdentityWith()` released a few lines later, in every
   one of its five callers. `SpriteBatch::Begin` assigns four states, so every call published four
   create/destroy pairs and did a map insert and erase on the draw path, in STATS as well. Found by
   backtracing a registration in gdb. The assignment no longer registers.
   `StateAssignmentAndSpriteBatchBeginRegisterNoTransientResources` consumed 303 resource IDs where
   103 were expected before the fix, and exactly 103 after.
2. **Event views were not live (high).** The page read events from the oldest in the 32,768-entry
   history at 512 per 500 ms. Measured: the Events view showed frame 1,368 while the game was at
   frame 2,519, about 19 s behind, and it can never catch up once a game produces events faster
   than 1,024 per second. The page now starts at the live tail and skips ahead when more than 1,000
   events behind; after the fix the newest event shown was from the current frame.
3. **Loss banner was permanent (medium).** It showed the process-cumulative ring-overwrite counter,
   which is non-zero as soon as the history wraps, and displayed a real gap for only one poll. It now
   reports what this view missed since it connected, and keeps it. After the fix it stayed empty
   through 60 s of normal operation with the ring wrapped.
4. **Unpublished metrics shown as zero (medium).** A metric the build never published read as `0`,
   so a `SpriteBatch`-only game showed "Draw calls 0". It now shows `—`, the UI's own convention.
5. **Bridge URL never appeared when stdout was redirected (medium).** The line was not flushed before
   `Run()`, which never returns, so a launcher script, `tee` or an IDE never showed the ephemeral URL.
6. **`/favicon.ico` 404 on every load (low)**, logged as a console error. The page now declares an
   empty icon.

### Found and left open

- **`SpriteBatch::Begin(SpriteSortMode, BlendState)` takes `BlendState` by value** and forwards it by
  value, so each call constructs two new `BlendState` objects, each with a heap-allocated state.
  C# passes a reference. Diagnostics reports these lifetimes correctly; the cost is the engine's.
  Fixing it changes a public XNA API signature, which is outside this task.
- **19 of 25 resources in the demo are `unknown`**: the stock and `SpriteBatch`-owned state objects and
  vertex declarations are `GraphicsResource`s that never describe themselves. Truthful, but it puts
  the described textures and buffers below a page of empty rows.
- **`SpriteBatch` draws are not draw calls**: sprites go to the renderer directly, not through
  `GraphicsDevice`, so a `SpriteBatch`-only game publishes no `Graphics/DrawCalls`. Now stated as
  `—` in the UI and documented, not invented.

### Verification

| Configuration | Result |
|---|---|
| Debug FULL HEADLESS/NULL (`build-probe/`) | Graphics 2,326 passed, Runtime 174, Diagnostics 40/40, Inspector 27/27 |
| End to end, Chrome 152 headless | all eight views populated, no exception, no CSP report; live tail, empty banner, game-exit recovery |
| Demo, `-Wall -Wextra -Wshadow -Wconversion -Werror` | clean |

## Follow-up fixes (`AUD-DIAG-INSP-0005`, 2026-09-18): first native Windows build and run

Until this task the Windows claim rested on a MinGW-w64 cross-build, and the Winsock and BCrypt
code had never executed.

### Method

- **PROVEN** host: the `win10_local` VirtualBox guest, Windows 10 22H2, MSVC 19.44.35229 x64, Ninja,
  driven with `tools/platform/windows_vm_exec.sh`. Configuration `C:\cna\build\build-probe`: Debug,
  HEADLESS platform and renderer, NULL audio, SDL, net and Draco off, Diagnostics FULL, Inspector
  and examples on. Targets `CnaDiagnosticsTests`, `CnaInspectorTests`, `cna-inspector`,
  `cna_inspector_demo`, built with `-k 0` so one pass reports every error.

### Defects found and fixed

1. **Missing `<array>` in `Agent.cpp` (build break).** The agent's token generator used
   `std::array` with no direct include; libstdc++ supplies it transitively, which is why both the
   Linux build and the MinGW gate passed, while MSVC's standard library does not. It was the only
   failed step in the whole build. A scan of both modules for standard facilities not guaranteed by
   the file or by any project header it includes found no other case, which matters because the
   macOS claim rests on libc++, whose transitive includes differ again.
2. **`windows_vm_sync.sh` refused linked worktrees.** It tested `.git` as a directory, which is a file
   in a worktree, so the Windows lab could not be used from one. It now asks git.
3. **The lab sync reported an unreadable checkout as clean.** Once the worktree could sync, its
   `vendor/googletest` payload (a submodule checkout there, a plain directory in the main checkout)
   carried a gitlink file that points nowhere in the guest, and every git command in the guest
   checkout then failed with exit 128. This task's own sync introduced that into the shared lab. It
   went unnoticed because the guest check counted the lines of `git status --porcelain`, and a
   failed status prints none, so it reported `clean = True`. Payloads now exclude `.git` from the
   archive and the stamp, the guest step removes any gitlink an earlier sync left, and a failed
   status stops the sync instead of reporting clean. The guest was repaired the same session: status
   exits 0 with no changes. The test results below stand: tracked files were reset to the commit
   and a gitlink does not take part in compilation.

### What held

- **TESTED** natively: Diagnostics 40/40, Inspector 26/26 (the descriptor-set test is POSIX-only).
  That covers real Winsock loopback authentication, negotiation, malformed clients, reconnect,
  request-rate backpressure and previews. No MSVC warning in any diagnostics, Inspector or
  `GraphicsResource` source.
- **TESTED** end to end on Windows: the demo prints its port and token; the bridge prints its URL
  with stdout redirected to a file; the session reports `target=Windows`, `localOnly=True`; resource
  metadata is exact (textures, render target, buffers, 519,168 declared bytes); the 240-frame history
  and all eight demo CPU zones arrive; with three idle Winsock connections open a request completes
  in 10 ms; killing the game yields HTTP 503 and the bridge keeps running.
- **MEASURED** in passing: the demo ran at about 34 fps (29 ms frames) in this guest under HEADLESS,
  against about 60 on Linux. Not investigated; the lab notes record that this guest's timing is
  irregular, so it is not evidence about CNA either way.

### Plan history

Commit `fd02a5999` erased this file: a one-line newline cleanup opened it for writing before reading
it. It was restored from `2495e4de1` and this section rewritten in the following commit.

Still **NOT TESTED**: native macOS; a browser on Windows (the page itself is platform-neutral and
was exercised in Chrome on Linux).
