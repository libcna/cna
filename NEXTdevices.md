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
1. `DEVPERF-001` — **DONE**
2. `SDLCORE-001` through `SDLCORE-004` — **DONE**
3. `LIFE-001` through `LIFE-005` — **DONE**
4. `ANDR2-001` — **DONE**
5. `ANDR2-003` — **DONE**
6. `TEST2-001` — **DONE**
7. then P1, then P2, then P3 (remaining `COMP2-*`, `MOT2-*`, `BASE2-*`, `VIB2-*`,
   `PERF2-*`, and any remaining `SDLCORE-*`/`ANDR2-*`/`DEVPERF-*`/`TEST2-*`/`LIFE-*`) —
   **NOT YET STARTED — this is the next phase, see Section 8.**

**All P0 items in the explicit execution order are now CLOSED.** This is a genuine
milestone, not a summary of intent — every one was implemented, tested (host-side
where possible), and TSan-verified where concurrency was involved.

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
  fixing the underlying issue or rigorously proving it safe. **This rule was tested for
  real this pass — see Section 3's TEST2-001 entry — and the answer was to actually fix
  a genuine bug TSan found, not dismiss the noise.**
- Per-task workflow ends with: updating `plan_devices.md`'s Section 16 entry (status,
  resolution/implementation summary, files changed, tests run, sanitizer result,
  remaining limitations, hardware evidence status) **and** a focused git commit.
- Keep `plan_devices.md` updated continuously, and update this file periodically —
  especially before context grows large enough that another session must resume cold.

---

## 2. Current status (2026-07-17, end of the P0 phase)

**All P0 tasks closed and committed**, in execution order:
1. `SDLCORE-002`+`SDLCORE-003` (commit `0bf930a1`) — exact `SDL_EventFilter` signature +
   `SDL_AddEventWatch` failure handling.
2. `LIFE-001`+`LIFE-002`+`LIFE-003`+`LIFE-005` and `LIFE-004` (commit `d12a8435`) —
   two-phase Compass/Motion lifecycle, `SensorOwnerControlBlock`, Accelerometer
   dual-event snapshot fix.
3. `SDLCORE-001` (commit `fef9e16c`) — unified sensor+haptic SDL call serialization
   onto one shared mutex (not `SDL_RunOnMainThread()` — see that task's own resolution
   for the SDL-source-backed reasoning).
4. `SDLCORE-004` (commit `ac782eb2`) — replaced raw-pointer dispatch membership with
   `DispatchRegistration` nodes, closing an ABA hazard; deterministic placement-new
   regression test added for both Accelerometer and Gyroscope.
5. `DEVPERF-001` (commit `ae9d7a50`) — googletest submodule fail-fast guard +
   clean-room CI now also runs the strict-XNA-API check.
6. `ANDR2-001`+`ANDR2-003` (commit `fb8dd50a`) — stale Android live-rate reset at
   Start() boundary; `AndroidSensorBridge::Stop()` now bounded (waits, then abandons
   + sets a sticky fatal flag) instead of an unconditional `join()` that could hang
   forever on a wedged native call. Verified via a real Android NDK cross-compile of
   the exact translation unit (compile-only — no device/emulator available).
7. `TEST2-001` (commit `1657fe34`) — **found and fixed a real, previously-unverified
   concurrency bug** while doing the TSan re-verification this task itself requires:
   a superseding `Stop()` could clear `transitioning_` before an *earlier, orphaned*
   `Start()` attempt's own cleanup call had finished, letting a *third* `Start()`
   attempt's backend call overlap it — confirmed via an actual TSan data race
   cascading into a heap-corruption/use-after-free under an 8-thread stress test.
   Fixed by making `Start()`'s (not `Stop()`'s) reserve phase wait on the existing
   `backendQuiescent_` condition variable for `backendCallsInFlight_ == 0`. Verified
   clean across **4 consecutive `devices-tsan` runs** (0 warnings each) after the fix.

