# NEXT.md — CNA Project Handoff (Devices)

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3
with a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It preserves
XNA-style public APIs (`Microsoft::Xna::Framework`, `Microsoft::Devices`) while using
modern C++ internally. Branch: `feature/devices`.

**Current effort: an independent "perfection re-audit" of `Microsoft::Devices`.**
On 2026-07-17 the user supplied a fresh, independent 92-task re-audit
(`audit_devices_2026-07-17.md`, merged verbatim into `plans/plan_devices.md` as
**"Section 16. Independent perfection re-audit backlog (2026-07-17)"**). This section
is the primary source of truth for current work and **intentionally reopens areas
older parts of `plans/plan_devices.md` describe as CLOSED** — an older CLOSED label is not
by itself a reason to skip re-examining a Section 16 finding.

**Explicit execution order given by the user:**
1. `DEVPERF-001` through `TEST2-001` (the full P0 set) — **ALL DONE.**
2. Then P1, then P2, then P3, no specified order within each tier — **P1 IN
   PROGRESS, see Section 2/8.**

**Mandatory rules (still in force for every remaining task):**
- Do not mark a task CLOSED merely because it compiles, a host-only mock test passes,
  it agrees with MonoGame, a comment claims a race is "unsupported," hardware-dependent
  behavior hasn't been tested, or the problematic path "seems unlikely."
- Do not create fake hardware evidence. Where Android/physical-sensor validation can't
  be performed in this container: implement everything that can be implemented, add
  the harness/test, document the exact device test procedure, and leave the
  hardware-validation portion explicitly OPEN — never claim it as done.
- Preserve the XNA/WP7 public compatibility surface; any non-XNA addition needs the
  established `NOXNA` policy and documentation.
- Do not weaken tests to make them pass. Do not silence sanitizer findings without
  fixing the underlying issue or rigorously proving it safe.
- Per-task workflow ends with: updating `plans/plan_devices.md`'s Section 16 entry (status,
  resolution/implementation summary, files changed, tests run, sanitizer result,
  remaining limitations, hardware evidence status) **and** a focused git commit.
- Keep `plans/plan_devices.md` updated continuously, and update this file periodically —
  especially before context grows large enough that another session must resume cold.

