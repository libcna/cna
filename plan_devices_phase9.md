# Phase 9 — Physical verification and release gate

Scope: `Microsoft::Devices`/`Microsoft::Devices::Sensors`, Devices tests, `examples/demo_devices`,
and Devices documentation only. Branch `feature/devices`. This phase is explicitly **not**
about adding more code-level hardening — `plan_devices_phase8.md` is treated as useful
history, re-verified rather than assumed, and this phase's job is to build an honest
release gate: reproduce the test/sanitizer matrix, attempt real hardware/Android
packaging, and produce a precise, qualifier-honest status report.

## Audit findings (P9-1)

Re-read every file listed in the brief and verified six specific claims directly
against the current code (not against what Phase 8's own plan doc says) before making
any edit.

1. **`dispatchToken_` prevents the dispatch cleanup guard from touching `this` after a
   self-destroying callback — CONFIRMED, unchanged.** `Detail::SdlSensorSubsystem<TSensor>::
   DispatchToInstances()` (`SdlSensorSubsystem.hpp`) copies `instance->dispatchToken_`
   into a local `token` while `instance` is confirmed alive+started under the lock, and
   the `ScopeExit` cleanup guard captures `[this, token, thisThreadId]` — `this` here is
   the `SdlSensorSubsystem` itself (a process-lifetime singleton, not the `TSensor`
   instance), never the raw instance pointer. `Accelerometer.hpp`/`Gyroscope.hpp` both
   declare `std::shared_ptr<std::vector<std::thread::id>> dispatchToken_;`. Matches
   Task P8-1 exactly as documented.

2. **The documented unsupported boundary for `Accelerometer::CurrentValueChanged`
   self-destroy is still accurate — CONFIRMED, unchanged.** Re-read
   `Accelerometer::DispatchSensorReading()` line by line:
   `setCurrentValueProperty(accelerometerReading);` (raises `CurrentValueChanged`) is
   immediately followed by `if (getIsDataValidProperty() && !ReadingChanged.Empty())` —
   `getIsDataValidProperty()` is evaluated unconditionally (left operand of `&&`),
   touching `this` again regardless of whether `ReadingChanged` has any subscriber. A
   handler that destroys the instance during `CurrentValueChanged` still hits this
   second touch on freed memory. The boundary Task P8-1 documented has not been
   silently fixed or silently reintroduced as a claim — it's exactly as described.

3. **`SensorBase<T>::TimeBetweenUpdates` getter/setter guarded and return by value —
   CONFIRMED, unchanged.** `getTimeBetweenUpdatesProperty()` returns `System::TimeSpan`
   (by value) under `std::lock_guard<std::mutex> lock(mutex_);`; `setTimeBetweenUpdatesProperty()`
   locks around the compare-and-write, releasing before `TimeBetweenUpdatesChanged.Raise()`.

4. **SDL sensor calls still serialized through `Detail::GetGlobalSdlSensorMutex()` —
   CONFIRMED, unchanged.** `Accelerometer::getIsSupportedProperty()`/`Start()`/
   `Dispose(bool)` all acquire it (three call sites grepped and confirmed); the
   lock-proof-parameter overloads from Task P8-3
   (`EnsureSubsystemInitialized`/`OpenDefaultSensorLocked`/`ProbeIsSupported`) are still
   in place in `SdlSensorSubsystem.hpp`.

5. **`VibrateController` does not hold probe-only haptic devices open accidentally —
   CONFIRMED, unchanged.** `getIsSupportedProperty()`/`getDeviceNameProperty()` both
   call `AcquireHapticDeviceForProbe(openedTemporary)` and both call
   `SDL_CloseHaptic(device)` when `openedTemporary` is true, before returning.

6. **The ZIP/submodule caveat is documented honestly — CONFIRMED, unchanged.** Present
   verbatim (Task P7-6) at the top of `docs/devices-build.md` and in `NEXT.md`'s
   Section 7.

**Additional note from reading `NOXNA.md`:** that file is entirely about the
*graphics*/rendering NOXNA extension layer (`CNA_NOXNA` CMake option, `CNA::Graphics`
namespace, PBR/HDR/shadows/Nova-3D) — a completely separate initiative from
`Microsoft::Devices`. It confirms the shared `NOXNA` marker convention
(`CNAHelper.hpp`'s documentation-only macro) but is otherwise irrelevant to this phase's
scope. No conflict: `Microsoft::Devices`'s `NOXNA`-tagged members (e.g.
`SetStartedForTesting()`) are always-compiled XNA-layer additions tagged for
documentation purposes, not gated behind `#ifdef CNA_NOXNA` the way the graphics
extension layer's types are — these are two different, non-conflicting uses of the same
marker, both consistent with `CLAUDE.md`'s definition.

**Conclusion: no concrete bug found in this audit pass.** No code edits made as a
result of P9-1 — matches the task's own instruction ("only edit code if you find a
concrete bug").

---

## Tasks

- P9-1 — Fresh audit before editing (this section)
- P9-2 — Reproduce the normal Devices test matrix locally
- P9-3 — Re-run sanitizer gates
- P9-4 — Android build and APK/demo path
- P9-5 — Physical hardware checklist execution
- P9-6 — DevicesDemo usability pass
- P9-7 — Final API/status table
- P9-8 — Native backend design boundary document
- P9-9 — Final acceptance decision

Each task gets its own `### Resolution` subsection below, plus its own commit where it
produces a file change.

## P9-2: Reproduce the normal Devices test matrix locally

### Resolution

**Blocker check:** `git submodule update --init --recursive` timed out after 2 minutes
(likely a slow remote-verification network round-trip in this container, not a real
problem) — but `git submodule status` immediately confirms all four submodules
(`third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer`,
`vendor/googletest`) are already initialized and checked out at their expected commits
(no `-`/`+` prefix). This repo was never in a fresh-clone state this session — no actual
blocker.

**Exact commands run, in order, exact results:**
```bash
cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
# Configuring done, Generating done — clean, no errors

cmake --build cmake-build-debug --target CNA -j"$(nproc)"
# Built target CNA — clean

cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
# Built target CnaTests — clean

./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
# [==========] 226 tests from 17 test suites ran.
# [  PASSED  ] 224 tests.
# [  SKIPPED ] 2 tests: AccelerometerTests.GetCurrentValuePropertyDoesNotThrowWhenSupported,
#              GyroscopeTests.GetCurrentValuePropertyDoesNotThrowWhenSupported
```

The 2 skips are expected and correct: both `GTEST_SKIP()` themselves specifically
*because* this container has no real accelerometer/gyroscope hardware — that is the
right outcome here, not a failure to investigate.

**No environment blocker found.** Matches Phase 8's final state exactly (226/224/2
skipped) — no drift, no regression, nothing to fix.

**Remaining risk:** none — pure reproduction, no code touched.