**Build:** `cmake-build-devices-ubsan` and `cmake-build-devices-tsan` both build clean
with every change above. `cmake-build-android` (arm64-v8a, API 24,
`~/Android/Sdk/ndk/30.0.14904198`) was also created this pass and used to
compile-verify `AndroidSensorBridge.cpp.o` directly (the full `CNA` library
cross-compile hits a **pre-existing, unrelated** `sharp-runtime` failure —
`RandomNumberGenerator.cpp`'s `::getrandom` call — not fixed, out of scope, flagged
for whoever next touches Android cross-compilation of that sibling repo).

**Tests:** Devices/Sensors filtered suite — **399 tests, 395 passed, 4 skipped**
(`AccelerometerTests`/`GyroscopeTests`'s `FailedEventWatchRegistrationRollsBackAndReportsFailure`
and `GetCurrentValuePropertyDoesNotThrowWhenSupported` — all hardware-only, correctly
skipped, no real sensor device in this container). Zero regressions across the whole
P0 phase.

**Sanitizers:**
- `devices-ubsan`: clean on everything this phase touched. One **pre-existing,
  unrelated** finding surfaced by running the *full* (non-Devices-filtered) suite once,
  for cross-subsystem regression checking: `src/Microsoft/Xna/Framework/Net/
  NetworkSession.cpp:282` invalid-vptr + a full-suite segfault, in the `Net` subsystem —
  confirmed unrelated (no file this phase touched is anywhere near it), not
  investigated further, flagged for a future `Net`-focused session.
- `devices-tsan`: **actually run this phase** (previous phases had deferred it every
  time) — found and fixed the real bug described above. 4 consecutive clean runs
  after the fix. This is the single most important verification event of this phase —
  see Section 3 for why it mattered.

---

## 3. Recent changes (this phase, 2026-07-17) — most recent first

- **`TEST2-001` (closed):** TSan verification pass across every `LIFE-*`/`SDLCORE-*`
  concurrency test. Found a real overlapping-backend-call race in Compass/Motion
  (see Section 2). Fixed in `Compass.cpp`/`Motion.cpp` (no header changes — reused
  the existing `backendQuiescent_` condvar). Also made
  `FakeCompassBackend`/`FakeMotionBackend`'s `StopCalled`/`StopCallCount` atomic (a
  second, related, expected race in test-only bookkeeping). Added
  `SensorSubsystemOwnershipTests.SensorAndHapticSdlCallsShareOneProcessWideMutex` — a
  missing regression test the coverage audit found (asserts by address that
  `GetGlobalSdlSensorMutex()`/`GetGlobalSdlSubsystemMutex()` are literally the same
  mutex object).
