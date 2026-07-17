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

**Pattern across `ANDR2-002`/`004`/`005`/`006`:** all inside `#ifdef __ANDROID__` code
with **zero host-side test coverage possible** — verified instead via a real Android
NDK cross-compile of the exact translation unit each time (`cmake-build-android`,
`arm64-v8a`, API 24). The **full** `CNA` library Android cross-compile remains blocked
by a pre-existing, unrelated `sharp-runtime` failure (`RandomNumberGenerator.cpp`'s
`::getrandom()` call missing for this NDK/API combination) — only single-translation-unit
compiles work. `ANDR2-006`'s `liblog.so` link dependency has still **not** been
confirmed to actually resolve at link time.

**Build:** `cmake-build-devices-ubsan` and `cmake-build-android` both still build
clean (the latter for individual translation units only, per above).

**Tests:** Devices/Sensors filtered suite — **284 tests** (narrower, precise filter
used from `VIB2-003` onward: `AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:
MotionTests.*:SensorBaseTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*:
AndroidMotionMathTests.*:AndroidCompassMathTests.*`) — **280 passed, 4 skipped**
(hardware-only, unchanged), 0 failures across every task above. Note: a broader,
unscoped `*Devices*:*Sensor*:...` filter also incidentally matches
`AccelerometerReadingTests`/`*EventArgsTests` (plain data-holder tests), one of which
(`GetHashCodeConsistency`) trips a **pre-existing, unrelated** UBSan finding in
`Vector3::GetHashCode()` (signed-int overflow in hash-combining) — not touched by any
task this pass, out of scope for Devices work, not silenced, just avoid the broad
filter and use the precise one above (or expect and ignore that one specific failure
if using a broader filter for some other reason).

**Sanitizers:** `devices-ubsan` clean on every P1 change this pass. `devices-tsan` was
NOT re-run for `VIB2-003`/`004`/`ANDR2-002` — none add new locking/concurrency
structure beyond what already existed (same mutex scope, same call ordering; the one
new mutex use in `ANDR2-002`, `IsAvailable()`'s lock, is Android-only and can't run
under host TSan at all). Re-run TSan if a future P1 task touches concurrent
*host-buildable* logic.

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
  `VIB2-004`'s disconnect/reconnect detection, and `ANDR2-002`'s lock/invalidation
  fixes are all implemented and reasoned-through-correct but **never actually
  exercised** — no haptic device and no Android hardware/emulator exist in this
  container. Each has a documented hardware validation procedure in
  `docs/devices-hardware-checklist.md` (Sections 4a, 4b, 6a respectively) and is
  explicitly left **OPEN** in `plan_devices.md` (see Section 1's labeling convention).
- 35+ more Section 16 tasks remain OPEN across P1/P2/P3 (`plan_devices.md` is the
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

---

## 7. Useful commands

```bash
# Build + test (precise filter — avoids the pre-existing Vector3 UBSan finding, see
# Section 2/4):
cmake --build cmake-build-devices-ubsan --target CnaTests -j4
UBSAN_OPTIONS=halt_on_error=1 ./cmake-build-devices-ubsan/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:SensorBaseTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*:AndroidMotionMathTests.*:AndroidCompassMathTests.*"

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

- **`LIFE-007`/`010`/`011` — deliberately set aside, not merely unstarted.** Large
  architecture tasks; `LIFE-011` specifically has a **real design tension** already
  found (see prior checkpoint / its own `plan_devices.md` notes) — do not attempt a
  naive symmetric `Stop()` wait, it reintroduces `TEST2-001`'s fixed deadlock.
- `DEVPERF-002`–`005` — API/behavioral oracle generation, callback/threading contract
  documentation, structured diagnostic channel. `DEVPERF-005` (diagnostic channel) is
  referenced as "future scope" by several already-closed tasks — worth considering
  next since multiple other tasks implicitly wait on it.
- `SDLCORE-005`/`009` — hotplug handling, callback exception consistency (host-buildable,
  unlike the ANDR2-* Android items — worth prioritizing for actual TSan coverage).
  `SDLCORE-007`/`011` — investigated briefly last pass, still open, see prior notes.
- `ANDR2-007`/`009`–`012`/`014`/`015` — remaining Android-only items (`014`/`015`
  explicitly want fuzzing/instrumented hardware runs; `ANDR2-012` is the right home for
  the "app lifecycle changes" scope `ANDR2-002` deliberately deferred).
- `COMP2-001`/`003`/`004`/`005`/`008` — remaining Compass items (`004`/`005` need
  physical devices).
- `MOT2-001`/`003`/`005`/`006`/`008`/`009`/`010` — remaining Motion items (`010` needs
  physical hardware).
- `BASE2-001`–`005` — mostly "verify against a behavioral oracle" tasks; this
  environment has no WP7 SDK/MonoGame reference — may need scoping down to "verify
  internal consistency", a decision worth making explicit if picked up.
- `VIB2-005`–`007` — remaining Vibrate items (`005` needs a direct-backend Android
  validation; `006`/`007` are host-testable design/behavior questions).
- `PERF2-001`–`003`, `TEST2-002`/`004`–`006`/`010` — remaining P1 perf/test-infra items.
  `TEST2-005` ("Build a native fault-injection layer") is now referenced by three
  closed-but-OPEN tasks (`VIB2-003`/`004`, `ANDR2-002`) as the thing that would let
  their acceptance criteria actually be verified — worth prioritizing highly if
  picked up, since it unblocks re-closing multiple tasks at once, not just its own.

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
file (NEXTdevices.md) for what's been done: all P0 tasks are closed, and 14 P1
tasks are closed or progressed so far (BASE2-007, VIB2-002, VIB2-001, LIFE-008,
ANDR2-004, ANDR2-005, ANDR2-006, LIFE-006, COMP2-009, MOT2-002, COMP2-002,
VIB2-003, VIB2-004, ANDR2-002 -- see Section 2 for commit hashes and a one-line
summary of each). Read Section 1's "labeling convention" note carefully before
closing anything -- it distinguishes tasks provable by code inspection (CLOSED)
from tasks whose acceptance criteria name an empirical/hardware result (stays
OPEN even once implemented).

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