**Important labeling convention established this pass — read before closing anything:**
A task whose **acceptance criteria themselves name an empirical/dynamic result**
("fault injection leaves no leak", "TSan-clean under stress", "a fake service restart
re-probes successfully") must stay **OPEN** even once its code fix is implemented,
built, and reasoned through as correct — use the heading form
`### TASK-ID — Title — OPEN (implementation done; <what needs hardware/tooling>)`
and a `**Progress so far (not yet CLOSED — see Remaining limitations):**` body instead
of `**Resolution:**`. This is *stricter* than the earlier `ANDR2-004`/`005`/`006`
precedent, which correctly closed similar Android-only, host-uncompilable fixes —
the distinction is that those tasks' acceptance criteria were **logical/structural
claims fully provable by code inspection + compilation** ("no NDK function receives a
null looper", "Start returns the documented failure promptly" — true by construction
once the guard exists), not claims requiring a sanitizer or real hardware to actually
observe. Apply this distinction task-by-task: read the acceptance criteria literally
and ask "can this specific sentence be verified by reading the code, or does it name a
run that must actually happen?" `VIB2-003`, `VIB2-004`, and `ANDR2-002` (this pass) are
the first three examples of the "stays OPEN" case — read their `plans/plan_devices.md`
resolution notes as the template.

---

## 2. Current status (2026-07-18, P1 tractable backlog exhausted, moving into P2)

**All P0 tasks closed** (see prior checkpoints / `plans/plan_devices.md` for full detail).

**Precise Section 16 task count (2026-07-18, counted directly from `plans/plan_devices.md`,
not from memory — re-count with the script below before trusting this if picking up
much later, since it will go stale):**
- **92 real tasks total** in Section 16 (`### TASK-ID — ... — CLOSED/OPEN` headers;
  excludes the trailing "Perfection re-audit definition of done" non-task header).
- **27 CLOSED** — fully done.
- **24 OPEN but substantively investigated/progressed** (has its own "Progress so
  far" note — real implementation/tests/documentation done, left OPEN only because
  its acceptance criteria name a hardware/oracle/other-blocked-work result this
  container genuinely cannot produce).
- **41 OPEN, genuinely untouched** (21 at P1, 20 at P2) — no work done on these at
  all. Most are either hardware-gated (`COMP2-004/005`, `MOT2-009/010`, `VIB2-005`,
  `ANDR2-012/015`, `TEST2-006`) or large architecture tasks deliberately set aside,
  not quick continuations (`LIFE-007/010/011`, `ANDR2-011/014`, `MOT2-006`,
  `TEST2-004/005` — see Section 9's "Do not do yet" for why each was set aside) or
  blocked on other not-yet-done work (`DEVPERF-002/003` need a real WP7 reference
  manifest/harness this environment cannot produce). The P2 items among the 41 are
  the least-triaged group — worth a fresh look before assuming they're all
  hardware/architecture-blocked too, since only `VIB2-006`/`VIB2-007` have been
  looked at closely so far.

Re-count anytime with:
```bash
python3 - <<'PYEOF'
import re
with open('plans/plan_devices.md') as f:
    content = f.read()
idx = content.find('## 16. Independent perfection re-audit backlog')
blocks = re.split(r'\n(?=### )', content[idx:])
tasks = [b for b in blocks if b.startswith('### ') and 'definition of done' not in b]
closed = [t for t in tasks if re.search(r'^### .*— CLOSED', t.split('\n')[0])]
open_tasks = [t for t in tasks if re.search(r'^### .*— OPEN', t.split('\n')[0])]
touched = [t for t in open_tasks if 'Progress so far' in t]
untouched = [t for t in open_tasks if 'Progress so far' not in t]
print(f"Total: {len(tasks)}  CLOSED: {len(closed)}  OPEN-progressed: {len(touched)}  OPEN-untouched: {len(untouched)}")
PYEOF
```

**P1 tasks closed so far, in the order worked (all committed):**
1. `BASE2-007` (`aaf3dae8`) — counter-underflow clamping → loud `assert()`.
2. `VIB2-002` (`129fa2af`) — NaN/infinity handling for vibration intensity/motor
   magnitudes, at both `VibrateController` and `SdlHapticVibrateBackend` layers.
3. `VIB2-001` (`74ec2077`) — `IsSupported()` now also requires
   `SDL_HapticRumbleSupported(device)`.
4. `LIFE-008` (`856608a8`) — `Compass`/`Motion` constructors roll back
   `instanceCount_` on any post-reservation exception.
5. `ANDR2-004` (`0599ebe2`) — `AndroidSensorBridge`'s `RunExitGuard` destructor is now
   explicit `noexcept` + `try`/`catch`.
6. `ANDR2-005` (`a879f6ab`) — `ALooper_prepare()`'s return value is now null-checked.
7. `ANDR2-006` (`f7750da3`) — `ASensorEventQueue_getEvents()` negative returns now
   trigger persistent-failure shutdown after 5 consecutive errors;
   `disableSensor()`/`destroyEventQueue()` failures checked + logged.
8. `LIFE-006` (`de79d923`) — new `SensorBase<T>::DisposalTerminalStateGuard` guarantees
   `disposed_` publication even if the winning `Dispose()` caller's cleanup throws.
9. `COMP2-009` (`aaaa54e4`, docs-only) — closed by reference to `LIFE-005`.
10. `MOT2-002` (`107e3e0d`) — hardened `AndroidMotionMath.hpp` quaternion math against
    invalid/non-unit input; also fixed `AndroidMotionBackend`'s separately-published
    `RotationMatrix`, beyond the one function the task named.
11. `COMP2-002` (`7e366cf9`) — Compass analogue of `MOT2-002` for
    `AndroidCompassMath.hpp`'s `atan2()`-based formulas.
12. `VIB2-003` (`2d3abdf7`) — `SdlHapticVibrateBackend`'s `SDL_PlayHapticRumble`/
    `SDL_StopHapticEffects`/`SDL_StopHapticRumble`/`SDL_RunHapticEffect` return values
    are now checked (debug-only `SDL_Log()` diagnostics); a failed `Run()` after a
    successful `Create()` now destroys the just-uploaded effect immediately instead of
    leaving it allocated until the next call happens to reclaim it. **Left OPEN**
    (implementation done) — see the labeling convention above; acceptance criterion
    "fault injection leaves no leak" needs a real haptic device, never opened in this
    container.
13. `VIB2-004` (`2ca5aed6`) — `SdlHapticVibrateBackend` now detects a disconnected
    haptic device (`IsHapticDeviceStillConnected()` re-queries `SDL_GetHaptics()` and
    compares instance IDs — confirmed SDL3 has no haptic hotplug event and
    `SDL_GetHapticID()` only returns the ID captured at open time) and
    closes/discards the stale handle (`ReleaseHapticDeviceIfStale()`), called from
    every public entry point, so the existing `OpenFirstHapticDevice()` reopen path
    transparently retries on the next call. **Left OPEN** (implementation done) — same
    reasoning as `VIB2-003`; disconnect/reconnect needs real hardware.
14. `ANDR2-002` (`753e0631`) — `AndroidSensorBridge::IsAvailable()` called `Probe()`
    (plain, non-atomic `manager_`/`sensor_` pointers) with **no lock**, while `Start()`
    already called the same `Probe()` under `stateMutex_` — a genuine data race, now
    fixed by locking `IsAvailable()`'s call too. Also added `InvalidateProbeCache()`,
    called from `Run()` on a failed queue-creation/enable-sensor call or
    `ANDR2-006`'s own consecutive-`getEvents`-failure threshold, so a subsequent
    `Start()` re-probes from scratch instead of reusing possibly-dead cached handles
    forever. **Left OPEN** (implementation done) — "Concurrent ... TSan-clean" and "a
    fake service restart re-probes" both name empirical results this Android-only code
    (uncompilable on this host outside the NDK cross-compile) cannot produce here;
    genuinely needs real hardware/emulator or `TEST2-005`'s future fault-injection
    layer. "App lifecycle changes" from the required work deliberately left to
    `ANDR2-012`'s own separate scope, not silently dropped.
15. `SDLCORE-009` (`1ff35248`) — **CLOSED**, not left OPEN (see the labeling convention
    above for why this one differs from `VIB2-003`/`004`/`ANDR2-002`: its acceptance
    criteria describe purely in-process C++ exception handling, fully exercisable and
    exercised with real, passing assertions, not a hardware-dependent fact). Chose
    log-and-continue as `DispatchToInstances()`'s formal policy (documented in its own
    comment); the swallow itself was already correct, only silent. Split the
    `catch (...)` into a `std::exception&`-typed clause (extracts `.what()`) plus a
    fallback, both routed through a new `LogAndRecordDispatchException()`: debug-only
    `SDL_Log()` for interactive observability, plus new
    `dispatchExceptionCountForTesting_`/`lastDispatchExceptionMessageForTesting_` fields
    (guarded by the existing `mutex_`) exposed via new `Accelerometer`/`Gyroscope`
    static test hooks — genuine automated observability, not just "trust the log line."
    Extended two **pre-existing** tests with the new assertions rather than adding
    near-duplicates. Adds a new lock-acquisition site, so re-verified clean under
    `devices-tsan` (3 runs, 0 warnings) — the first task this pass to need that.
16. `SDLCORE-005` (`de37673d`) — the SDL-sensor analogue of `VIB2-004`: `sensor_`/
    `sensorId_` (`Detail::SdlSensorSubsystem<TSensor>`, shared by `Accelerometer` and
    `Gyroscope`) were cached by the first successful open and reused indefinitely.
    Confirmed SDL3 has no sensor-specific hotplug event either (same as haptics) — added
    `IsSensorConnected()` (re-queries `SDL_GetSensors()`, compares ids) and wired it into
    `OpenDefaultSensorLocked()`, closing/discarding a stale handle before the existing
    selection loop reruns. One fix covers both sensor classes (shared template).
    **Explicitly did not implement** the required work's mid-session live-disconnect
    bullet ("on removal, stop delivery, invalidate data, transition State... policy-driven
    reacquisition") — the SDL sensor path is entirely event-driven with no polling loop
    (unlike `AndroidSensorBridge::Run()`), so there's no natural trigger point for an
    already-running instance to notice a disconnect without a genuinely new
    architectural piece; flagged as a candidate for its own future task, not silently
    dropped. **Left OPEN** (implementation done) — same reasoning as `VIB2-003`/`004`/
    `ANDR2-002`. Re-verified clean under `devices-tsan` (new call site from `Start()`'s
    locked section).
17. `COMP2-001` (`91f6ff14`) — `AndroidCompassBackend` fuses two independent Android
    streams (rotation-vector heading + magnetic-field magnetometer/accuracy) purely by
    "last value from each," with no check they came from around the same moment — a
    silently-dead stream's frozen value could get fused with the other's fresh samples
    forever. Added `ComputeCompassMaxSampleSkew()`/`IsCompassSampleFresh()` (pure
    functions, `AndroidCompassMath.hpp`) and per-stream timestamps (from
    `AndroidSensorSample`'s own existing delivery time); `PublishReading()` now drops
    (skips) a stale pairing instead of fusing it — self-heals automatically once the
    stalled stream resumes. **Unlike most Android-only fixes this pass, the new
    primitives are pure/host-testable** — 9 new tests directly prove the threshold
    derivation and freshness boundary, satisfying "synthetic skew tests prove pairing
    boundaries" outright. **Left OPEN** (implementation done) — the end-to-end runtime
    behavior (`PublishReading()`'s actual wiring) still needs real hardware to observe,
    same reasoning as `VIB2-003`/`004`/`ANDR2-002`/`SDLCORE-005`, but this task's
    decision *logic* is more thoroughly tested than any of those four.
18. `MOT2-003` (`c03b86b2`) — **minor progress only, explicitly not a full fix.**
    `AndroidMotionBackend` already had a `MOTION-007` fixed 500ms freshness bound across
    its four fused sources (attitude/gravity/linear-acceleration/gyroscope) — this task's
    "Problem" wording criticizes that bound as too loose for fast motion, it does not
    describe an unbounded system. Added only the required work's narrowest, clearly
    isolable bullet: `droppedFusionFrameCountForTesting_`/
    `GetDroppedFusionFrameCountForTesting()`, exposing how often the existing drop
    already fires. **Deliberately did NOT implement** the harder two-thirds (bounded
    per-source sample queues, nearest/interpolated selection within a tight,
    *hardware-measured* skew) — genuinely comparable in scope to `LIFE-007`/`010`/`011`,
    and "measured" is a literal instruction requiring real device jitter data this
    container cannot produce. Both acceptance criteria describe the undone redesign, not
    the counter — this task's acceptance criteria remain **unmet**, not just
    hardware-unverified; the least-complete task touched this pass.
19. `MOT2-005` (`551da335`) — `AndroidMotionBackend::Start()` already picks
    `TYPE_ROTATION_VECTOR` (north-referenced) or falls back to
    `TYPE_GAME_ROTATION_VECTOR` (drift-prone) into `usingGameRotationVector_`, but
    nothing exposed which was in effect. Added
    `IMotionBackend::IsUsingNorthReferencedAttitudeSource()` and a new public
    `Motion::getIsAttitudeNorthReferencedProperty()` (NOXNA). **Found and fixed a
    latent data race while wiring this up**: `usingGameRotationVector_` was written by
    `Start()` with no lock at all — harmless before this task (nothing read it), a real
    race the instant a reader existed; now stored under `stateMutex_`.
    **Unlike most Android-only fixes this pass, fully host-testable** — the `Motion` →
    `IMotionBackend` delegation runs through the existing `FakeMotionBackend`, so 4 new
    tests directly cover the no-backend default, both fake-backend outcomes, and the
    post-dispose throw, with no Android dependency. **Left OPEN** (implementation done)
    — whether the real backend actually selects the fallback in the expected real
    circumstance, plus the required work's WP7-documentation-comparison and
    drift-measurement bullets, remain unverified/unattempted. Re-verified clean under
    `devices-tsan` (new host-buildable lock/interface change).
20. `ANDR2-009` (`d8b47682`) — `Run()`'s inner drain loop could, under continuous
    high-rate input, run indefinitely without returning to the outer loop, where
    `rateChangeRequested_` is checked. **Corrected the task's own "starve Stop" framing**
    while investigating: `stopRequested_` was already re-checked every inner iteration
    before this task — only rate-change responsiveness was actually unbounded. Added a
    64-event-per-batch cap (`MaxEventsPerDrainBatch`) plus
    `drainBatchLimitHitCountForTesting_`/`GetDrainBatchLimitHitCountForTesting()`, named
    deliberately — no sample is dropped or coalesced, only how many process before
    yielding is bounded. Deliberately did **not** implement "optionally coalesce to
    newest sample" (the required work's own explicitly-optional bullet — a real
    observable-behavior change judged out of scope). **Left OPEN** (implementation done)
    — confirming the cap fires under real sustained load needs real hardware or
    `TEST2-005`.
21. `ANDR2-010` (`fdcc37e8`) — both `ASensorEventQueue_setEventRate()` call sites in
    `Run()` (startup + live `SetSampleInterval()` updates) discarded their return value
    entirely — a rejected rate change was silently absorbed. Both now record the result
    into `lastSetEventRateSucceededForTesting_` (reset by every `Start()`, matching
    `ANDR2-001`'s own reset discipline — "version by run generation" without a separate
    counter), exposed via `GetLastSetEventRateSucceededForTesting()`. Also added
    `GetMinDelayMicrosecondsForTesting()` (`ASensor_getMinDelay()`, confirmed via the
    real NDK header to need no API-level guard for this project's minimum). "Continue
    delivery on nonfatal rejection" needed no change (already correct, confirmed by
    re-reading, not assumed). "Keep software throttling if required" **not attempted** —
    genuinely needs real hardware measurement to know if it's even necessary. **Left
    OPEN** (implementation done) — "effective cadence is measured on hardware" is, by
    its own wording, unsatisfiable without real hardware.
22. `BASE2-001` (`92a0b6b8`) — **found and fixed a real, previously undetected
    signed-integer-overflow bug**, not just a diagnostic addition: adding the overflow
    test coverage this task's own required work asks for immediately triggered UBSan in
    `SensorBase<T>::ShouldAcceptUpdateAt()` — comparing `(now - lastAcceptedUpdateTime_)
    >= interval` directly implicitly promotes both `std::chrono::duration`s to their
    finer common period, multiplying `interval`'s 100ns-tick count by 100; for
    `TimeSpan::MaxValue` (tick count near `INT64_MAX`) that overflows signed 64-bit
    arithmetic (`9223372036854775807 * 100...`). Fixed by explicitly `duration_cast`-ing
    the *elapsed* duration down to `interval`'s own coarser period first (always safe —
    an int64 100ns-tick count spans ~29,000 years). Added regression tests at both
    `MaxValue`/`MinValue` through the real comparison (previously only the plain setter
    was tested at these extremes). **Confirmed** the Problem statement's own claim
    ("Android silently clamps while SDL throttle treats negative as always-ready") is
    real — only `Accelerometer`/`Gyroscope` call `ShouldAcceptUpdateAt()` at all.
    **Deliberately did not** attempt unifying the two mechanisms (adding software
    throttling to `Compass`/`Motion`) — genuinely blocked on the not-yet-built behavioral
    oracle (`DEVPERF-002`/`003`; real WP7 throttle-equivalent behavior is unknown without
    it) and would likely break existing fake-backend tests that fire readings in
    immediate succession. **Left OPEN** — but for a *different* reason than most other
    entries this pass: blocked on other unstarted work (an oracle), not on hardware.
    Re-verified clean under `devices-tsan` (shared, concurrency-relevant method on the
    real `Accelerometer`/`Gyroscope` dispatch path).
23. `COMP2-008` (`9b4277b0`) — confirmed production behavior already never fabricates a
    declination-corrected `TrueHeading` (`AndroidCompassBackend.cpp` passes the identical
    `magneticHeadingDegrees_` value for both fields — the correct honest fallback).
    **Found a real public-API documentation gap**: `CompassReading::getTrueHeadingProperty()`'s
    Doxygen comment never disclosed this — a caller reading only the public doc could
    reasonably expect a real declination-corrected value. Fixed the doc comment.
    Deliberately did **not** stub a speculative "declination provider" extension point —
    `docs/location-future-plans/plan.md` (re-read, still accurate) already establishes any
    future location support belongs in a separate `System::Device::Location` namespace,
    not bolted onto `Microsoft::Devices::Sensors`. **Left OPEN**: doc fix is real and
    complete, but the declination-provider integration itself is correctly blocked on
    separately-scoped, out-of-scope location work, not merely unverified — a fourth
    distinct "why OPEN" flavor this pass (documentation-complete, feature-blocked).

**2026-07-18: independent re-verification of `audit_devices.md` (a *different*, older
audit than Section 16 — 6 findings `DEV-AUD-001` through `006`, all marked `CLOSED` on
2026-07-16, 4 commits `ce8153ed`..`a74f4d73`).** A user-supplied external review found
only 2 of 6 were genuinely fully resolved. Verified the review's own claims directly
(re-ran the exact test filter, got identical numbers — 420/416/4/0) before acting on it,
rather than trusting it blindly. See the (corrected) `project_devices_audit_remediation`
memory for the full per-finding breakdown. Commit `422ed4c4`:
- Fixed 2 real, concrete bugs: `Motion.hpp`'s `Calibrate` doc comment said "the compass
  component" (copy-paste leftover from `Compass.hpp`); `docs/devices-android.md` falsely
  claimed no `Motion` vector remap exists at all (it does, `MOTION-012`) and that no CI
  infrastructure exists anywhere (`devices-tests.yml` now exists, just unconfirmed green).
- Corrected 1 stale status-tracking gap: `VERIFY-001`'s recorded `358`-test count had
  gone stale exactly as its own entry warned — added a fresh, dated re-verification
  entry (`420`/`416`/`4`/`0`) rather than editing the historical number in place.
- Added 1 cross-reference amendment: `ANDROID-BRIDGE-005`'s "no platform-independent
  test seam is possible" conclusion is superseded by `ANDR2-014`'s later finding that a
  fake-NDK-adapter seam *is* achievable — noted so `CLOSED` isn't mistaken for "no
  remaining gap in this area."
- Re-examined 1 finding (`DEV-AUD-004`/`ANDROID-BRIDGE-006`) and **left it alone** — its
  own resolution note already honestly distinguishes "re-examination task done" from
  "underlying limitation resolved" (matches `Accelerometer`'s own identical, permanently
  accepted callback-destruction boundary) — don't reopen this one without a new, concrete
  reason, the reviewer's "still unsupported" framing doesn't mean it was mishandled.
- Confirmed 2 claims as accurate, no fix needed: `docs/hardware-qa-reports/` genuinely
  doesn't exist; "no physical Android device used" is already honestly stated.

24. `BASE2-002` (`4081dece`) — **found and fixed a second real bug this pass** using the
    same "investigate before assuming oracle-blocked" methodology `BASE2-001` proved
    valuable: every one of the four sensor classes dispatched a new reading via
    `setIsDataValidProperty(true)` then `setCurrentValueProperty(reading)` as two
    *independently*-locked calls — a concurrent reader could observe the window between
    them (`IsDataValid` already `true`, `CurrentValue` still the previous/default value).
    `Accelerometer`'s own dispatch had an especially wide window (69 lines between the
    two calls). Added `SensorBase<T>::SetCurrentValueAndMarkDataValid()` (one lock scope
    for both fields); all four classes now use it. Also removed a redundant
    `getIsDataValidProperty()` round-trip in `Accelerometer`/`Gyroscope` (checking a
    known-locally `bool` via a mutex-guarded getter). **Real regression proof, not just
    reasoning**: a writer/reader thread pair stress test that would have failed before
    this fix — 3 consecutive clean `devices-tsan` runs. **Left OPEN** — same reasoning
    as `BASE2-001`: the atomicity fix is real and complete, but testing each lifecycle
    transition (Stop, failed Start, restart, source/permission loss) against real WP7
    behavior remains blocked on `DEVPERF-002`/`003`.

25. `BASE2-003` (`01bb7b3b`) — documentation-only: added a class-level doc comment to
    `SensorState.hpp` recording, per enum value, the actual verified reachability
    (grepped every `state_ = SensorState::...` assignment across all four `.cpp` files
    directly, not assumed) — `NotSupported`/`Initializing`/`Ready`/`Disabled` are
    produced by all four sensor classes today; `NoData`/`NoPermissions` are produced by
    **none** of them. Deliberately documented as "currently never produced, unverified
    against real WP7 behavior" rather than "intentionally never produced" — flagged an
    unconfirmed hypothesis (Android's basic motion sensors don't require a runtime
    permission grant, which would make `NoPermissions` plausibly unreachable on this
    project's supported platforms) without claiming it as fact. **Left OPEN**: honest
    reachability documentation is complete, but confirming intent against real WP7/
    MonoGame behavior has no local oracle.
26. `BASE2-004` (`6484957f`) — **no source files changed**: investigated and confirmed
    this task's gap was *already* correctly resolved by a prior session's own
    oracle-verified work. `DEV-API-005` (2026-07-06) had already verified the exception
    type split against cited archived MSDN pages; `ErrorId`'s `0` default is correct
    since SDL3 has no numeric error codes; the embedded `SDL_GetError()` text in
    exception messages was judged intentional/acceptable, not a leak of internal detail.
    A genuine fourth "why OPEN"/"why CLOSED" pattern this pass: sometimes investigation
    finds nothing to fix because an earlier task already did the real work — "confirm
    existing correctness" is itself a valid, non-trivial contribution, not a null result.
27. `BASE2-005` (`04c38fca`) — **CLOSED**, not left OPEN: this task's problem statement
    read as if it needed an external WP7 oracle, but the two concrete gaps named in its
    acceptance criteria ("reentrant update", "handler list mutation during dispatch")
    were both closeable by direct source inspection. Confirmed by reading sharp-runtime's
    current `System::EventHandler<T>::Raise()` directly that it already snapshots
    `handlers_` before iterating and takes `const TEventArgs&` — no handler can corrupt
    another's args, and `Add()`/`Remove()` during dispatch only affects the *next*
    `Raise()`. Found and fixed one stale test (`RemovingAnotherNotYetInvokedHandlerDuring
    DispatchDoesNotThrow` described an older, already-fixed `Raise()` behavior and had
    deliberately weakened its own assertion to `(void)secondHandlerInvoked` — renamed,
    comment rewritten, tightened into real assertions). Added the one genuinely-untested
    scenario, `HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState`. "Throwing
    handler" and "dual events match reference order" acceptance criteria were already
    covered by pre-existing, still-passing tests, confirmed still current rather than
    re-authored. Test-file-only change; re-verified clean under both `devices-ubsan` and
    `devices-tsan` (312-test `*Accelerometer*:*Gyroscope*:*Compass*:*Motion*:*SensorBase*`
    filter, 308 passed, 4 hardware skips, 0 failures, no sanitizer report).

28. `DEVPERF-004` (`4cd9de18`) — new `docs/devices-event-contract.md`, the single
    normative statement of dispatch thread identity, ordering, handler-list mutation,
    reentrancy, destruction-during-dispatch, and exception semantics for all five
    Sensors events, explicitly separating WP7-inherited .NET multicast-delegate
    baseline from CNA-only policy decisions (required work's own second bullet).
    Extended `BASE2-005`'s Accelerometer-only handler-removal/reentrancy tests to
    `Gyroscope`/`Compass`/`Motion` (6 new tests total, no production source changes —
    every guarantee documented was already correctly implemented). **Found a real,
    concrete gap while tracing the actual call chain, not assumed**: `Compass`/`Motion`'s
    exception path (`AndroidSensorBridge::Run()`'s `callback_(sample)` `catch (...) { }`)
    is silent — no crash, but no logging or counter, unlike `Accelerometer`/`Gyroscope`'s
    `SDLCORE-009`-hardened path; the source comment claiming these paths are "identical"
    is now stale (accurate when written, superseded by `SDLCORE-009` afterward without
    being revisited). **Deliberately left unfixed**, named for `DEVPERF-005`
    ("structured native error/diagnostic channel... cover SDL and Android failure
    paths") to actually close — this is exactly why `DEVPERF-004` stays **OPEN**
    (documentation/decision/tests done; one real cross-backend policy-implementation
    gap remains, correctly scoped to a different task rather than fixed here).
29. `DEVPERF-005` (`c2c7956d`..`c9be8084`, 7 commits) — **CLOSED**. New
    `Detail::NativeDiagnosticRecord`/`Detail::NativeDiagnosticSink`
    (`Backend`/`Operation`/`NativeCode`/`NativeMessage`/`DeviceId`/`Timestamp`/
    `Severity` fields, exactly what this task's required work names), `Record()`
    `noexcept` and internally `try`/`catch`-wrapped so it can never itself become a new
    throw-across-a-C-callback hazard. Exposed via debug-build `SDL_Log()` (confirmed
    linked and working on Android too) plus `GetRecordCountForTesting()`/
    `GetLastRecordForTesting()`/`SetCallbackForTesting()`/`ResetForTesting()`. 9 new
    host-testable `NativeDiagnosticTests.cpp` tests (including an 8-thread concurrent
    stress test). Migrated all five diagnostic call sites named as follow-up work:
    `DEVPERF-004`'s `AndroidSensorBridge::Run()` swallow (split typed/fallback catch,
    mirroring `SDLCORE-009`), `SDLCORE-009`'s own `SdlSensorSubsystem` dispatch
    exception (added alongside the pre-existing per-subsystem counter, not instead of
    it — existing tests assert exact values there), `VIB2-003`'s 4 haptic `SDL_Log()`
    diagnostics (replaced), `VIB2-004`'s stale-haptic-release (new Info-severity
    record), `SDLCORE-005`'s stale-sensor-release (new Info-severity record),
    `ANDR2-006`'s 2 Android cleanup-failure diagnostics (replaced, `NativeCode`
    populated with the real negative return value). Then ran an independent fork
    sweep of the entire `Microsoft::Devices` tree for any remaining silent swallows —
    found and fixed 2 more (`SDL_InitHapticRumble`/`SDL_CreateHapticEffect`, the two
    calls immediately adjacent to already-migrated ones the initial pass missed);
    spot-checked (re-read source directly, not just trusted the sweep) every other
    candidate and confirmed each already converts to a state/error via existing
    mechanisms (`SignalStartOutcome`/`InvalidateProbeCache`/existing `bool` returns/
    `lastSetEventRateSucceededForTesting_`) — satisfying the acceptance criterion's
    explicit "or converted to a state/error" alternative. Every touched translation
    unit re-verified via NDK cross-compile; full precise filter clean under both
    `devices-ubsan` and `devices-tsan` after every commit. Real-hardware fault
    injection for the haptic/Android-only sites remains `VIB2-003`/`004`/`SDLCORE-005`/
    `ANDR2-006`'s **own** separately-tracked "needs real hardware" limitation, not
    reopened or claimed resolved by closing `DEVPERF-005` itself.
30. `SDLCORE-007` (`e1c9c7d2`, docs-only) — verified against the pinned SDL source:
    `SDL_SensorEvent::timestamp` is nanoseconds since SDL library init
    (`SDL_GetTicksNS()`), a genuine monotonic acquisition-time value, distinct from
    Android's boot-time `ASensorEvent::timestamp`. **Found a real, direct conflict**
    with `READINGS-003` (`docs/devices-api-coverage.md`, 2026-07-06's already-settled,
    cross-sensor-class-consistent "always wall-clock-at-dispatch, never raw
    monotonic" policy) — this task's required work asks for exactly the "unreliable,
    platform-specific... offset calculation" `READINGS-003` explicitly rejected,
    just calibrated rather than raw. **Deliberately not implemented**: doing it only
    for `Accelerometer`/`Gyroscope` (SDL-backed; `Compass`/`Motion` are Android-NDK,
    a different clock domain this task's SDL-specific wording never addresses) would
    silently break `READINGS-003`'s "applied identically everywhere" guarantee
    several existing `Compass`/`Motion` tests rely on; the accuracy gain over
    dispatch-time is likely small per `READINGS-003`'s own "dispatch happens
    promptly" reasoning; and the full clock-step-safe bridge this task's acceptance
    criteria demand is comparable in scope/risk to `LIFE-007`/`010`/`011`/`ANDR2-011`.
    Presented as a genuine product/architecture decision via `AskUserQuestion` rather
    than picked unilaterally — **decided 2026-07-18: leave deferred, `READINGS-003`
    stands as the supersedeing policy.** Settled, not a re-open candidate absent a
    new concrete reason (e.g. a real reported timestamp-accuracy problem) — see
    Section 9's "Do not do yet" below.
31. `SDLCORE-011` (`3e93860a`) — confirmed the problem statement directly:
    `VibrateController::getDefaultProperty()`'s function-local static singleton's
    destructor (via `SdlHapticVibrateBackend`) makes real `SDL_CloseHaptic()`/
    `SDL_QuitSubSystem()` calls with no guard against running after the
    application's own `SDL_Quit()`. New `Detail::DevicesShutdownCoordinator`
    (header-only atomic flag) — call `Shutdown()` before `SDL_Quit()` — wired into
    that destructor. **Read SDL's own source rather than assuming a uniform risk**:
    `SDL_CloseHaptic()` on a device `SDL_Quit()` already freed internally is a real
    heap-use-after-free (`CHECK_HAPTIC_MAGIC()` dereferences freed memory) —
    reasoned from source, not empirically reproduced (needs a real opened haptic
    device, unavailable here, same limitation `VIB2-003`/`004` carry).
    `SDL_QuitSubSystem()` after `SDL_Quit()`, by contrast, was checked and found
    **already safe** (refcount-gated no-op) — verified empirically via new
    `tools/devices/shutdown_ordering_harness.cpp` run under `cmake-build-devices-asan`
    both with and without the guard (`--skip-shutdown-call`), 0 ASan reports either
    way; an earlier draft of this note had prematurely claimed "confirmed
    reproducible" before actually running the harness both ways — caught and
    corrected before committing. New `DevicesShutdownCoordinatorTests.cpp` (4 tests)
    plus `DevicesShutdownOrderingTests.cpp` (spawns the harness via `posix_spawn`,
    mirroring `AudioMixerTests.cpp`'s "needs a fresh process" precedent).
    `SdlSensorSubsystem<TSensor>` checked and confirmed to have **no** destructor
    logic touching SDL at all — already safe by construction. **Left OPEN**: the one
    genuinely dangerous call site (`SDL_CloseHaptic` UAF) remains hardware-unverified;
    "stress exception exit and plugin/library unload" not attempted.
32. `PERF2-002` (`03eb0945`) — existing stress tests topped out at 50–400 iterations,
    far short of this task's own "at least 100k" threshold. Added one new
    100,000-cycle construct/probe/Start/Stop/Dispose test per class:
    `Accelerometer`/`Gyroscope` (real backend, unsupported here so each cycle is
    construct→probe→throw-on-`Start()`→`Dispose()`, still real SDL
    subsystem/enumeration interaction every cycle), `Compass`/`Motion` (fake backend,
    matching every other host-runnable test for those two classes), and
    `VibrateController` (the **real** `SdlHapticVibrateBackend`, not a fake — adapted
    to 100k probe/`Start`/`Stop` cycles against its one process-lifetime singleton,
    since it has no per-cycle construct/`Dispose`). New shared
    `tests/Microsoft/Devices/Detail/ProcSelfResourceCounters.hpp` (Linux-only
    `/proc/self/fd`/`/proc/self/status` counters) — every test asserts an *exact*
    return to baseline, not a loose tolerance. All 5 clean under `devices-ubsan`/
    `-tsan`/`-asan` (0 FD/thread growth, 0 sanitizer reports). **Left OPEN**: this
    task's own acceptance criteria explicitly name `LSan` — re-confirmed in this pass
    (not just cited from an earlier finding) that `ASAN_OPTIONS=detect_leaks=1`
    produces zero `LeakSanitizer` output against these tests, consistent with `LSan`
    remaining non-functional in this container (needs `ptrace`, unavailable here) —
    genuinely blocked on the container, not on unfinished implementation.
33. `TEST2-002` (`cd4c4c73`, docs-only) — one consolidated, dated sweep instead of
    relying on this session's own ad hoc per-task verification: `--clean-first`
    rebuild of `cmake-build-devices-ubsan`/`-tsan`/`-asan`, then the exact precise
    Devices filter used throughout this pass (already includes every "lifecycle fuzz
    test" this task names — `PERF2-002`'s 100k-cycle tests, `TEST2-001`'s stress
    tests — confirmed by grep, no separately-named fuzz suite exists elsewhere).
    UBSan/TSan (3 runs)/ASan all clean, 0 reports, 0 failures, 357/353 every time.
    `LSan` explicitly forced on (`ASAN_OPTIONS=detect_leaks=1`) — zero output of any
    kind, **re-confirming** (independently of `PERF2-002`'s own check) it's
    non-functional in this container. Zero pre-existing/unrelated findings encountered
    with this precise filter (the three already-tracked out-of-scope ones — `Vector3`
    UBSan, sharp-runtime `TimeSpan::copy_count` TSan, `NetworkSession.cpp` ASan — all
    live outside these suites). Dependency revisions + full logs recorded (logs kept
    in this session's scratchpad, not committed — matches this project's existing
    convention of recording exact counts/commands/revisions in `plans/plan_devices.md`
    itself rather than checking in raw build/run output). **Left OPEN**: only the
    `LSan` half of "`ASan/LSan`" is unachievable here; everything else fully delivered.
34. `COMP2-003` (`77f8e0ec`) — the flat/upright heading formulas themselves were
    already carefully derived in an earlier task (`COMPASS-009`); this task's own
    three specific gaps were investigated and closed. **"Display orientation" —
    found a real, citation-backed answer, not guessed**: traced
    `docs/devices-api-coverage.md`'s existing `MagnetometerReading` citation (an
    archived MSDN Magazine article specifically about the WP7 Compass) confirming
    real WP7 sensor readings "are the same whether... running in portrait or
    landscape mode" — `AndroidCompassMath.hpp` correctly has **no** landscape remap,
    now documented as confirmed-correct rather than silently absent. **"Compare with
    Android reference APIs"**: new `IndependentReferenceCrossCheckTests` reconstructs
    Android's own documented quaternion→matrix→azimuth algorithm from scratch (all
    nine matrix elements) and cross-checks against the production formulas across 6
    quaternions — a regression-proof comparison, not a one-time claim.
    **"Cardinal headings"**: completed 180°/270° coverage for both flat and upright
    modes (previously incomplete in both) — hand-derived quaternions were
    numerically cross-checked with an independent script before committing, which
    **caught and fixed a real arithmetic error** in the first draft of the
    upright-mode 180°/270° values. No production `.cpp` changed — header doc-comment
    + tests only. Full precise filter (363 tests) clean under `devices-ubsan` (359
    passed, 4 hardware skips, 0 failures); `AndroidCompassBackend.cpp` re-verified
    via NDK cross-compile. **Left OPEN**: this task's own problem statement names
    "hardware-unverified" as the starting state, which remains true.
35. `MOT2-001` (`8d949de3`) — confirmed Android's rotation-vector quaternion and XNA's
    `Quaternion`/`Matrix` use the **identical** right-handed Hamilton formula
    (`Matrix.hpp` self-documents as right-handed; `Quaternion::CreateFromAxisAngle()`'s
    actual implementation has no sign flip) — **verified by direct computation**, new
    `AndroidMotionMathTests.DirectQuaternionPlusNinetyDegreeYawRotatesUnitXToUnitYMatchingRightHandedConvention`
    builds a raw quaternion the same way a real Android sample would (independent of
    the existing round-trip tests) and confirms `+X`→`+Y` for a +90° yaw — **no
    handedness correction needed**. Confirmed no display-orientation remap needed for
    `Motion.Attitude` either, reusing `COMP2-003`/`ACCEL-008`'s same citation.
    **Found and verified (direct NumPy computation) a significant, previously-uncaught
    finding** while checking whether Attitude should get a landscape remap matching
    `MOTION-012`'s remap of `Gravity`/`DeviceAcceleration`/`DeviceRotationRate`:
    `ConvertAndroidPortraitToXnaLandscape()` is a **reflection** (determinant `-1`) for
    both rotation states, not a proper rotation (determinant `+1`) — it cannot
    represent an actual 90°/270° physical device rotation, and **no quaternion can
    represent a reflection at all**, so a "consistency" remap couldn't even be validly
    constructed. **Deliberately not fixed**: `ConvertAndroidPortraitToXnaLandscape()`
    is already-shipped, already-tested, `ACCEL-008`'s own maintainer-made decision
    (2026-07-07) — `MOT2-001` has no mandate to unilaterally revisit it. Cross-referenced
    from both `MOT2-001`'s own resolution and a new post-closure note added to
    `ACCEL-008`'s entry, discoverable from either direction — **flag this for whoever
    next touches `ACCEL-004`/`ACCEL-008`/`MOTION-012`, do not assume it was already
    checked just because those tasks are closed.** No production `.cpp` changed. Full
    precise filter (364 tests) clean under `devices-ubsan` (360 passed, 4 hardware
    skips, 0 failures); `AndroidMotionBackend.cpp` re-verified via NDK cross-compile.
    **Left OPEN**: axis correspondence itself remains hardware-unverified.
36. `TEST2-010` (`4411533b`) — the existing `cna_strict_xna_api_check` only ever proved
    the *positive* direction; "a deliberately leaked extension fails every check" had
    zero coverage. New `tools/devices/StrictXnaApiSurfaceLeakCheck.cpp` deliberately
    calls a `NOXNA` member under the same strict-mode flags, wired as a new
    `EXCLUDE_FROM_ALL` CMake target plus a `WILL_FAIL TRUE` ctest that verifies the
    build genuinely fails — confirmed end-to-end through the real CMake/ctest pipeline
    (not just a manual invocation). Ran **both** checks independently across **every**
    toolchain available in this environment: `GCC` 14.2.0, host `Clang` 19.1.7 (never
    previously exercised against this codebase this session), and Android NDK `Clang`
    (via direct compile-only invocation — a full Clang project reconfigure was
    unnecessary, and `cmake-build-android` only supports single-TU compiles anyway).
    All three: positive compiles clean, negative fails with **genuinely different**
    diagnostic formats (GCC's `cc1plus: some warnings being treated as errors` vs.
    Clang's `[-Werror,-Wdeprecated-declarations]`) — satisfies "without relying on one
    warning spelling" by construction, since `WILL_FAIL` only checks the exit code,
    never the message text. Full precise filter (364 tests) clean under
    `devices-ubsan` after the change. **Left OPEN**: `MSVC` remains genuinely
    unavailable — this is a Linux container with no Windows/MSVC toolchain at all, a
    pre-existing environment-wide limitation, not a gap in effort.
37. `PERF2-001` (`b853a6dc`) — no repeatable benchmark of any kind existed for
    `Microsoft::Devices` before this task. New `tools/devices/devices_microbenchmark.cpp`
    (standalone `cna_devices_microbenchmark` target): 10 benchmarks covering every one
    of this task's own named categories — probe, single-instance dispatch, **event
    fanout at N=1/5/10** (reusing `AccelerometerTests.cpp`'s own
    `RegisterStartedInstanceForTesting()`/`DispatchToInstancesForTesting()` hooks),
    the throttled-reject path (isolated from full dispatch), a real `Start()`/`Stop()`
    throw/catch cycle, and `Compass`/`Motion`'s pure fusion math. `p50`/`p95`/`p99`
    microsecond latencies, JSON Lines output. New
    `tools/devices/compare_devices_microbenchmark.py` flags `p95` regressions against
    a committed baseline (`docs/devices-benchmark-baseline.jsonl`, this host/container,
    2026-07-18) — **added an absolute-microsecond floor after empirically catching the
    tool's own first false positive**: two consecutive runs of the *same unmodified
    binary* produced a spurious 53% relative delta on a ~0.4µs benchmark (pure noise)
    before the floor was added — verified the fix with a clean re-run plus a
    correctly-flagged artificial 3x-regression injection, not just asserted to work.
    Full precise filter (364 tests) clean under `devices-ubsan`; both `StrictXnaApi*`
    ctests (`TEST2-010`) still pass. **Left OPEN**: allocations/lock-time are not
    separately instrumented (needs an allocator hook or modifying already-hardened
    locking code, out of mandate); actual CI wiring to run this automatically and fail
    on a regression is a separate, not-yet-done follow-up.
38. `VIB2-006` (`2165903d`, P2 — first P2 item picked up this pass, remaining P1
    backlog is now hardware-gated or large-architecture-only) — "intensity zero" was
    already decided by an earlier task (`DEVICES-0030`), but the only existing test
    only checked "doesn't throw," not what actually happens. New
    `StartWithIntensityZeroForwardsAsAnActiveZeroStrengthStartNotAnImplicitStop` uses
    `FakeVibrateBackend` to confirm it genuinely forwards as an active `Start()` call,
    not an implicit `Stop()` — strengthened the doc comment with a real SDL citation
    (`SDL_PlayHapticRumble()`'s own contract: "a 0-1 float value," `0` explicitly
    valid, confirmed against `SDL_haptic.c`'s actual clamp-and-run implementation).
    New `StartWhileAlreadyActiveForwardsAsANewIndependentStartCall` (confirmed against
    SDL's own implementation: an already-playing effect gets updated and restarted,
    never rejected) and `StopWhenIdleForwardsToBackendWithoutThrowing` — neither
    scenario had any coverage before. Confirmed by reading `SdlHapticVibrateBackend.cpp`
    directly that simple/left-right mutual exclusion is **already implemented**, not
    missing — existing tests exercise it against the real backend but, like every
    other real-backend test in this file, can only prove "does not throw" without real
    hardware. No production `.cpp` changed. Full precise filter (367 tests) clean
    under `devices-ubsan`. **Left OPEN**: real SDL-level mode-switch correctness and
    leak-freedom remain hardware-unverified.

**Emerging pattern to remember:** `BASE2-001`/`002`/`005` all looked, at first glance,
like tasks fully blocked on the not-yet-built behavioral oracle — but each had a
concrete, oracle-*independent* bug or gap hiding in its own problem statement, found
only by actually tracing the code instead of deferring on sight (two real bugs, one
stale test). `BASE2-003`/`004` are the counter-cases worth remembering too:
investigation doesn't always find a new bug — sometimes it correctly confirms existing
behavior is already right (`BASE2-004`) or produces honest, non-claim-inflating
documentation of what's verified vs. not (`BASE2-003`). All five `BASE2-*` tasks this
pass are now closed or progressed as far as they can go without an external oracle or
hardware. Apply the same "investigate before deferring" discipline to every remaining
P1 task before concluding there's nothing actionable.

**Pattern across `ANDR2-002`/`004`/`005`/`006`:** all inside `#ifdef __ANDROID__` code
with **zero host-side test coverage possible** — verified instead via a real Android
NDK cross-compile of the exact translation unit each time (`cmake-build-android`,
`arm64-v8a`, API 24). `ANDR2-009`/`010` are a partial exception: `AndroidSensorBridge.cpp`
is *not* itself `#ifdef __ANDROID__`-gated at the file level (each method has its own
inline `#ifdef __ANDROID__ ... #else ... #endif`, "inert everywhere" by design), so their
new getters' plumbing and non-Android defaults (`0`/`true`) are host-tested in
`AndroidSensorBridgeTests.cpp` — but the actual `Run()`-internal logic those getters
report on is still Android-only and NDK-cross-compile-verified only, same as the others.
The **full** `CNA` library Android cross-compile remains blocked by a pre-existing,
unrelated `sharp-runtime` failure (`RandomNumberGenerator.cpp`'s `::getrandom()` call
missing for this NDK/API combination) — only single-translation-unit compiles work.
`ANDR2-006`'s `liblog.so` link dependency has still **not** been confirmed to actually
resolve at link time.

**Build:** `cmake-build-devices-ubsan`, `cmake-build-devices-tsan`, and
`cmake-build-android` all still build clean (the last for individual translation units
only, per above).

**Tests:** Devices/Sensors filtered suite — **329 tests** (precise filter, used from
`VIB2-003` onward, now including `AndroidSensorBridgeTests.*` since `ANDR2-009`:
`AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:SensorBaseTests.*:
SensorSubsystemOwnershipTests.*:VibrateControllerTests.*:AndroidMotionMathTests.*:
AndroidCompassMathTests.*:AndroidSensorBridgeTests.*`) — **325 passed, 4 skipped**
(hardware-only, unchanged), 0 failures across every task above (re-confirmed after
`BASE2-005`'s new/renamed tests). `MOT2-003` added no tests (Android-only, no
pure-logic component to test the way `COMP2-001`/`MOT2-005`/`ANDR2-009`/`ANDR2-010`/
`BASE2-001`'s host-testable pieces had). `BASE2-003`/`004` added no tests (docs-only /
no-change tasks respectively). Note: a broader, unscoped `*Devices*:*Sensor*:...`
filter also incidentally matches `AccelerometerReadingTests`/`*EventArgsTests` (plain
data-holder tests), one of which (`GetHashCodeConsistency`) trips a **pre-existing,
unrelated** UBSan finding in `Vector3::GetHashCode()` (signed-int overflow in
hash-combining) — not touched by any task this pass, out of scope for Devices work, not
silenced, just avoid the broad filter and use the precise one above (or expect and
ignore that one specific failure if using a broader filter for some other reason).

**Sanitizers:** `devices-ubsan` clean on every P1 change this pass — **and directly
caught a real bug this pass** (`BASE2-001`'s `ShouldAcceptUpdateAt()` overflow, see
Section 2's own entry — this is exactly the kind of finding the mandatory "verify via
sanitizer, don't just reason about it" rule exists for). `devices-tsan` was NOT re-run
for `VIB2-003`/`004`/`ANDR2-002`/`COMP2-001`/`MOT2-003`/`ANDR2-009`/`010`/`BASE2-003`/
`004` (none add new locking/concurrency structure beyond what already existed, or the
new state is a worker-thread-only-written atomic with no new lock, or no source changed
at all) but WAS re-run for `SDLCORE-009`, `SDLCORE-005`, `MOT2-005`, `BASE2-001`,
`BASE2-002`, and `BASE2-005` (a new lock-acquisition site, a meaningfully-changed
comparison inside an existing one, or new dispatch/reentrancy-relevant test coverage,
each on a real dispatch path) — 3 consecutive clean runs each (`BASE2-005`: 1 full-suite
run, 0 `WARNING: ThreadSanitizer` occurrences either way). Re-run TSan if a future P1
task touches concurrent *host-buildable* lock-based logic.

**2026-07-18: CPU thermal pacing rule restated by the user, mid-`BASE2-005` work** — old
threshold language cancelled; current, standing rule: pause starting new work only at
**≥85°C**, resume once back at **≤75°C**; always finish already-started work regardless
of temperature. (This matches, and was previously captured in, the
`feedback_cpu_thermal_pacing` memory record — the restatement corrected this session's
own over-conservative behavior of throttling builds to `-j2` in the 75-85°C band, which
was never required by the actual standing rule.)

---

## 3. Recent changes — see Section 2's numbered list plus prior checkpoints (git
history / `plans/plan_devices.md`) for full technical detail on each. Not duplicated here.

---

## 4. Current blocker / main problem

**No blocker for continuing P1.** Same flagged, out-of-scope items as prior
checkpoints (unchanged):
1. Pre-existing `Net`/`NetworkSession.cpp` UBSan finding + full-suite segfault.
2. Pre-existing `sharp-runtime` Android cross-compile failure
   (`RandomNumberGenerator.cpp`'s `::getrandom()`) — blocks confirming `ANDR2-006`'s
   `liblog.so` link dependency resolves, and blocks any real TSan/ASan Android run.
3. Pre-existing, unrelated UBSan finding in `Vector3::GetHashCode()` (see Section 2's
   Tests note) — only surfaces via an overly-broad test filter; not a Devices issue.

---

## 5. Known bugs and limitations

- Everything from prior checkpoints still applies (LIFE-* concurrency boundary,
  ANDR2-001/003 host-untestability, Net/sharp-runtime gaps, VIB2-001/LIFE-008's
  never-actually-reached-on-this-host new code paths).
- **New this pass:** `VIB2-003`'s `SDL_RunHapticEffect()`-failure cleanup path,
  `VIB2-004`'s and `SDLCORE-005`'s disconnect/reconnect detection, `ANDR2-002`'s
  lock/invalidation fixes, and `COMP2-001`'s fusion-freshness check are all implemented
  and reasoned-through-correct (and, for `SDLCORE-005`, TSan-clean; for `COMP2-001`, the
  underlying decision logic is directly unit-tested) but **never actually exercised
  end-to-end on real hardware** — no haptic device, no real SDL sensor, and no Android
  hardware/emulator exist in this container. Each has a documented hardware validation
  procedure in `docs/devices-hardware-checklist.md` (Sections 2a, 4a, 4b, 6a, 7a
  respectively) and is explicitly left **OPEN** in `plans/plan_devices.md` (see Section 1's
  labeling convention). `SDLCORE-005` additionally leaves its required work's
  mid-session live-disconnect bullet entirely unimplemented (see Section 2's own entry)
  — a real architectural gap, not just an untested one.
- `SDLCORE-009` (in contrast) **is fully closed** — its acceptance criteria describe
  purely in-process exception handling, exercised with real, passing, TSan-clean tests.
- `MOT2-003` is the **least complete** task touched this pass — only its narrowest
  "expose counters" sub-bullet was implemented; the harder redesign (bounded queues,
  interpolation, a hardware-measured tight skew) is genuinely unimplemented, comparable
  in scope to `LIFE-007`/`010`/`011`, not merely untested. Its acceptance criteria are
  **unmet**, not just hardware-unverified — do not mistake this for a near-complete fix.
- `ANDR2-009`'s investigation found the task's own "starve Stop" framing was only
  partially accurate — `stopRequested_` was already checked every inner-loop iteration;
  only rate-change responsiveness was actually unbounded. Worth re-checking the code
  directly (not just this note) before assuming either framing without verification.
- `BASE2-001` is left OPEN for a genuinely different reason than the others: its
  overflow-bug fix is real and complete, but its core "unify TimeBetweenUpdates
  semantics across all four sensor backends" ask is blocked on `DEVPERF-002`/`003` (the
  not-yet-built behavioral oracle), not on hardware — don't conflate this with the
  hardware-validation reason most other `OPEN` entries this pass carry.
- `ANDR2-011` ("consolidate Android sensor bridges onto a shared looper") was scanned
  and judged a major architecture redesign (up to 6 worker threads per `Motion`
  instance → one shared looper) — comparable to `LIFE-007`/`010`/`011`, not attempted.
- 28+ more Section 16 tasks remain OPEN across P1/P2/P3 (`plans/plan_devices.md` is the
  actual source of truth — this file only tracks what's been *closed or progressed*).

---

## 6. Architecture notes

No new architectural pieces this pass beyond the P0 phase's additions. Localized
changes only:
- `SdlHapticVibrateBackend.cpp`: every real SDL haptic call's return value is now
  checked (debug-only `SDL_Log()` diagnostics); new `IsHapticDeviceStillConnected()`
  (anonymous namespace) and `ReleaseHapticDeviceIfStale()` (private member), called
  from `Start()`/`Stop()`/`StartLeftRight()`/`AcquireHapticDeviceForProbe()`.
- `AndroidSensorBridge.cpp`: `IsAvailable()`'s `Probe()` call now locked under
  `stateMutex_` (matching `Start()`'s pre-existing discipline); new
  `Impl::InvalidateProbeCache()`, called from `Run()`'s three native-failure sites.
  `Run()`'s inner drain loop now caps at `MaxEventsPerDrainBatch` (64) events per outer
  pass; new `drainBatchLimitHitCountForTesting_`/`lastSetEventRateSucceededForTesting_`
  (both reset by `Start()`); new public `GetDrainBatchLimitHitCountForTesting()`/
  `GetLastSetEventRateSucceededForTesting()`/`GetMinDelayMicrosecondsForTesting()`
  (the last wraps the NDK's own `ASensor_getMinDelay()`).
- `SdlSensorSubsystem.hpp`: `DispatchToInstances()`'s single silent `catch (...)` split
  into a `std::exception&`-typed clause + fallback, both routed through new
  `LogAndRecordDispatchException()`; new `dispatchExceptionCountForTesting_`/
  `lastDispatchExceptionMessageForTesting_` fields. New `IsSensorConnected()` (static),
  wired into `OpenDefaultSensorLocked()`'s existing cache-reuse fast path.
  `Accelerometer`/`Gyroscope` each gained 3 new `NOXNA` static test hooks forwarding to
  these (`GetDispatchExceptionCountForTesting`/`GetLastDispatchExceptionMessageForTesting`/
  `IsSensorConnectedForTesting`).
- `AndroidCompassMath.hpp`: new pure `ComputeCompassMaxSampleSkew()`/
  `IsCompassSampleFresh()`; `AndroidCompassBackend` gained two per-stream
  `System::DateTimeOffset` timestamps and a `maxSampleSkew_`, checked in
  `PublishReading()` before fusing.
- `AndroidMotionBackend.hpp`/`.cpp`: new `droppedFusionFrameCountForTesting_`/
  `GetDroppedFusionFrameCountForTesting()`, incremented in the pre-existing
  `MOTION-007` drop branch; new `IMotionBackend::IsUsingNorthReferencedAttitudeSource()`
  (also implemented by `AndroidMotionBackend`) plus `Motion::getIsAttitudeNorthReferencedProperty()`
  (`NOXNA`); `usingGameRotationVector_` moved under `stateMutex_` (was an unguarded
  write, harmless until this task added a reader).
- `SensorBase.hpp`: `ShouldAcceptUpdateAt()`'s duration comparison rewritten to
  `duration_cast` the elapsed time down to `interval`'s own period before comparing,
  fixing a real signed-integer-overflow at `TimeSpan::MaxValue` (found by UBSan, see
  `BASE2-001`'s own entry above) — affects the real `Accelerometer`/`Gyroscope` dispatch
  path, not just a test-only surface.

---

## 7. Useful commands

```bash
# Build + test (precise filter — avoids the pre-existing Vector3 UBSan finding, see
# Section 2/4):
cmake --build cmake-build-devices-ubsan --target CnaTests -j4
UBSAN_OPTIONS=halt_on_error=1 ./cmake-build-devices-ubsan/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:SensorBaseTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*:AndroidMotionMathTests.*:AndroidCompassMathTests.*:AndroidSensorBridgeTests.*"

# TSan (re-run if a P1 task touches concurrent *host-buildable* logic; repeat 3-4x):
cmake --build cmake-build-devices-tsan --target CnaTests -j3
TSAN_OPTIONS="halt_on_error=0" ./cmake-build-devices-tsan/CnaTests --gtest_filter="..."
# ALWAYS grep the full log for "WARNING: ThreadSanitizer" -- exit code alone (66) is
# the fastest signal something fired; a clean gtest summary does not mean TSan found
# nothing.

# Android cross-compile (single translation unit -- full CNA link still blocked):
cd cmake-build-android && ninja CMakeFiles/CNA.dir/<path-to-file>.cpp.o
# (cmake-build-android already configured; re-run cmake -S . -B cmake-build-android
# ... only if the build dir is missing/stale)

# Thermal check (this container's Tctl runs high independent of this session's own
# builds -- pace with -j3-6 if >75C, don't wait for it to drop below 70C, it may not):
sensors | grep -i tctl
```

---

## 8. Next smallest task

Continue the P1 backlog. Not yet triaged/started, roughly in the order encountered
scanning `plans/plan_devices.md` Section 16 (no mandated order within P1 — pick by
tractability):

- **`LIFE-007`/`010`/`011`, `ANDR2-011` — deliberately set aside, not merely
  unstarted.** Large architecture tasks; `LIFE-011` specifically has a **real design
  tension** already found (see prior checkpoint / its own `plans/plan_devices.md` notes) — do
  not attempt a naive symmetric `Stop()` wait, it reintroduces `TEST2-001`'s fixed
  deadlock. `ANDR2-011` ("consolidate Android sensor bridges onto a shared looper") was
  scanned and judged comparably large (up to 6 worker threads per `Motion` instance →
  one shared looper/thread — a genuine architecture redesign, not a quick fix).
- `DEVPERF-002`/`003` — `004`/`005` are both now **CLOSED** (see Section 2): `004`
  produced the normative event contract doc, `005` built the shared
  `NativeDiagnosticSink`, closed the one concrete gap `004` found, migrated all five
  named follow-up call sites (`SDLCORE-009`/`VIB2-003`/`004`/`SDLCORE-005`/`ANDR2-006`),
  and closed 2 more an independent sweep found. `DEVPERF-002`/`003` (the
  API/behavioral oracle) remain the only open `DEVPERF-*` items and a confirmed
  blocker for `BASE2-001`'s remaining scope (see Section 5) — picking these up
  unblocks more than just their own tasks, but they need an actual WP7 reference
  (archived MSDN citations proved workable for narrow doc questions, e.g. `DEV-API-005`,
  but this task wants a machine-readable manifest/full behavioral harness, a much
  bigger ask — scope this carefully before starting, it may turn out to be
  irreducibly hardware/SDK-blocked rather than just effort-blocked).
  `SDLCORE-007` and `SDLCORE-011` are both now investigated (see Section 2's entries
  30/31): `007` is deliberately left for explicit human input (see Section 9's "Do
  not do yet" below, do not pick a side unilaterally); `011` has its core
  implementation done and stays OPEN only because the one genuinely dangerous call
  site (`SDL_CloseHaptic` UAF) needs real hardware to verify under ASan.
- `ANDR2-007`/`012`/`014`/`015` — remaining Android-only items (`ANDR2-009`/`010` done
  this pass, `011` deliberately deferred per above; `007` is the same design-heavy
  "calibrated boot/monotonic-to-UTC offset" concern as `SDLCORE-007` below, for the
  Android-native path specifically; `014`/`015` explicitly want fuzzing/instrumented
  hardware runs; `ANDR2-012` is the right home for the "app lifecycle
  changes"/mid-session-recovery scope both `ANDR2-002` and `SDLCORE-005` deliberately
  deferred this pass).
- `COMP2-003`/`004`/`005` — remaining Compass items (`COMP2-001`/`008` done this pass;
  `004`/`005` need physical devices).
- `MOT2-001`/`006`/`008`/`009`/`010` — remaining Motion items (`MOT2-003`/`005` done
  this pass, though `MOT2-003` only minimally — its core redesign is still fully open
  and would be a substantial task if picked up properly, not a quick follow-up.
  `MOT2-006` was investigated: its "handle a source disappearing after Start without
  continuing stale output" and "define recovery/restart and state transitions" bullets
  are genuinely unimplemented today — `Motion.state_` never changes in response to a
  mid-session backend degradation, confirmed by reading `Motion.cpp` — comparable in
  scope to `LIFE-007`/`010`, not attempted this pass. `MOT2-010` needs physical
  hardware.).
- All of `BASE2-001`–`005` are now done this pass (see Section 2) — `001`/`002` found
  and fixed real oracle-independent bugs, `003` documented honest reachability, `004`
  confirmed prior work already correct, `005` closed outright (test-coverage gap, no
  oracle actually needed). `BASE2-006`/`007` remain: `007` is already `CLOSED` (see its
  own `plans/plan_devices.md` entry, done in an earlier pass); `BASE2-006` (float/NaN/hash
  value-semantics audit) has not been started this pass.
- `VIB2-005`–`007` — remaining Vibrate items (`005` needs a direct-backend Android
  validation; `006`/`007` are host-testable design/behavior questions).
- `PERF2-001`/`003`, `TEST2-002`/`004`–`006`/`010` — remaining P1 perf/test-infra items
  (`PERF2-002` done this pass, see Section 2). A dedicated survey (2026-07-18) rated
  these: `TEST2-002` (consolidated clean-checkout sanitizer sweep across the exact
  Devices filter + lifecycle tests, zero-unexplained-reports) is **tractable-now,
  lowest risk** — this session already does exactly this kind of verification
  ad hoc after every task, just never as one consolidated, explicitly documented
  pass; recommended next. `TEST2-004` (deterministic interleaving hooks/barriers
  replacing sleep-based stress) is real, host-tractable engineering scope.
  `PERF2-001` (microbenchmark suite with CI baseline storage) is tractable but
  medium-sized. `PERF2-003` (8–24 hour soak) cannot run in one session — a short,
  honestly-labeled smoke-test substitute is the realistic option, not the literal
  ask. `TEST2-006` needs 3+ real Android devices. `TEST2-010` (multi-compiler
  strict-XNA check) is tractable for GCC/Clang/NDK-Clang (all available here) but
  not MSVC (this is a Linux host) — partial coverage only.
  `TEST2-005` ("Build a native fault-injection layer") is referenced by four
  closed-but-OPEN tasks (`VIB2-003`/`004`, `ANDR2-002`, `SDLCORE-005`) as the thing that
  would let their acceptance criteria actually be verified — highest cross-task value,
  but "abstract every native call" is large; a scoped subset (e.g. just the SDL
  haptic calls) is more realistic for one pass than the full thing.
- `MOT2-001` — `COMP2-003` is done this pass (see Section 2): the closely-related
  Android rotation-vector→XNA attitude transform derivation for `Motion` is the
  natural next pick, same tractable shape and shares the same underlying quaternion
  math/Android sensor documentation just investigated for Compass. Also directly
  relevant: `docs/devices-native-backend-design.md`'s own note that `Motion.Attitude`'s
  `ConvertRotationVectorToXnaQuaternion()` mapping is a "direct, unremapped
  passthrough" and `MOTION-002`'s own still-open question — read that context first,
  it may already answer part of what `MOT2-001` asks. `COMP2-004`/`005`/`MOT2-009`/`010`
  need physical devices/measurements. `MOT2-006` is genuinely unimplemented
  state-machine work, comparable in scope to `LIFE-007`/`010` — do not pick up as a
  quick continuation.
  `MOT2-008` (canonical timestamp semantics) likely intersects `READINGS-003`/
  `SDLCORE-007` territory — scope carefully before starting, don't assume it's separate.
  `ANDR2-007` is Android's own version of the `READINGS-003`/`SDLCORE-007` conflict
  just settled (leave deferred) — very likely the same outcome if picked up.
  `ANDR2-012`/`015`/`VIB2-005` need real hardware. `ANDR2-014` (fake-NDK-adapter +
  fuzz harness) is large architecture, comparable to `TEST2-005`.
- A genuinely new design need surfaced twice this pass (`ANDR2-002`'s "app lifecycle
  changes" and `SDLCORE-005`'s mid-session live-disconnect bullet): neither the SDL
  sensor path nor the Android bridge has any mechanism for an **already-started**
  instance to notice its device going away without a *new* `Start()` call prompting
  re-validation. `ANDR2-012` is explicitly scoped for the Android/app-lifecycle half of
  this; the SDL-sensor half (an already-running `Accelerometer`/`Gyroscope` instance)
  has no task of its own yet — worth creating one if picked up, rather than bolting it
  onto `SDLCORE-005` retroactively.

Before starting any task, grep `plans/plan_devices.md` for other tasks touching the same
file/function first — several tasks this pass and last overlapped with or were
already resolved by a sibling task. Read each task's full `plans/plan_devices.md` entry
before starting, and read Section 1's labeling-convention note above before deciding
whether a finished implementation should be marked CLOSED or left OPEN.

---

## 9. Do not do yet

- Everything from prior checkpoints still applies (no `Net` fix, no broad
  `sharp-runtime` change, no `SDL_RunOnMainThread()` redesign, no symmetric
  `backendQuiescent_` wait in `Stop()`, no `third_party/SDL` edits, no fabricated
  hardware evidence, no push without asking).
- Do not re-litigate `BASE2-007`/`LIFE-008`'s "no RAII quota token" decision without a
  concrete new reason.
- Do not add a *direct* `SDL_Log()` (or any raw SDL call) to `AndroidSensorBridge.cpp`
  — still deliberately SDL-free at the translation-unit level. `DEVPERF-005`'s
  `ANDR2-006` migration routes this file's diagnostics through
  `NativeDiagnosticSink::Record()` instead (a *separate* translation unit,
  `NativeDiagnostic.cpp`, that happens to use `SDL_Log()` internally) — this is
  compliant with the letter of this rule (verified building clean on Android both
  before and after), not an exception to it; don't add a *new*, second raw SDL call
  to this file on the theory that one already snuck in.
- `SDLCORE-007` ("use acquisition timestamps from SDL sensor events"): **decided
  2026-07-18, via explicit `AskUserQuestion` — leave deferred.** Do not implement
  the calibrated monotonic-to-wall-clock bridge this task's required work asks for;
  `READINGS-003`'s already-settled, cross-sensor-class-consistent "always
  wall-clock-at-dispatch" policy stands (see Section 2's own dated entry for the
  full investigation and decision). Settled, not open for autonomous
  re-litigation — would need a new, concrete reason (e.g. a real reported
  timestamp-accuracy problem) to revisit.
- **`Detail::ConvertAndroidPortraitToXnaLandscape()` is a reflection (determinant
  `-1`), not a proper rotation, for both `Rotation90`/`Rotation270`** (found and
  verified by direct NumPy computation while working `MOT2-001`, 2026-07-18 — see
  that task's own resolution note and the post-closure cross-reference added to
  `ACCEL-008`'s entry). Do **not** unilaterally "fix" this by changing
  `ConvertAndroidPortraitToXnaLandscape()`'s own formula — it is already-shipped,
  already-tested, `NOXNA` behavior across `Accelerometer`/`Gyroscope`/`Motion` (5
  fields total) with its own maintainer-made decision (`ACCEL-008`) to keep it;
  changing its actual output values would be exactly the kind of large,
  unverifiable-without-hardware behavior change the standing rules say to flag,
  not silently make. Do **not** assume this was already checked just because
  `ACCEL-004`/`ACCEL-008`/`MOTION-012` show as `CLOSED` — it was not, until this
  finding. If picked up, treat as its own properly-scoped task (derive what the
  *correct* proper-rotation transform should be, decide whether to fix the
  existing three vector fields' remap or replace it with something else,
  re-verify every existing `AndroidSensorOrientationTests.cpp` test's expected
  values against the corrected formula) — not a quick patch.
- Do not mark a task fully `CLOSED` just because `ANDR2-004`/`005`/`006` were, if its
  own acceptance criteria name an empirical/dynamic result those three didn't — see
  Section 1's labeling convention. Check the literal wording every time.
- Do not build a full native fault-injection layer (`TEST2-005`) as a side effect of
  some other task "just to make its test pass" — it's valuable enough to be its own
  properly-scoped task; note the dependency in that task's resolution instead.

---

## 10. Resume prompt

```
Read plans/plan_devices.md's "Section 16. Independent perfection re-audit backlog
(2026-07-17)" first -- it is the source of truth for current work. Read this
file (NEXTdevices.md)'s own Section 2 top for the precise, directly-counted
task breakdown (92 total / 27 CLOSED / 24 OPEN-but-progressed / 41
OPEN-untouched, with a re-count script) before assuming anything about scope
remaining -- re-run that script if this checkpoint is more than a session or
two old, since it will go stale. For what's been done: all P0 tasks are closed, and 37 P1
tasks plus 1 P2 task are closed or progressed so far (BASE2-007, VIB2-002, VIB2-001, LIFE-008,
ANDR2-004, ANDR2-005, ANDR2-006, LIFE-006, COMP2-009, MOT2-002, COMP2-002,
VIB2-003, VIB2-004, ANDR2-002, SDLCORE-009, SDLCORE-005, COMP2-001, MOT2-003,
MOT2-005, ANDR2-009, ANDR2-010, BASE2-001, COMP2-008, BASE2-002, BASE2-003,
BASE2-004, BASE2-005, DEVPERF-004, DEVPERF-005, SDLCORE-007, SDLCORE-011,
PERF2-002, TEST2-002, COMP2-003, MOT2-001, TEST2-010, PERF2-001, VIB2-006 (P2) --
see Section 2 for commit hashes
and a one-line summary of each, including PERF2-002's own new 100k-cycle
lifecycle leak tests, TEST2-002's own consolidated clean-checkout sanitizer
sweep, COMP2-003's citation-backed display-orientation finding plus
independent reference cross-check tests, and **`MOT2-001`'s significant new
finding that `ConvertAndroidPortraitToXnaLandscape()` is a reflection, not a
rotation -- read Section 9's own flagged note on this before touching that
function or `ACCEL-004`/`ACCEL-008`/`MOTION-012`**), plus a
separate, unrelated re-verification round of the *older* `audit_devices.md`
(6 `DEV-AUD-*` findings, commit `422ed4c4` -- see Section 2's own dated
entry). All five `BASE2-*` P1 tasks are now done. `DEVPERF-004` found a real,
concrete, previously-undetected gap (AndroidSensorBridge::Run()'s silent
exception-swallow, no logging/counter, unlike SDLCORE-009's SDL-side fix);
`DEVPERF-005` built the shared Detail::NativeDiagnosticSink, closed that gap,
then migrated all five named follow-up call sites (SDLCORE-009, VIB2-003,
VIB2-004, SDLCORE-005, ANDR2-006) plus 2 more a dedicated sweep found, and is
now **CLOSED** -- do not reopen it without a new, concrete reason (real-hardware
fault injection for the haptic/Android-only sites is VIB2-003/004/SDLCORE-005/
ANDR2-006's own separately-tracked limitation, not DEVPERF-005's).
`SDLCORE-011` (VibrateController's static-destruction-order hazard against
SDL_Quit()) has its core fix done and verified, both by reasoning from SDL's
own source and by a real standalone ASan harness -- stays OPEN only because the
one genuinely dangerous call site (SDL_CloseHaptic use-after-free) needs real
hardware to verify, not because the fix itself is incomplete. `SDLCORE-007`
was investigated and found in direct conflict with the already-settled
READINGS-003 policy -- deliberately NOT implemented, left for explicit human
input (see Section 9's "Do not do yet"), not a task to pick up autonomously.
Read Section
1's "labeling convention" note carefully before closing anything -- it
distinguishes tasks provable by code inspection (CLOSED, e.g. SDLCORE-009,
BASE2-005) from tasks whose acceptance criteria name an empirical/hardware
result (stays OPEN even once implemented, e.g. VIB2-003/004, ANDR2-002,
SDLCORE-005, COMP2-001, MOT2-005, ANDR2-009/010, BASE2-001/002). MOT2-003 is
a special case: only its narrowest sub-bullet was implemented, its
acceptance criteria remain genuinely unmet (not just hardware-unverified) --
don't mistake it for a near-complete fix. BASE2-001/002 are a *third* "why
OPEN" pattern worth remembering: both looked oracle-blocked at first glance
but each had a real, oracle-independent bug (a UBSan-caught overflow, then a
TSan-proven atomicity race) hiding in their own problem statement.
BASE2-003/004 are the counter-cases: investigation doesn't always find a new
bug -- BASE2-004 confirmed a prior task (DEV-API-005) had already correctly
resolved the gap, and BASE2-003 produced honest reachability documentation
without inflating it into an unverifiable "intentional" claim. BASE2-005
closed outright: its problem statement read as oracle-blocked but both named
acceptance-criteria gaps (reentrant update, handler-list mutation during
dispatch) were closeable by reading sharp-runtime's EventHandler<T>::Raise()
directly plus one new test -- no oracle was actually needed. General lesson,
now proven five times over: investigate each task for a concrete,
oracle-independent bug or gap named in its own problem statement before
assuming it's entirely blocked.

CPU thermal pacing rule was restated by the user on 2026-07-18: pause
starting new work only at >=85C, resume once back at <=75C; always finish
already-started work regardless of temperature (see feedback_cpu_thermal_pacing
memory and Section 2's dated note) -- do not throttle builds to -j1/-j2 in the
75-85C band, that was this session's own over-correction, not the actual rule.

Continue the backlog (Section 8 lists untriaged candidates with rough
tractability notes -- BASE2-* is now fully done, COMP2-008 is also done, so
ignore any older references to those as still-open). **The tractable P1
backlog is now largely exhausted**: every remaining OPEN P1 item is either
genuinely hardware-gated (needs a real Android/haptic device this container
doesn't have) or a large architecture task deliberately set aside
(TEST2-004, TEST2-005, LIFE-007/010/011, ANDR2-011/014, MOT2-006 -- see
Section 9's "Do not do yet" for why each was set aside, not picked up as a
quick continuation). `VIB2-006` (P2) was the first P2 item picked up this
pass for exactly this reason -- if the P1 backlog stays exhausted, continue
into P2 (`VIB2-007`, `TEST2-003`, `TEST2-006`'s host-testable pieces if any,
etc.) rather than forcing an unready P1 architecture task. Read each task's
full plans/plan_devices.md entry before starting. For each task worked: implement,
add/extend tests where a real test seam exists (several P1 items are
Android-only with zero host coverage -- verify those via a real NDK
cross-compile of the exact translation unit instead), build
cmake-build-devices-ubsan and re-run the Devices/Sensors filtered suite (use
the precise filter in Section 7, not a broad one -- it incidentally trips a
pre-existing, unrelated Vector3 UBSan finding), run devices-tsan (3-4x) if
the task touches concurrent host-buildable logic, update plans/plan_devices.md's
Section 16 entry with a full resolution/progress note, and make one focused
git commit per task or tightly-related group.

Do not mark anything CLOSED on insufficient grounds (Section 1's mandatory
rules and labeling convention) and do not fabricate hardware evidence --
leave hardware-only validation explicitly OPEN with a documented device test
procedure in docs/devices-hardware-checklist.md, exactly as this pass's tasks
did.

Work autonomously through the backlog without stopping for confirmation
unless you hit a genuine architectural decision, a hardware-only validation
boundary, or a real environmental blocker. Update this file again before
context grows large enough that a future session would need to resume cold.
```