- **`ANDR2-001`+`ANDR2-003` (closed):** `AndroidSensorBridge::Start()` now resets
  `rateChangeRequested_`/`pendingTimeBetweenUpdates_` at every Start() boundary
  (closes stale-interval leakage across a Stop()/Start() cycle — deliberately no
  generation counter added, since `stateMutex_` already makes one unnecessary — see
  that task's resolution for the full reasoning). `Stop()`'s external-caller join is
  now bounded (`kNativeCallTimeout`, 5s) — times out into `detach()` + a sticky
  `abandoned_` flag (a deliberate permanent "fatal backend-health state") instead of
  an unconditional `join()` that could hang forever on a wedged NDK call.
- **`DEVPERF-001` (closed):** `cmake/UnitTests.cmake` gained the same fail-fast
  `FATAL_ERROR` guard `cmake/ThirdPartySDL.cmake` already had, for the
  `vendor/googletest` submodule. `.github/workflows/devices-tests.yml` now also
  builds+runs `cna_strict_xna_api_check` (previously only registered as a separate
  `ctest` test the workflow's `CnaTests`-direct step never actually touched).
- **`SDLCORE-004` (closed):** `SdlSensorSubsystem<TSensor>::startedInstances_` now
  holds `shared_ptr<DispatchRegistration>` nodes, not raw `TSensor*` — closes an ABA
  hazard where a freed instance's address, reused by a brand-new unrelated instance,
  could receive a stale dispatch meant for the original. New deterministic
  (placement-new-based) regression test for both Accelerometer and Gyroscope.
- **`SDLCORE-001` (closed):** unified `SdlSensorSubsystem`'s and
  `SdlHapticVibrateBackend`'s previously-independent SDL call mutexes into one shared
  `Microsoft::Devices::Detail::GetGlobalSdlSubsystemMutex()` — chosen over the audit's
  literal `SDL_RunOnMainThread()` suggestion after reading SDL's own source directly
  (SENSOR/HAPTIC have no real main-thread enforcement anywhere in SDL; only VIDEO on
  Apple does) and confirming a naive main-thread-marshal redesign would risk
  deadlocking this project's own already-supported multi-threaded usage.
- **`LIFE-001`–`005` (closed):** `Compass`/`Motion` rewritten to a two-phase
  reserve/release/commit lifecycle via `Detail::SensorOwnerControlBlock<TOwner>` (new,
  shared_ptr-held `{mutex, generation, owner}`); `Accelerometer::DispatchSensorReading()`
  snapshots `ReadingChanged` before raising `CurrentValueChanged` (which can destroy
  the sender) so it can still safely raise the former afterward.
- **`SDLCORE-002`+`SDLCORE-003` (closed):** exact `SDL_EventFilter` signature/calling
  convention (`static_assert`-pinned) + `SDL_AddEventWatch` failure now checked,
  captured, and rolled back correctly.
- Prior to this phase (2026-07-16, separate task, already committed/pushed before this
  phase began): 6 findings from an earlier, independent `audit_devices.md` were fixed.

---

## 4. Current blocker / main problem

**No blocker for continuing into P1.** Two things worth flagging for whoever picks
this up next, neither of which blocks further Devices work:

1. **A pre-existing UBSan finding + segfault in the `Net` subsystem** (Section 2) —
   confirmed unrelated to this phase's work, not investigated, flagged for a future
   `Net`-focused session.
2. **A pre-existing, unrelated `sharp-runtime` Android cross-compile failure**
   (`RandomNumberGenerator.cpp`'s `::getrandom` call, missing for this NDK/API
   combination) — blocks a *full* `CNA` library Android cross-compile in
   `cmake-build-android`, though single-translation-unit compiles (used this phase to
   verify `AndroidSensorBridge.cpp`) still work directly via `ninja
   CMakeFiles/CNA.dir/<path>.o`. Not fixed — out of scope, sibling repo, narrow-change
   policy (see Section 9).

---

## 5. Known bugs and limitations

- **Documented, accepted concurrency boundary** (carried over from a prior session's
  `ANDROID-BRIDGE-006`, reaffirmed by `LIFE-*`): a callback already past its
  `generation`/`owner` check, mid-flight on another thread, racing a *different*
  thread's completion of destruction remains unsupported. Same-thread reentrant
  destruction and "callback arrives after teardown began" are fully solved.
- **`ANDR2-001`/`ANDR2-003`'s host-untestability**: both fixes are entirely inside
  `#ifdef __ANDROID__`-gated code. Verified via manual trace + a real Android NDK
  compile-only cross-compile of the exact translation unit; real device/emulator
  behavioral validation was not performed. Exact device test procedures are documented
  in `plan_devices.md`'s own resolution notes for those tasks — follow them if a real
  Android device/emulator ever becomes available in this environment.
- **`Net`/`NetworkSession.cpp` UBSan finding + segfault** — see Section 4. Out of
  scope, not fixed.
- **`sharp-runtime` Android cross-compile gap** — see Section 4. Out of scope, not
  fixed (sibling repo, narrow-change policy).
- Everything Section 16 of `plan_devices.md` still lists as OPEN (all P1/P2/P3 items,
  35+ of them across `COMP2-*`/`MOT2-*`/`BASE2-*`/`VIB2-*`/`PERF2-*`, plus remaining
  `SDLCORE-005`+, `ANDR2-002`/`004`+, `DEVPERF-002`+, `TEST2-002`+) is genuinely OPEN —
  this file only tracks *this phase's* P0 work; `plan_devices.md` Section 16 is the
  actual source of truth for status on everything else.

---

## 6. Architecture notes (this phase's new/changed pieces)

```
include/Microsoft/Devices/Sensors/Detail/SensorOwnerControlBlock.hpp   ← shared_ptr-held {mutex, generation, owner} block
include/Microsoft/Devices/Detail/SdlSubsystemMutex.hpp                 ← shared process-wide SDL sensor+haptic mutex
include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp        ← DispatchRegistration nodes (ABA-safe); GetGlobalSdlSensorMutex() forwards to the shared mutex
include/Microsoft/Devices/Detail/SdlHapticVibrateBackend.hpp/.cpp      ← uses the shared mutex, no private one
include/Microsoft/Devices/Sensors/Compass.hpp / .cpp                   ← two-phase lifecycle, control_ block, backendQuiescent_ wait in Start()
include/Microsoft/Devices/Sensors/Motion.hpp / .cpp                    ← mirrors Compass exactly
src/Microsoft/Devices/Sensors/Accelerometer.cpp                        ← ReadingChanged snapshot fix, Start() reorder+rollback
src/Microsoft/Devices/Sensors/Gyroscope.cpp                            ← Start() reorder+rollback
src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp           ← rateChangeRequested_ reset at Start(); bounded Stop() + abandoned_ flag
```

- **`Compass`/`Motion`'s `Start()` reserve phase now waits on `backendQuiescent_`**
  (`backendCallsInFlight_ == 0`) before claiming a new attempt — the `TEST2-001` fix.
  **`Stop()` deliberately does not** — it must stay non-blocking with respect to an
  in-flight `Start()` (see `ConcurrentStopDuringStartDoesNotDeadlock`). If you ever
  touch this lifecycle code again: do not add a symmetric wait to `Stop()` without
  re-reading why that specific asymmetry exists — it was the exact thing that first
  caused (a naive symmetric-mutex attempt) then fixed (the asymmetric wait) a real
  deadlock during this phase's own work.
- **`AndroidSensorBridge::Impl::abandoned_`**: sticky, permanent, never reset once
  set. A bridge that hits this can never `Start()` again — a deliberate "fatal
  backend-health state" for a genuinely wedged native call, not a bug.
- Everything else from the prior phase's architecture notes (removed from this file
  to keep it current — see git history / `plan_devices.md` if older detail is needed)
  still applies unless a resolution note above says otherwise.

---

## 7. Useful commands

```bash
# Build (existing preset dirs used throughout this phase):
cmake --build cmake-build-devices-ubsan --target CnaTests -j4   # use -j3-4 if Tctl > 75C
cmake --build cmake-build-devices-tsan --target CnaTests -j3    # TSan is heavier per-TU

# Devices/Sensors filtered suite:
./cmake-build-devices-ubsan/CnaTests --gtest_filter="*Accelerometer*:*Gyroscope*:*Compass*:*Motion*:*Sensor*:*VibrateController*:*Haptic*"

# TSan run (repeat 3-4x for anything concurrency-related -- this phase's bug was timing-dependent):
TSAN_OPTIONS="halt_on_error=0" ./cmake-build-devices-tsan/CnaTests --gtest_filter="*Accelerometer*:*Gyroscope*:*Compass*:*Motion*:*Sensor*:*VibrateController*:*Haptic*"
# ALWAYS grep the full log for "WARNING: ThreadSanitizer" -- a passing gtest summary
# does NOT mean TSan found nothing; it exits nonzero (66) independently of gtest's own
# pass/fail reporting when it detects anything.

# Android cross-compile (compile-only verification, no device/emulator):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
# Full CNA target currently fails on an unrelated sharp-runtime issue -- build one
# translation unit directly instead:
cd cmake-build-android && ninja CMakeFiles/CNA.dir/src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp.o

# Thermal check (pace heavy builds if Tctl > 75C -- this pass never actually saw it
# drop back under 70C even after a 30-minute wait, so don't rely on that happening;
# just use reduced -j and keep working):
sensors | grep -i tctl
```

---

## 8. Next smallest task

**Immediate next step:** start the P1 backlog — `COMP2-*` (9 tasks), `MOT2-*` (10
tasks), `BASE2-*` (8 tasks), `VIB2-*` (7 tasks), `PERF2-*` (5 tasks), all currently
OPEN in `plan_devices.md` Section 16, plus any remaining `SDLCORE-005`+,
`ANDR2-002`/`004`+, `DEVPERF-002`+, `TEST2-002`+ items not yet triaged. The user's own
instruction is "then P1, then P2, then P3" without a specified order *within* P1 — use
judgement: several P1 items explicitly require real hardware
(`COMP2-004`/`COMP2-005`/`MOT2-010`/`VIB2-005` name physical truth-table/verification
work outright) and should be scoped the same way `ANDR2-001`/`003` were — implement
what's implementable, document the exact device procedure, leave hardware validation
OPEN. Others (`BASE2-*` especially) look host-testable without any hardware at all —
consider starting there for tractable, closeable wins before the harder
Android-math-verification tasks (`COMP2-002`/`003`, `MOT2-001`/`002`/`003`).

Read each task's full entry in `plan_devices.md` before starting — this file
intentionally doesn't restate their content.

---

## 9. Do not do yet

- Do not fix the `Net`/`NetworkSession.cpp` UBSan finding + segfault (Section 4) — out
  of scope for this Devices-focused phase.
- Do not attempt to fix the `sharp-runtime` Android `getrandom()` cross-compile
  failure as a broad change — sibling repo, narrow-change policy; a single-line,
  well-scoped fix (if one is obviously safe) could be considered later but was not
  attempted this phase.
- Do not attempt an `SDL_RunOnMainThread()`-based redesign for `SDLCORE-001` — already
  evaluated and rejected with cited SDL-source evidence.
- Do not add a symmetric `backendQuiescent_` wait to `Compass`/`Motion::Stop()` — see
  Section 6's warning; this specific asymmetry (Start() waits, Stop() doesn't) is
  load-bearing, not an oversight.
