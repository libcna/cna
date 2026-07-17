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
- **Recurring theme this pass:** several P1 tasks' literal "required work" wording
  (RAII quota tokens, EXPECT_DEATH tests, etc.) was deliberately *not* followed to the
  letter — each such deviation is documented with reasoning in `plan_devices.md`'s own
  resolution note, not silently narrowed. Read those reasoning sections before assuming
  a task needs "more" done to satisfy its literal wording.

---

## 2. Current status (2026-07-17, mid-P1)

**All P0 tasks closed** (see Section 3 of the prior checkpoint, preserved in git
history / `plan_devices.md` for full detail — not repeated here to keep this file
current rather than cumulative).

**P1 tasks closed so far, in the order worked (all committed):**
1. `BASE2-007` (`aaf3dae8`) — counter-underflow clamping → loud `assert()`. Deliberately
   did not build a full RAII quota-token class (investigated and rejected — see its own
   resolution note for why the *release* side can't simply live in a destructor given
   this API's early-explicit-`Dispose()` semantics).
2. `VIB2-002` (`129fa2af`) — NaN/infinity handling for vibration intensity/motor
   magnitudes. Fixed at two layers (`VibrateController`'s own canonicalization +
   `SdlHapticVibrateBackend`'s own checked, saturating conversion) — real UB
   (`static_cast<Uint16>(NaN * 65535.0f)`) confirmed and closed, with 6 new tests.
3. `VIB2-001` (`74ec2077`) — `IsSupported()` now also requires
   `SDL_HapticRumbleSupported(device)`, not just "some haptic device opened." Found
   that a prior session's `VIB-005` investigation had correctly rejected
   `SDL_InitHapticRumble()` (uploads a real effect) but overlooked this genuinely
   read-only sibling function — confirmed via direct SDL source reading.
4. `LIFE-008` (`856608a8`) — `Compass`/`Motion` constructors now roll back
   `instanceCount_` on any exception after reserving a slot, matching
   `Accelerometer`/`Gyroscope`'s already-correct pattern (a real, if currently
   unreachable-on-this-host, quota leak).
5. `ANDR2-004` (`0599ebe2`) — `AndroidSensorBridge`'s `RunExitGuard` destructor is now
   explicit `noexcept` + `try`/`catch`, matching `Detail::ScopeExit`'s own established
   pattern — closes a real (if narrow) crash-and-strand-every-waiter risk.
6. `ANDR2-005` (`a879f6ab`) — `ALooper_prepare()`'s return value is now null-checked
   before use.
7. `ANDR2-006` (`f7750da3`) — `ASensorEventQueue_getEvents()` negative returns (genuine
   read errors) are now distinguished from `0` ("no events yet") and trigger a
   persistent-failure shutdown after 5 consecutive errors, instead of retrying
   forever; `disableSensor()`/`destroyEventQueue()` failures are now checked and
   logged (debug builds, `__android_log_print` — needed a new `liblog.so` CMake link
   dependency, added).
8. `LIFE-006` (`de79d923`) — new `SensorBase<T>::DisposalTerminalStateGuard`, used by
   all four sensor classes, guarantees `disposed_` is published (unblocking every
   concurrent losing `Dispose()` waiter) even if the winning caller's own cleanup
   throws — closes a gap `WaitForDisposalToComplete()`'s own prior doc comment
   explicitly flagged as a known, unfixed assumption. Directly, deterministically
   tested (not just reasoned about) via a new isolated `SensorBaseTests.cpp` fixture
   extension (deliberately *not* a real `Accelerometer` — see its own resolution note
   for the shared-global-counter pollution risk that would have created for other
   tests in the same binary). Re-verified clean across 4 consecutive `devices-tsan`
   runs (this touches shared locking used by all four sensor classes).
9. `COMP2-009` (`aaaa54e4`, docs-only, no source change) — closed by reference: its
   own core concern ("destruction in either event cannot invoke the other on a dead
   owner") is the exact hazard `LIFE-005` (same pass) already fully closed for both
   `Compass`/`Motion`. Re-verified directly against current code before closing on
   that basis, rather than assuming the earlier task's own claim.
10. `MOT2-002` (`107e3e0d`) — hardened `AndroidMotionMath.hpp`'s quaternion math
    against invalid/non-unit input (`Detail::NormalizeOrIdentity()`, new shared
    helper; `asin()` argument now clamped to `[-1,1]`). **Found and fixed a real gap
    beyond the one function the task named**: `AndroidMotionBackend::HandleAttitudeSample()`
    separately builds the *published* `AttitudeReading::RotationMatrix` from the raw
    (previously unvalidated) quaternion, independently of the yaw/pitch/roll
    extraction — moved validation into `ConvertRotationVectorToXnaQuaternion()` itself
    so every downstream consumer stays consistent. One pre-existing test needed
    updating (its own non-unit-input assumption was intentionally superseded, not
    weakened); 8 new fuzz/gimbal-lock tests added.
11. `COMP2-002` (`7e366cf9`) — the Compass analogue of `MOT2-002`, for
    `AndroidCompassMath.hpp`'s different, non-`Quaternion`-typed `atan2()`-based
    formulas. New `Detail::NormalizeCompassQuaternion()`, wired into the one
    production entry point (`ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()`)
    so the tilt-mode decision and heading formula never see inconsistent (one raw,
    one normalized) data. Confirmed this file's pre-existing `double` arithmetic
    already avoids the `float`-overflow risk `MOT2-002` had to fix separately. 9 new
    tests, zero pre-existing tests needed changes.

**Pattern across `ANDR2-004`/`005`/`006`:** all three are inside `#ifdef __ANDROID__`
code with **zero host-side test coverage possible** — verified instead via a real
Android NDK cross-compile of the exact translation unit each time
(`cmake-build-android`, `arm64-v8a`, API 24). The **full** `CNA` library Android
cross-compile remains blocked by a pre-existing, unrelated `sharp-runtime` failure
(`RandomNumberGenerator.cpp`'s `::getrandom()` call missing for this NDK/API
combination) — only single-translation-unit compiles work. This means `ANDR2-006`'s
new `liblog.so` link dependency has **not** been confirmed to actually resolve at
link time — flag for whoever next fixes the `sharp-runtime` blocker.

**Build:** `cmake-build-devices-ubsan` and `cmake-build-android` both still build
clean (the latter for individual translation units only, per above).

**Tests:** Devices/Sensors filtered suite — **424 tests, 420 passed, 4 skipped**
(hardware-only, unchanged all pass). Zero regressions across every P1 task above.

**Sanitizers:** `devices-ubsan` clean on every P1 change. `devices-tsan` re-run (4
consecutive clean runs) specifically for `LIFE-006`, since that one touches shared,
genuinely concurrent base-class locking used by all four sensor classes — the other
P1 tasks this pass (`BASE2-007`/`VIB2-*`/`LIFE-008`/`ANDR2-004/005/006`/`MOT2-002`/
`COMP2-002`) don't add new concurrency (exception-safety, NaN-handling, Android-only
sequential-logic fixes, pure host-testable math), so weren't separately TSan-verified.
Re-run TSan if a future P1 task touches concurrent logic.

**Emerging pattern worth knowing about:** two tasks this pass (`MOT2-002`/`COMP2-002`)
were found to have a *second*, related task in the same P1 backlog whose own concern
turned out to already be resolved (`COMP2-009`) or whose fix needed to reach further
than the literal task wording named (a separately-published `RotationMatrix`, not just
the one function `MOT2-002` explicitly called out). Before starting a new math/lifecycle
task, grep `plan_devices.md` for other tasks touching the same file/function — some of
the remaining backlog may already be partly or fully addressed by a sibling fix.

---

## 3. Recent changes — see Section 2's numbered list (this pass) plus the prior
checkpoint's own Section 3 (P0 phase, preserved in git history) for full technical
detail on each. Not duplicated here — read each task's own `plan_devices.md`
resolution note for the complete reasoning; this file only summarizes.

---

## 4. Current blocker / main problem

**No blocker for continuing P1.** Same two flagged, out-of-scope items as the prior
checkpoint (unchanged):
1. Pre-existing `Net`/`NetworkSession.cpp` UBSan finding + full-suite segfault.
2. Pre-existing `sharp-runtime` Android cross-compile failure
   (`RandomNumberGenerator.cpp`'s `::getrandom()`) — now *additionally* blocking
   confirmation that `ANDR2-006`'s new `liblog.so` link dependency actually resolves.

---

## 5. Known bugs and limitations

- Everything from the prior checkpoint's Section 5 still applies (LIFE-* concurrency
  boundary, ANDR2-001/003 host-untestability, Net/sharp-runtime gaps).
- **New this pass:** `VIB2-001`'s `SDL_HapticRumbleSupported()` check has no real
  haptic device to exercise it against (this container has none) — the new code path
  itself was never actually reached by any test, only reasoned about via direct SDL
  source reading. `LIFE-008`'s rollback `catch` block is similarly never actually
  reached on this host (no throwing path exists here today) — both are documented,
  honest "implemented but not behaviorally exercised" gaps, not fabricated evidence.
- 40+ more Section 16 tasks remain OPEN across P1/P2/P3 (`plan_devices.md` is the
  actual source of truth — this file only tracks what's been *closed*).

---

## 6. Architecture notes

No new architectural pieces this P1 pass (unlike the P0 phase's `SensorOwnerControlBlock`/
`DispatchRegistration`/`SdlSubsystemMutex` additions) — every fix so far has been a
localized change to existing code:
- `Compass`/`Motion` constructors: now wrap the post-quota-reservation body in
  `try`/`catch(...) { --instanceCount_; throw; }`, matching
  `Accelerometer`/`Gyroscope`.
- `VibrateController.cpp`/`SdlHapticVibrateBackend.cpp`: NaN-canonicalizing helpers
  (`CanonicalizeVibrationMagnitude`, `SanitizeSdlHapticInput`, `ToSdlHapticMagnitude`).
- `AndroidSensorBridge.cpp`'s `RunExitGuard`: now `noexcept` + `try`/`catch`.
  `Run()`: null-checks `ALooper_prepare()`; tracks `consecutiveGetEventsFailures`
  across poll iterations; checks `disableSensor()`/`destroyEventQueue()` return values
  with a debug-only `__android_log_print()`.
- `cmake/CnaLibrary.cmake`: Android's `target_link_libraries(CNA PUBLIC android)` now
  also links `log` (for `__android_log_print`).

---

## 7. Useful commands

```bash
# Build + test (unchanged from the P0 checkpoint):
cmake --build cmake-build-devices-ubsan --target CnaTests -j4
./cmake-build-devices-ubsan/CnaTests --gtest_filter="*Accelerometer*:*Gyroscope*:*Compass*:*Motion*:*Sensor*:*VibrateController*:*Haptic*"

# TSan (re-run if a P1 task touches concurrent logic; repeat 3-4x, this pass's own P0
# bug was timing-dependent):
cmake --build cmake-build-devices-tsan --target CnaTests -j3
TSAN_OPTIONS="halt_on_error=0" ./cmake-build-devices-tsan/CnaTests --gtest_filter="..."
# ALWAYS grep the full log for "WARNING: ThreadSanitizer" -- exit code alone (66) is
# the fastest signal something fired; a clean gtest summary does not mean TSan found
# nothing.

# Android cross-compile (single translation unit -- full CNA link still blocked):
cd cmake-build-android && ninja CMakeFiles/CNA.dir/<path-to-file>.cpp.o
# (cmake-build-android already configured from the P0 phase; re-run cmake -S . -B
# cmake-build-android ... from NEXTdevices.md's own prior-checkpoint command only if
# the build dir is missing/stale)

# Thermal check (this container's Tctl runs high independent of this session's own
# builds -- pace with -j3-4 if >75C, don't wait for it to drop below 70C, it may not):
sensors | grep -i tctl
```

---

## 8. Next smallest task

Continue the P1 backlog. Not yet triaged/started, roughly in the order encountered
scanning `plan_devices.md` Section 16 (no mandated order within P1 — pick by
tractability, same reasoning as this pass's choices):

- **`LIFE-007`/`010`/`011` — deliberately set aside, not merely unstarted.** These are
  large architecture tasks (a full explicit state machine; failure-class-to-state
  mapping; per-generation in-flight callback counters) — investigated `LIFE-011`
  specifically far enough to find a **real design tension**: its literal ask ("Stop()
  guarantees no further callback after return") cannot be satisfied by naively making
  `Stop()` wait for `backendCallsInFlight_ == 0` before returning — that reintroduces
  the *exact* deadlock `TEST2-001` fixed (`ConcurrentStopDuringStartDoesNotDeadlock`'s
  synchronous-join scenario). Resolving it properly needs the required work's own
  "per-generation in-flight counters" (distinguishing "waiting on a call this Stop()
  itself superseded" from "waiting on a call this Stop() is legitimately stopping"),
  which is real design work, not a quick fix — do not attempt a naive symmetric wait.
- `DEVPERF-002`–`005` — API/behavioral oracle generation, callback/threading contract
  documentation, structured diagnostic channel. Larger, more design-heavy P1 tasks;
  `DEVPERF-005` (diagnostic channel) in particular has been referenced as "future
  scope" by several tasks closed this pass (`ANDR2-006` especially) — worth
  considering next since multiple other tasks are implicitly waiting on it.
- `SDLCORE-005`/`009` — hotplug handling, callback exception consistency.
  `SDLCORE-007` (acquisition timestamps) is a meaningful policy change (dispatch-time
  wall clock → calibrated monotonic-to-UTC bridge) — design-heavy, not a quick fix.
  `SDLCORE-011` (shutdown ordering) investigated briefly: nothing in production code
  currently calls `SDL_Quit()` (only one example demo does, in its own `main()`), so
  the actual risk in *this* codebase today is narrower than the task's framing
  suggests — worth re-confirming that's still true before deciding how much this one
  actually needs, rather than assuming the full fix is urgent.
- `ANDR2-002`/`007`/`009`–`012`/`014`/`015` — remaining Android-only items (several are
  real-hardware-only by nature: `014`/`015` explicitly want fuzzing/instrumented
  hardware runs). `ANDR2-007`'s "calibrated boot/monotonic-to-UTC offset" is the same
  design-heavy concern as `SDLCORE-007` above, for the Android-native path specifically.
- `COMP2-001`/`003`/`004`/`005`/`008` — `MOT2-002`/`COMP2-002` (quaternion math
  hardening) are done; remaining Compass items are timestamp alignment, axis-basis
  derivation, and hardware-truth-table/verification tasks (`COMP2-004`/`005`
  explicitly need physical devices — scope those like `ANDR2-001`/`003` did).
- `MOT2-001`/`003`/`005`/`006`/`008`/`009`/`010` — remaining Motion items (`MOT2-010`
  needs physical hardware).
- `BASE2-001`–`005` — mostly "verify against a behavioral oracle" tasks; this
  environment has no WP7 SDK/MonoGame reference to compare against directly, so these
  may need to be scoped down to "verify current behavior is internally consistent and
  documented" rather than "verify against a real oracle" — a scope decision worth
  making explicit if picked up.
- `VIB2-003`–`007`, `PERF2-*`, `TEST2-002`+ — remaining P1/P2 items in those areas.

Before starting any task, grep `plan_devices.md` for other tasks touching the same
file/function first — this pass found two cases (`COMP2-009`, and part of `MOT2-002`'s
own fix) where a task's concern overlapped with or was already resolved by a sibling
task. Read each task's full `plan_devices.md` entry before starting.

---

## 9. Do not do yet

- Everything from the prior checkpoint's Section 9 still applies (no `Net` fix, no
  broad `sharp-runtime` change, no `SDL_RunOnMainThread()` redesign, no symmetric
  `backendQuiescent_` wait in `Stop()`, no `third_party/SDL` edits, no fabricated
  hardware evidence, no push without asking).
- Do not re-litigate `BASE2-007`/`LIFE-008`'s "no RAII quota token" decision without a
  concrete new reason — both were investigated and documented, not skipped out of
  laziness.
- Do not add `SDL_Log()` (or any SDL call) to `AndroidSensorBridge.cpp` — it is
  deliberately SDL-free; use `__android_log_print()` (already wired up, `liblog.so`
  linked) for anything debug-diagnostic in that specific file.

---

## 10. Resume prompt

```
Read plan_devices.md's "Section 16. Independent perfection re-audit backlog
(2026-07-17)" first -- it is the source of truth for current work. Read this
file (NEXTdevices.md) for what's been done: all P0 tasks are closed (see git
log for the full list, commits through TEST2-001), and 7 P1 tasks are closed
so far (BASE2-007, VIB2-002, VIB2-001, LIFE-008, ANDR2-004, ANDR2-005,
ANDR2-006 -- see Section 2 for commit hashes and a one-line summary of each).

Continue the P1 backlog (Section 8 lists untriaged candidates with rough
tractability notes). Read each task's full plan_devices.md entry before
starting. For each task worked: implement, add/extend tests where a real
test seam exists (several P1 items are Android-only with zero host
coverage -- verify those via a real NDK cross-compile of the exact
translation unit instead, matching this pass's own established practice),
build cmake-build-devices-ubsan and re-run the Devices/Sensors filtered
suite, run devices-tsan (3-4x) if the task touches any concurrent logic,
update plan_devices.md's Section 16 entry with a full resolution note, and
make one focused git commit per task or tightly-related group.

Do not mark anything CLOSED on insufficient grounds (Section 1's mandatory
rules) and do not fabricate hardware evidence -- leave hardware-only
validation explicitly OPEN with a documented device test procedure, exactly
as ANDR2-001/003/VIB2-001/LIFE-008 all did this pass.

Work autonomously through the backlog without stopping for confirmation
unless you hit a genuine architectural decision, a hardware-only validation
boundary, or a real environmental blocker. Update this file again before
context grows large enough that a future session would need to resume cold.
```
