# NEXT.md — CNA Project Handoff (Devices)

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3
with a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It preserves
XNA-style public APIs (`Microsoft::Xna::Framework`, `Microsoft::Devices`) while using
modern C++ internally. Branch: `feature/devices`.

**Current effort: an independent "perfection re-audit" of `Microsoft::Devices`.**
On 2026-07-17 the user supplied a fresh, independent 92-task re-audit
(`audit_devices_2026-07-17.md`, merged verbatim into `plan_devices.md` as
**"Section 16. Independent perfection re-audit backlog (2026-07-17)"**). This section
is the primary source of truth for current work and **intentionally reopens areas
older parts of `plan_devices.md` describe as CLOSED** — an older CLOSED label is not
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
- Per-task workflow ends with: updating `plan_devices.md`'s Section 16 entry (status,
  resolution/implementation summary, files changed, tests run, sanitizer result,
  remaining limitations, hardware evidence status) **and** a focused git commit.
- Keep `plan_devices.md` updated continuously, and update this file periodically —
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
the first three examples of the "stays OPEN" case — read their `plan_devices.md`
resolution notes as the template.

---

## 2. Current status (2026-07-17, mid-P1)

**All P0 tasks closed** (see prior checkpoints / `plan_devices.md` for full detail).

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

**Tests:** Devices/Sensors filtered suite — **325 tests** (precise filter, used from
`VIB2-003` onward, now including `AndroidSensorBridgeTests.*` since `ANDR2-009`:
`AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:SensorBaseTests.*:
SensorSubsystemOwnershipTests.*:VibrateControllerTests.*:AndroidMotionMathTests.*:
AndroidCompassMathTests.*:AndroidSensorBridgeTests.*`) — **321 passed, 4 skipped**
(hardware-only, unchanged), 0 failures across every task above. `MOT2-003` added no
tests (Android-only, no pure-logic component to test the way `COMP2-001`/`MOT2-005`/
`ANDR2-009`/`ANDR2-010`/`BASE2-001`'s host-testable pieces had). Note: a broader,
unscoped `*Devices*:*Sensor*:...` filter also incidentally matches
`AccelerometerReadingTests`/`*EventArgsTests` (plain data-holder tests), one of which
(`GetHashCodeConsistency`) trips a **pre-existing, unrelated** UBSan finding in
`Vector3::GetHashCode()` (signed-int overflow in hash-combining) — not touched by any
task this pass, out of scope for Devices work, not silenced, just avoid the broad
filter and use the precise one above (or expect and ignore that one specific failure
if using a broader filter for some other reason).

**Sanitizers:** `devices-ubsan` clean on every P1 change this pass — **and directly
caught a real bug this pass** (`BASE2-001`'s `ShouldAcceptUpdateAt()` overflow, see
Section 2's own entry — this is exactly the kind of finding the mandatory "verify via
sanitizer, don't just reason about it" rule exists for). `devices-tsan` was NOT re-run
for `VIB2-003`/`004`/`ANDR2-002`/`COMP2-001`/`MOT2-003`/`ANDR2-009`/`010` (none add new
locking/concurrency structure beyond what already existed, or the new state is a
worker-thread-only-written atomic with no new lock) but WAS re-run for `SDLCORE-009`,
`SDLCORE-005`, `MOT2-005`, and `BASE2-001` (a new lock-acquisition site, or a
meaningfully-changed comparison inside an existing one, on a real dispatch path) — 3
consecutive clean runs each, 0 `WARNING: ThreadSanitizer` occurrences. Re-run TSan if a
future P1 task touches concurrent *host-buildable* lock-based logic.

---

## 3. Recent changes — see Section 2's numbered list plus prior checkpoints (git
history / `plan_devices.md`) for full technical detail on each. Not duplicated here.

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
  respectively) and is explicitly left **OPEN** in `plan_devices.md` (see Section 1's
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
- 28+ more Section 16 tasks remain OPEN across P1/P2/P3 (`plan_devices.md` is the
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
scanning `plan_devices.md` Section 16 (no mandated order within P1 — pick by
tractability):

- **`LIFE-007`/`010`/`011`, `ANDR2-011` — deliberately set aside, not merely
  unstarted.** Large architecture tasks; `LIFE-011` specifically has a **real design
  tension** already found (see prior checkpoint / its own `plan_devices.md` notes) — do
  not attempt a naive symmetric `Stop()` wait, it reintroduces `TEST2-001`'s fixed
  deadlock. `ANDR2-011` ("consolidate Android sensor bridges onto a shared looper") was
  scanned and judged comparably large (up to 6 worker threads per `Motion` instance →
  one shared looper/thread — a genuine architecture redesign, not a quick fix).
- `DEVPERF-002`–`005` — API/behavioral oracle generation, callback/threading contract
  documentation, structured diagnostic channel. `DEVPERF-005` (diagnostic channel) is
  referenced as "future scope" by several already-closed tasks (including this pass's
  `SDLCORE-009`) — worth considering next since multiple other tasks implicitly wait on
  it. `DEVPERF-002`/`003` (the behavioral oracle) are now **also** a confirmed blocker
  for `BASE2-001`'s remaining scope (see Section 5) — picking these up unblocks more
  than just their own tasks.
  `SDLCORE-007`/`011` — investigated briefly last pass, still open, see prior notes.
- `ANDR2-007`/`012`/`014`/`015` — remaining Android-only items (`ANDR2-009`/`010` done
  this pass, `011` deliberately deferred per above; `007` is the same design-heavy
  "calibrated boot/monotonic-to-UTC offset" concern as `SDLCORE-007` below, for the
  Android-native path specifically; `014`/`015` explicitly want fuzzing/instrumented
  hardware runs; `ANDR2-012` is the right home for the "app lifecycle
  changes"/mid-session-recovery scope both `ANDR2-002` and `SDLCORE-005` deliberately
  deferred this pass).
- `COMP2-003`/`004`/`005`/`008` — remaining Compass items (`COMP2-001` done this pass;
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
- `BASE2-002`–`005` — like `BASE2-001` (done this pass, see Section 2), all rely on "the
  behavioral oracle"/"reference behavior"; this environment has no WP7 SDK/MonoGame
  reference and `DEVPERF-002`/`003` (the oracle-generation tasks) are themselves not yet
  done — genuinely blocked, not just under-scoped. `BASE2-001`'s own precedent: **do**
  look for a concrete, oracle-independent bug/gap named in the problem statement first
  (it found and fixed a real overflow this way) before concluding a task is entirely
  blocked — investigate before deferring wholesale.
- `VIB2-005`–`007` — remaining Vibrate items (`005` needs a direct-backend Android
  validation; `006`/`007` are host-testable design/behavior questions).
- `PERF2-001`–`003`, `TEST2-002`/`004`–`006`/`010` — remaining P1 perf/test-infra items.
  `TEST2-005` ("Build a native fault-injection layer") is now referenced by four
  closed-but-OPEN tasks (`VIB2-003`/`004`, `ANDR2-002`, `SDLCORE-005`) as the thing that
  would let their acceptance criteria actually be verified — worth prioritizing highly
  if picked up, since it unblocks re-closing multiple tasks at once, not just its own.
- A genuinely new design need surfaced twice this pass (`ANDR2-002`'s "app lifecycle
  changes" and `SDLCORE-005`'s mid-session live-disconnect bullet): neither the SDL
  sensor path nor the Android bridge has any mechanism for an **already-started**
  instance to notice its device going away without a *new* `Start()` call prompting
  re-validation. `ANDR2-012` is explicitly scoped for the Android/app-lifecycle half of
  this; the SDL-sensor half (an already-running `Accelerometer`/`Gyroscope` instance)
  has no task of its own yet — worth creating one if picked up, rather than bolting it
  onto `SDLCORE-005` retroactively.

Before starting any task, grep `plan_devices.md` for other tasks touching the same
file/function first — several tasks this pass and last overlapped with or were
already resolved by a sibling task. Read each task's full `plan_devices.md` entry
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
- Do not add `SDL_Log()` (or any SDL call) to `AndroidSensorBridge.cpp` — deliberately
  SDL-free; use `__android_log_print()` there instead.
- Do not mark a task fully `CLOSED` just because `ANDR2-004`/`005`/`006` were, if its
  own acceptance criteria name an empirical/dynamic result those three didn't — see
  Section 1's labeling convention. Check the literal wording every time.
- Do not build a full native fault-injection layer (`TEST2-005`) as a side effect of
  some other task "just to make its test pass" — it's valuable enough to be its own
  properly-scoped task; note the dependency in that task's resolution instead.

---

## 10. Resume prompt

```
Read plan_devices.md's "Section 16. Independent perfection re-audit backlog
(2026-07-17)" first -- it is the source of truth for current work. Read this
file (NEXTdevices.md) for what's been done: all P0 tasks are closed, and 22 P1
tasks are closed or progressed so far (BASE2-007, VIB2-002, VIB2-001, LIFE-008,
ANDR2-004, ANDR2-005, ANDR2-006, LIFE-006, COMP2-009, MOT2-002, COMP2-002,
VIB2-003, VIB2-004, ANDR2-002, SDLCORE-009, SDLCORE-005, COMP2-001, MOT2-003,
MOT2-005, ANDR2-009, ANDR2-010, BASE2-001 -- see Section 2 for commit hashes
and a one-line summary of each). Read Section 1's "labeling convention" note
carefully before closing anything -- it distinguishes tasks provable by code
inspection (CLOSED, e.g. SDLCORE-009) from tasks whose acceptance criteria
name an empirical/hardware result (stays OPEN even once implemented, e.g.
VIB2-003/004, ANDR2-002, SDLCORE-005, COMP2-001, MOT2-005, ANDR2-009/010).
MOT2-003 is a special case: only its narrowest sub-bullet was implemented,
its acceptance criteria remain genuinely unmet (not just hardware-unverified)
-- don't mistake it for a near-complete fix. BASE2-001 is a different special
case: it found and fixed a real signed-integer-overflow bug (verified by
UBSan) while investigating, but stays OPEN because its core cross-backend
unification ask is blocked on the not-yet-built behavioral oracle
(DEVPERF-002/003), not on hardware -- a third distinct "why OPEN" reason
alongside "needs hardware" and "genuinely unimplemented, comparable to
LIFE-007/010/011". Worth remembering: investigate each task for a concrete,
oracle-independent bug named in its own problem statement before assuming
it's entirely blocked -- BASE2-001's overflow fix came from doing exactly
that.

Continue the P1 backlog (Section 8 lists untriaged candidates with rough
tractability notes). Read each task's full plan_devices.md entry before
starting. For each task worked: implement, add/extend tests where a real
test seam exists (several P1 items are Android-only with zero host
coverage -- verify those via a real NDK cross-compile of the exact
translation unit instead), build cmake-build-devices-ubsan and re-run the
Devices/Sensors filtered suite (use the precise filter in Section 7, not a
broad one -- it incidentally trips a pre-existing, unrelated Vector3 UBSan
finding), run devices-tsan (3-4x) if the task touches concurrent
host-buildable logic, update plan_devices.md's Section 16 entry with a full
resolution/progress note, and make one focused git commit per task or
tightly-related group.

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