- Do not restructure `Detail::AndroidSensorBridge`'s locking scheme without a
  concrete, newly-found bug tied to a specific Section 16 finding.
- Do not edit anything under `third_party/SDL` — vendored, forbids AI-authored
  contributions per its own `CLAUDE.md`.
- Do not mark any Section 16 task CLOSED on the grounds the user explicitly ruled out
  (Section 1) — compiles / host-mock-passes / agrees-with-MonoGame / "unsupported"-by-
  comment / untested-hardware-path / "seems unlikely" are all explicitly not enough.
- Do not fabricate hardware test evidence — document the exact device procedure and
  leave that portion OPEN, exactly as `ANDR2-001`/`ANDR2-003` did this phase.
- Do not push to the remote unless explicitly asked for this phase specifically.

---

## 10. Resume prompt

```
Read plan_devices.md's "Section 16. Independent perfection re-audit backlog
(2026-07-17)" first -- it is the source of truth for current work. Read this
file (NEXTdevices.md) for what's been done: every P0 task in the user's
explicit execution order (DEVPERF-001, SDLCORE-001-004, LIFE-001-005,
ANDR2-001, ANDR2-003, TEST2-001) is CLOSED and committed. TEST2-001's own
TSan verification pass found and fixed a real, previously-unverified
concurrency bug in the LIFE-001 design (see Section 2/3/6) -- read that
before touching Compass.cpp/Motion.cpp's Start()/Stop() again.

Next: start the P1 backlog (COMP2-*, MOT2-*, BASE2-*, VIB2-*, PERF2-*, plus
any untriaged remaining SDLCORE-*/ANDR2-*/DEVPERF-*/TEST2-*/LIFE-* items).
Read each task's full plan_devices.md entry before starting. Several P1
items explicitly require real hardware -- scope those like ANDR2-001/003
(implement what's implementable, document the device procedure, leave
hardware validation OPEN). For each task worked: implement, add/extend
tests, build both cmake-build-devices-ubsan and (for anything touching
concurrency) cmake-build-devices-tsan -- run TSan 3-4x, not once, since this
phase's own bug was timing-dependent and needed repeated runs to trust. Grep
full TSan output for "WARNING: ThreadSanitizer" every time; a clean gtest
summary does not mean TSan found nothing. Update plan_devices.md's Section
16 entry with a full resolution note and make one focused git commit per
task or tightly-related group.

Work autonomously through the backlog without stopping for confirmation
unless you hit a genuine architectural decision, a hardware-only validation
boundary, or a real environmental blocker. Update this file again before
context grows large enough that a future session would need to resume cold.
```
